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
    int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }
    // read ip address and port number from argv
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP_ADDRESS> <PORT>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char *ip_address = argv[1];
    int port_number = atoi(argv[2]);

    // set up server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", ip_address);
        close(client);
        exit(EXIT_FAILURE);
    }
    server_addr.sin_port = htons(port_number);

    // connect to server
    if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
        perror("connect() failed");
        close(client);
        exit(EXIT_FAILURE);
    }

    // receive server greeting once so connection success is visible
    char hello_buf[256];
    ssize_t hello_len = recv(client, hello_buf, sizeof(hello_buf) - 1, 0);
    if (hello_len < 0) {
        perror("recv() failed");
        close(client);
        exit(EXIT_FAILURE);
    }
    if (hello_len > 0) {
        hello_buf[hello_len] = '\0';
        printf("Server: %s", hello_buf);
        if (hello_buf[hello_len - 1] != '\n') {
            printf("\n");
        }
    }

    // send data to server via user input
    char buf[256];
    while (1) {
        printf("Enter string: ");
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            break;
        }
        if (send_all(client, buf, strlen(buf)) < 0) {
            perror("send() failed");
            break;
        }
        if (strncmp(buf, "exit", 4) == 0) {
            break;
        }
    }

    close(client);
    return 0;
}
