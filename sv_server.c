#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>


struct sinhvien {
  int mssv;
  char hoten[64];
  char ngaysinh[16];
  float diem;
};
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

    // set up client address structure
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    client_addr.sin_port = htons(atoi(argv[1]));

    // set up SO_REUSEADDR to allow reuse of the port immediately after the server is closed
    int opt = 1; 
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { 
        perror("setsockopt failed"); 
        exit(EXIT_FAILURE); 
    }

    // bind to the specified port
    if (bind(listener, (struct sockaddr *)&client_addr, sizeof(client_addr))) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    // listen for incoming connections
    listen(listener, 5);

    int client = accept(listener, NULL, NULL);
    if (client < 0) {
        perror("accept() failed");
        exit(EXIT_FAILURE);
    }

    // open log file for writing
    FILE *log_fp = fopen(argv[2], "w");
    if (log_fp == NULL) {
        perror("fopen() failed");
        exit(EXIT_FAILURE);
    }

    // receive data from client and write to log file (ip, date time sent, mssv, hoten, ngaysinh, diem)
    struct sinhvien sv;
    while (1) {
        int bytes_received = recv(client, &sv, sizeof(sv), 0);
        if (bytes_received <= 0) {
            break;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        fprintf(log_fp, "Client IP: %s, MSSV: %d, Hoten: %s, Ngay Sinh: %s, Diem: %.2f\n", client_ip, sv.mssv, sv.hoten, sv.ngaysinh, sv.diem); 
        fflush(log_fp);
    }

    fclose(log_fp);
    close(client);
    close(listener);

    return 0;
}