#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent_total = 0;
    while (sent_total < len) {
        ssize_t sent = send(fd, buf + sent_total, len - sent_total, 0);
        if (sent <= 0) {
            return -1;
        }
        sent_total += (size_t)sent;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    // read port number, file contain hello string, path to save file from argv
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <PORT> <HELLO_FILE> <SAVE_PATH>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port_number = atoi(argv[1]);
    char *hello_file = argv[2];
    char *save_path = argv[3];

    // set up local bind address
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(port_number);

    // set up SO_REUSEADDR to allow reuse of the port immediately after the server is closed
    int opt = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // bind to the specified port
    if (bind(listener, (struct sockaddr *)&bind_addr, sizeof(bind_addr))) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    // listen for incoming connections
    if (listen(listener, 5) < 0) {
        perror("listen() failed");
        exit(EXIT_FAILURE);
    }

    int client = accept(listener, NULL, NULL);
    if (client < 0) {
        perror("accept() failed");
        exit(EXIT_FAILURE);
    }

    // read hello string from file and send to client
    char hello_buf[256];
    FILE *hello_fp = fopen(hello_file, "r");
    if (hello_fp == NULL) {
        perror("fopen() failed");
        exit(EXIT_FAILURE);
    }
    if (fgets(hello_buf, sizeof(hello_buf), hello_fp) == NULL) {
        perror("fgets() failed");
        exit(EXIT_FAILURE);
    }
    fclose(hello_fp);

    // send hello string to client
    if (send_all(client, hello_buf, strlen(hello_buf)) < 0) {
        perror("send() failed");
        close(client);
        close(listener);
        exit(EXIT_FAILURE);
    }

    // receive data from client and save to file
    char recv_buf[256];
    FILE *save_fp = fopen(save_path, "w");
    if (save_fp == NULL) {
        perror("fopen() failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        ssize_t len = recv(client, recv_buf, sizeof(recv_buf) - 1, 0);
        if (len <= 0) {
            break;
        }
        recv_buf[len] = 0;
        fputs(recv_buf, save_fp);
        printf("Received: %s", recv_buf);
    }
    fclose(save_fp);

    close(client);
    close(listener);
    return 0;
}