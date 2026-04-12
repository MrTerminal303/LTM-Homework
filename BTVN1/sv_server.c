#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>


struct sinhvien {
  int mssv;
  char hoten[64];
  char ngaysinh[16];
  float diem;
};

static int recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t got = recv(fd, (char *)buf + total, len - total, 0);
        if (got == 0) {
            return 0;
        }
        if (got < 0) {
            return -1;
        }
        total += (size_t)got;
    }
    return 1;
}

int main(int argc, char *argv[]) {

    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    // read file log path and port number from argv
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <PORT> <LOG_FILE>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // set up local bind address
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(atoi(argv[1]));

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

    // reap child processes automatically
    signal(SIGCHLD, SIG_IGN);

    // accept loop - fork a child for each client
    while (1) {
        struct sockaddr_in peer_addr;
        socklen_t peer_len = sizeof(peer_addr);
        int client = accept(listener, (struct sockaddr *)&peer_addr, &peer_len);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept() failed");
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork() failed");
            close(client);
            continue;
        }

        if (pid == 0) {
            // child
            close(listener);

            // open log file in append mode so multiple children don't truncate
            FILE *log_fp = fopen(argv[2], "a");
            if (log_fp == NULL) {
                perror("fopen() failed in child");
                close(client);
                exit(EXIT_FAILURE);
            }

            struct sinhvien sv;
            while (1) {
                int rc = recv_all(client, &sv, sizeof(sv));
                if (rc == 0) {
                    break;
                }
                if (rc < 0) {
                    perror("recv() failed");
                    break;
                }

                char client_ip[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &peer_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL) {
                    strncpy(client_ip, "unknown", sizeof(client_ip));
                    client_ip[sizeof(client_ip) - 1] = '\0';
                }

                char timestamp[32];
                time_t now = time(NULL);
                struct tm *tm_info = localtime(&now);
                if (tm_info != NULL) {
                    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                } else {
                    strncpy(timestamp, "unknown-time", sizeof(timestamp));
                    timestamp[sizeof(timestamp) - 1] = '\0';
                }

                fprintf(log_fp, "%s %s %d %s %s %.2f\n",
                        client_ip, timestamp, sv.mssv, sv.hoten, sv.ngaysinh, sv.diem);
                printf("Client IP: %s, Time: %s, MSSV: %d, Hoten: %s, Ngay Sinh: %s, Diem: %.2f\n",
                       client_ip, timestamp, sv.mssv, sv.hoten, sv.ngaysinh, sv.diem);
                fflush(log_fp);
            }

            fclose(log_fp);
            close(client);
            exit(0);
        } else {
            // parent
            close(client);
            continue;
        }
    }

    close(listener);
    return 0;
}