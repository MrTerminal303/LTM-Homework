#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>

#define MAX_CLIENTS 1020
#define BUFSIZE 4096

typedef struct {
    int fd;
    char id[64];
    char name[128];
    int registered;
} client_t;

void removeClient(client_t *clients, int *nClients, int i) {
    close(clients[i].fd);
    if (i < *nClients - 1)
        clients[i] = clients[*nClients - 1];
    *nClients -= 1;
}

static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static void make_timestamp(char *buf, size_t bufsz) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (!lt) { buf[0] = '\0'; return; }
    strftime(buf, bufsz, "%Y/%m/%d %I:%M:%S%p", lt);
}

int main() {

    // Create listening socket
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed");
        return 1;
    }

    // Allow reuse of address
    int opt = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt() failed");
        close(listener);
        return 1;
    }

    // Bind to port 8000 on all interfaces
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8000);

    // Bind and listen
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind() failed");
        close(listener);
        return 1;
    }

    if (listen(listener, 10)) {
        perror("listen() failed");
        close(listener);
        return 1;
    }

    client_t clients[MAX_CLIENTS];
    int nClients = 0;

    char buf[BUFSIZE];
    struct pollfd pfds[MAX_CLIENTS + 1];

    // Main loop
    while (1) {
        // Prepare pollfd set
        pfds[0].fd = listener;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        for (int i = 0; i < nClients; i++) {
            pfds[i + 1].fd = clients[i].fd;
            pfds[i + 1].events = POLLIN;
            pfds[i + 1].revents = 0;
        }

        // Wait for activity
        int ret = poll(pfds, nClients + 1, -1);
        if (ret < 0) {
            perror("poll() failed");
            break;
        }

        // Check for new connection
        if (pfds[0].revents & POLLIN) {
            int clientfd = accept(listener, NULL, NULL);
            if (clientfd < 0) {
                perror("accept() failed");
            } else {
                if (nClients < MAX_CLIENTS) {
                    // Add new client
                    clients[nClients].fd = clientfd;
                    clients[nClients].registered = 0;
                    clients[nClients].id[0] = '\0';
                    clients[nClients].name[0] = '\0';
                    nClients++;
                    printf("New connection: fd=%d\n", clientfd);
                    const char *welcome = "Welcome to chat server.\nPlease register using: client_id:client_name\n";
                    send(clientfd, welcome, strlen(welcome), 0);
                } else {
                    // Server full
                    const char *full = "Server full. Try later.\n";
                    send(clientfd, full, strlen(full), 0);
                    close(clientfd);
                }
            }
        }

        // Check for client messages
        for (int i = 0; i < nClients; i++) {

            // Check if this client's socket has data
            int fd = clients[i].fd;
            if (!(pfds[i + 1].revents & POLLIN)) continue;
            ret = recv(fd, buf, sizeof(buf) - 1, 0);
            if (ret <= 0) {
                printf("Client fd=%d disconnected\n", fd);
                removeClient(clients, &nClients, i);
                i--;
                continue;
            }
            buf[ret] = '\0';
            // strip CR/LF
            char *p = strchr(buf, '\n'); if (p) *p = '\0';
            p = strchr(buf, '\r'); if (p) *p = '\0';
            
            // If not registered, expect registration message
            if (!clients[i].registered) {
                // expect: client_id:client_name
                char copy[BUFSIZE];
                strncpy(copy, buf, sizeof(copy) - 1);
                copy[sizeof(copy) - 1] = '\0';
                char *colon = strchr(copy, ':');
                if (!colon) {
                    const char *msg = "Invalid format. Use client_id:client_name\n";
                    send(fd, msg, strlen(msg), 0);
                    continue;
                }

                // Split into id and name
                *colon = '\0';
                char *idpart = copy;
                char *namepart = colon + 1;
                trim(idpart);
                trim(namepart);
                if (idpart[0] == '\0' || namepart[0] == '\0') {
                    const char *msg = "Invalid format. Both id and name required.\n";
                    send(fd, msg, strlen(msg), 0);
                    continue;
                }

                // Save registration info
                strncpy(clients[i].id, idpart, sizeof(clients[i].id) - 1);
                clients[i].id[sizeof(clients[i].id) - 1] = '\0';
                strncpy(clients[i].name, namepart, sizeof(clients[i].name) - 1);
                clients[i].name[sizeof(clients[i].name) - 1] = '\0';
                clients[i].registered = 1;

                // Send acknowledgment
                char ack[256];
                snprintf(ack, sizeof(ack), "Registered as %s:%s\n", clients[i].id, clients[i].name);
                send(fd, ack, strlen(ack), 0);
                printf("Client fd=%d registered: %s (%s)\n", fd, clients[i].id, clients[i].name);
                continue;
            }

            // Prepare broadcast message to other clients
            char out[BUFSIZE * 2];
            char ts[64];
            make_timestamp(ts, sizeof(ts));
            if (ts[0]) snprintf(out, sizeof(out), "%s %s: %s\n", ts, clients[i].id, buf);
            else snprintf(out, sizeof(out), "%s: %s\n", clients[i].id, buf);
            printf("%s %s: %s\n", ts, clients[i].id, buf);

            // Send to all other registered clients
            for (int j = 0; j < nClients; j++) {
                if (j == i) continue;
                if (!clients[j].registered) continue;
                if (send(clients[j].fd, out, strlen(out), 0) < 0) {
                    perror("send() failed");
                }
            }
        }
    }

    for (int i = 0; i < nClients; i++) close(clients[i].fd);
    close(listener);
    return 0;
}