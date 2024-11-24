#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define UDP_PORT 8888
#define TCP_PORT 9999
#define BUFFER_SIZE 1024
#define EPOLL_MAX_EVENTS 10

double function(double x) {
    return x * x;
}

double integrate(double start, double end, int num_points) {
    double step = (end - start) / num_points;
    double result = 0.0;

    for (int i = 0; i < num_points; i++) {
        double x1 = start + i * step;
        double x2 = x1 + step;
        result += 0.5 * (x2 - x1) * (function(x1) + function(x2));
    }

    return result;
}

int set_non_blocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}

int main() {
    int udp_fd, tcp_fd, epoll_fd;
    struct sockaddr_in udp_addr, tcp_addr;
    struct epoll_event ev, events[EPOLL_MAX_EVENTS];

    // Создание UDP-сокета
    if ((udp_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket");
        exit(EXIT_FAILURE);
    }
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(UDP_PORT);
    udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("bind UDP");
        exit(EXIT_FAILURE);
    }

    printf("UDP socket listening on port %d\n", UDP_PORT);

    // Создание TCP-сокета
    if ((tcp_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket");
        exit(EXIT_FAILURE);
    }
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port = htons(TCP_PORT);
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tcp_fd, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("bind TCP");
        exit(EXIT_FAILURE);
    }

    if (listen(tcp_fd, 10) < 0) {
        perror("TCP listen");
        exit(EXIT_FAILURE);
    }

    printf("TCP socket listening on port %d\n", TCP_PORT);

    // Настройка epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }

    // Добавление UDP сокета в epoll
    ev.events = EPOLLIN;
    ev.data.fd = udp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev) < 0) {
        perror("epoll_ctl: udp_fd");
        exit(EXIT_FAILURE);
    }

    // Добавление TCP сокета в epoll
    ev.events = EPOLLIN;
    ev.data.fd = tcp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_fd, &ev) < 0) {
        perror("epoll_ctl: tcp_fd");
        exit(EXIT_FAILURE);
    }

    // Основной цикл обработки epoll
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1); // Блокируемся, пока не произойдут события
        if (nfds < 0) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == udp_fd) {
                // Обработка UDP-сообщений (broadcast)
                char buffer[BUFFER_SIZE];
                struct sockaddr_in master_addr;
                socklen_t addr_len = sizeof(master_addr);

                int bytes = recvfrom(udp_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&master_addr, &addr_len);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    printf("Received UDP message: %s\n", buffer);
                    fflush(stdout);

                    if (strcmp(buffer, "DISCOVER") == 0) {
                        const char *response = "WORKER_AVAILABLE";
                        sendto(udp_fd, response, strlen(response), 0, (struct sockaddr *)&master_addr, addr_len);
                        printf("Sent UDP response to master: %s\n", inet_ntoa(master_addr.sin_addr));
                    }
                }
            } else if (events[i].data.fd == tcp_fd) {
                // Обработка новых TCP-соединений
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(tcp_fd, (struct sockaddr *)&client_addr, &addr_len);
                if (client_fd < 0) {
                    perror("accept");
                    continue;
                }

                printf("Accepted connection from master: %s\n", inet_ntoa(client_addr.sin_addr));
                fflush(stdout);

                // Установим клиентский сокет в неблокирующий режим
                set_non_blocking(client_fd);

                // Добавим новый клиентский сокет в epoll
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                    perror("epoll_ctl: client_fd");
                    close(client_fd);
                }
            } else {
                // Обработка данных от TCP-клиентов
                int client_fd = events[i].data.fd;
                char buffer[BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));

                int bytes = read(client_fd, buffer, BUFFER_SIZE);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    printf("Received task from master: %s\n", buffer);
                    fflush(stdout);

                    // Обработка задания на интегрирование
                    int id;
                    double start, end;
                    if (sscanf(buffer, "%d %lf %lf", &id, &start, &end) == 3) {
                        printf("Task: integrate from %f to %f, id=%d\n", start, end, id);
                        fflush(stdout);

                        // Вычислим интеграл
                        double result = integrate(start, end, 1000);

                        // Отправим результат обратно мастеру
                        snprintf(buffer, BUFFER_SIZE, "%d %f", id, result);
                        if (write(client_fd, buffer, strlen(buffer)) < 0) {
                            perror("write");
                        } else {
                            printf("Result sent to master: %f for range [%f, %f]\n", result, start, end);
                            fflush(stdout);
                        }
                    }
                } else if (bytes == 0) {
                    // Подключение закрыто мастером
                    printf("Master disconnected from client_fd: %d\n", client_fd);
                    fflush(stdout);
                    close(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                } else {
                    perror("read");
                    close(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                }
            }
            fflush(stdout);
        }
    }

    close(udp_fd);
    close(tcp_fd);
    close(epoll_fd);

    return 0;
}
