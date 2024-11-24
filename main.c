
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define BROADCAST_PORT 8888
#define TCP_PORT 9999
#define MAX_SERVERS 100
#define BUFFER_SIZE 1024
#define MAX_TASKS 1000

typedef struct {
    struct sockaddr_in addr;
    int available;
    int fd;
} WorkerNode;

typedef struct {
    int done;
    double ans;
    double left, right;
} Ans;

Ans ans[MAX_TASKS];
WorkerNode workers[MAX_SERVERS];
int worker_count = 0;

void broadcast_discover(int udp_fd, struct sockaddr_in *broadcast_addr) {
    const char *message = "DISCOVER";
    if (sendto(udp_fd, message, strlen(message), 0, (struct sockaddr *)broadcast_addr, sizeof(*broadcast_addr)) < 0) {
        perror("sendto");
        exit(1);
    }
}

void receive_worker_discovery(int udp_fd) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in worker_addr;
    socklen_t addr_len = sizeof(worker_addr);

    int bytes_received = recvfrom(udp_fd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&worker_addr, &addr_len);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Discovered worker: %s\n", inet_ntoa(worker_addr.sin_addr));
        printf("Msg: %s\n", buffer);

        if (strcmp(buffer, "WORKER_AVAILABLE") != 0) {
            printf("Skipping worker...\n");
            return;
        }

        // Add worker to the list
        int exists = 0;
        for (int i = 0; i < worker_count; i++) {
            if (workers[i].addr.sin_addr.s_addr == worker_addr.sin_addr.s_addr) {
                exists = 1;
                break;
            }
        }

        if (!exists && worker_count < MAX_SERVERS) {
            workers[worker_count].addr = worker_addr;
            workers[worker_count].available = 1;
            worker_count++;
        }
    }
}

void send_task(int fd, int id, double lb, double rb) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    snprintf(buffer, BUFFER_SIZE, "%d %f %f\n", id, lb, rb);
    if (write(fd, buffer, strlen(buffer)) < 0) {
        perror("write");
    } else {
        printf("Sent new task [%f, %f] to worker\n", lb, rb);
    }
}

int epoll_fd;
struct epoll_event event, events[MAX_SERVERS];

int task_count;

const int NUM_TASKS = 100;

void add_worker(int i) {
    int worker_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (worker_fd < 0) {
        perror("socket");
        return;
    }

    workers[i].addr.sin_port = htons(TCP_PORT);
    workers[i].fd = worker_fd;
    if (connect(worker_fd, (struct sockaddr *)&workers[i].addr, sizeof(workers[i].addr)) < 0) {
        perror("connect");
        close(worker_fd);
        workers[i].available = 0;
        return;
    }

    printf("Connected to worker: %s\n", inet_ntoa(workers[i].addr.sin_addr));
    workers[i].available = 1;

    for (int j = 0; j < NUM_TASKS; j++) {
        if (!ans[j].done) {
            send_task(worker_fd, j, ans[j].left, ans[j].right);
            break;
        }
    }

    fcntl(worker_fd, F_SETFL, O_NONBLOCK);  // Set non-blocking mode
    event.data.fd = worker_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker_fd, &event) < 0) {
        perror("epoll_ctl");
        close(worker_fd);
        workers[i].available = 0;
    }
}

int main() {
    printf("starting master...\n");
    fflush(stdout);

    // Create UDP socket for discovery
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        perror("socket");
        exit(1);
    }

    int broadcast_enable = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in broadcast_addr = {0};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    struct sockaddr_in master_addr = {0};
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(BROADCAST_PORT);
    master_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind UDP socket to listen for replies from workers
    if (bind(udp_fd, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    fflush(stdout);
    // Send broadcast message to discover workers
    broadcast_discover(udp_fd, &broadcast_addr);
    fflush(stdout);

    // Wait for workers to reply
    printf("Waiting for workers to reply...\n");
    for (int i = 0; i < 5; i++) {
        receive_worker_discovery(udp_fd);
        fflush(stdout);
    }

    printf("Discovered %d workers.\n", worker_count);
    fflush(stdout);

    // Create TCP socket for task dispatching
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd < 0) {
        perror("socket");
        exit(1);
    }

    // Use epoll to manage multiple worker connections
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        exit(1);
    }
    fflush(stdout);

    event.events = EPOLLIN;

    int remaining_tasks = NUM_TASKS;
    double start = 0.0, end = 1.0;
    double step = (end - start) / NUM_TASKS;

    for (int i = 0; i < NUM_TASKS; ++i) {
        ans[i].done = 0;
        ans[i].left = start + i * step;
        ans[i].right = ans[i].left + step;
    }

    // Connect to all discovered workers
    int connected_workers = 0;
    for (int i = 0; i < worker_count; i++) {
        add_worker(i);
    }

    printf("connected to %d workers\n", connected_workers);
    sleep(3);
    fflush(stdout);

    // Distribute tasks among workers

    while (remaining_tasks > 0) {
        printf("Have %d tasks remaining...\n", remaining_tasks);
        fflush(stdout);

        for (int i = 0; i < worker_count; i++) {
            if (workers[i].available == 0) {
                add_worker(i);
            }
        }

        int num_events = epoll_wait(epoll_fd, events, MAX_SERVERS, -1);
        printf("got %d events\n", num_events);
        fflush(stdout);
        if (num_events < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < num_events; i++) {
            int client_fd = events[i].data.fd;

            char buffer[BUFFER_SIZE];
            memset(buffer, 0, BUFFER_SIZE);

            // Read worker response
            int bytes = read(client_fd, buffer, BUFFER_SIZE);
            if (bytes > 0) {
                printf("Received result from worker: %s\n", buffer);
                int id;
                double res;
                if (sscanf(buffer, "%d %lf", &id, &res) == 2) {
                    ans[id].ans = res;
                    if (!ans[id].done) {
                        remaining_tasks--;
                    }
                    ans[id].done = 1;
                }
                // Assign new task if available
                if (remaining_tasks > 0) {
                    for (int i = 0; i < NUM_TASKS; i++) {
                        if (!ans[i].done) {
                            send_task(client_fd, i, ans[i].left, ans[i].right);
                            usleep(1000);
                            break;
                        }
                    }
                }
            } else if (bytes == 0) {
                // Worker disconnected
                printf("Worker disconnected.\n");
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                for (int i = 0; i < worker_count; ++i) {
                    if (workers[i].fd == client_fd) {
                        workers[i].available = 0;
                    }
                }
                close(client_fd);

            } else {
                perror("read");
            }
        }
    }

    printf("All tasks completed.\n");
    double res = 0;
    for (int i = 0; i < NUM_TASKS; i++) {
        res += ans[i].ans;
    }
    printf("Ans=%lf\n", res);
    fflush(stdout);

    close(tcp_fd);
    close(udp_fd);
    close(epoll_fd);
    return 0;
}
