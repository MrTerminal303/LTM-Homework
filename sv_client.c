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

static int send_all(int fd, const void *buf, size_t len) {
  size_t sent_total = 0;
  while (sent_total < len) {
    ssize_t sent = send(fd, (const char *)buf + sent_total, len - sent_total, 0);
    if (sent <= 0) {
      return -1;
    }
    sent_total += (size_t)sent;
  }
  return 0;
}

static int read_line(char *buf, size_t len) {
  if (fgets(buf, len, stdin) == NULL) {
    return -1;
  }
  buf[strcspn(buf, "\n")] = '\0';
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

  // send data to server via user input (ip, date time sent, mssv, hoten, ngaysinh, diem)
  struct sinhvien sv;
  char input[128];
  while (1) {
    printf("Enter mssv: ");
    if (read_line(input, sizeof(input)) < 0) {
      break;
    }
    sv.mssv = (int)strtol(input, NULL, 10);

    printf("Enter hoten: ");
    if (read_line(sv.hoten, sizeof(sv.hoten)) < 0) {
      break;
    }

    printf("Enter ngaysinh: ");
    if (read_line(sv.ngaysinh, sizeof(sv.ngaysinh)) < 0) {
      break;
    }

    printf("Enter diem: ");
    if (read_line(input, sizeof(input)) < 0) {
      break;
    }
    sv.diem = strtof(input, NULL);

    if (send_all(client, &sv, sizeof(sv)) < 0) {
      perror("send() failed");
      break;
    }

    char command[16];
    printf("Enter command (exit to quit): ");
    if (read_line(command, sizeof(command)) < 0) {
      break;
    }
    if (strncmp(command, "exit", 4) == 0)
      break;
  }

  close(client);
  return 0;
}