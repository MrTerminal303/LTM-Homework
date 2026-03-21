#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

struct sinhvien {
  int mssv;
  char hoten[64];
  char ngaysinh[16];
  float diem;
};

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
  server_addr.sin_addr.s_addr = inet_addr(ip_address);
  server_addr.sin_port = htons(port_number);

  // connect to server
  if (connect(client, (struct sockaddr *)&server_addr, sizeof(server_addr))) {
    perror("connect() failed");
    exit(EXIT_FAILURE);
  }

  // send data to server via user input (ip, date time sent, mssv, hoten, ngaysinh, diem)
  struct sinhvien sv;
  while (1) {
    printf("Enter mssv: ");
    fflush(stdin);scanf("%d", &sv.mssv);
    printf("Enter hoten: ");
    fflush(stdin);scanf("%s", sv.hoten);
    printf("Enter ngaysinh: ");
    fflush(stdin);scanf("%s", sv.ngaysinh);
    printf("Enter diem: ");
    fflush(stdin);scanf("%f", &sv.diem);
    send(client, &sv, sizeof(sv), 0);

    //send timestamp
    char timestamp[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    send(client, timestamp, strlen(timestamp), 0);

    char command[16];
    printf("Enter command (exit to quit): ");
    fflush(stdin);scanf("%s", command);
    if (strncmp(command, "exit", 4) == 0)
      break;
  }

  close(client);
  return 0;
}