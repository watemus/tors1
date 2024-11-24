FROM ubuntu:latest

RUN apt-get update && apt-get install -y gcc iptables

WORKDIR /app
COPY . .

RUN gcc main.c -o main
RUN gcc worker.c -o worker
