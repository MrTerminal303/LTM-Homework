#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/select.h>
#include <ctype.h>

#define MAX_CLIENTS 1020

void removeClient(int *clients, int *nClients, int i) {
    if (i < *nClients - 1)
        clients[i] = clients[*nClients - 1];
    *nClients -= 1;
}

// Trim trailing CR/LF and spaces
static void rtrim(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || isspace((unsigned char)s[len-1]))) {
        s[len-1] = '\0';
        len--;
    }
}

// Check credentials file. Each line: username password
static int check_credentials(const char *dbfile, const char *user, const char *pass) {
    FILE *f = fopen(dbfile, "r");
    if (!f) return 0;
    char line[512];
    char u[128], p[128];
    while (fgets(line, sizeof(line), f)) {
        // skip empty lines
        if (sscanf(line, "%127s %127s", u, p) >= 1) {
            if (strcmp(u, user) == 0 && strcmp(p, pass) == 0) {
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    return 0;
}

#ifdef _WIN32
#define SHELL_PREFIX "cmd /c "
#else
#define SHELL_PREFIX "/bin/sh -c "
#endif

int main() {
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == -1) {
        perror("socket() failed");
        return 1;
    }
    
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))) {
        perror("setsockopt() failed");
        close(listener);
        return 1;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);
    
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind() failed");
        close(listener);
        return 1;
    }
    
    if (listen(listener, 5)) {
        perror("listen() failed");
        close(listener);
        return 1;
    }
    
    // Server is now listening for incoming connections
    printf("Server is listening on port 8080...\n");

    struct client_info { int fd; int stage; char user[64]; } clients[MAX_CLIENTS];
    int nClients = 0;

    fd_set fdread;
    char buf[512];
    struct timeval tv;

    while (1) {
        // Khoi tao tap fdread
        FD_ZERO(&fdread);
        FD_SET(listener, &fdread);
        int maxdp = listener + 1;
        for (int i = 0; i < nClients; i++) {
            FD_SET(clients[i].fd, &fdread);
            if (maxdp < clients[i].fd + 1)
                maxdp = clients[i].fd + 1;
        }

        // Goi ham select (blocking)
        int ret = select(maxdp, &fdread, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select() failed");
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (FD_ISSET(listener, &fdread)) {
            int client = accept(listener, NULL, NULL);
            if (nClients < MAX_CLIENTS) {
                clients[nClients].fd = client;
                clients[nClients].stage = 0; // waiting for username
                clients[nClients].user[0] = '\0';
                nClients++;
                printf("New client connected: %d\n", client);
                char *msg = "Welcome\n";
                send(client, msg, strlen(msg), 0);
                send(client, "Username: ", strlen("Username: "), 0);
            } else {
                printf("Too many connections\n");
                char *msg = "Sorry. Out of slots.\n";
                send(client, msg, strlen(msg), 0);
                close(client);
            }
        }

        for (int i = 0; i < nClients; i++) {
            int cfd = clients[i].fd;
            if (FD_ISSET(cfd, &fdread)) {
                ret = recv(cfd, buf, sizeof(buf) - 1, 0);
                if (ret <= 0) {
                    // Ket noi bi dong
                    printf("Client %d disconnected\n", cfd);
                    close(cfd);
                    clients[i] = clients[nClients - 1];
                    nClients--;
                    i--;
                    continue;
                }
                buf[ret] = 0;
                rtrim(buf);
                printf("Received from %d: %s\n", cfd, buf);

                if (clients[i].stage == 0) {
                    // got username
                    strncpy(clients[i].user, buf, sizeof(clients[i].user)-1);
                    clients[i].user[sizeof(clients[i].user)-1] = '\0';
                    clients[i].stage = 1;
                    send(cfd, "Password: ", strlen("Password: "), 0);
                    continue;
                } else if (clients[i].stage == 1) {
                    // got password, validate
                    const char *dbfile = "users.txt";
                    if (check_credentials(dbfile, clients[i].user, buf)) {
                        clients[i].stage = 2; // authenticated
                        send(cfd, "Login successful\n", strlen("Login successful\n"), 0);
                        send(cfd, "> ", 2, 0);
                    } else {
                        send(cfd, "Login failed\n", strlen("Login failed\n"), 0);
                        close(cfd);
                        clients[i] = clients[nClients - 1];
                        nClients--;
                        i--;
                    }
                    continue;
                } else {
                    // authenticated: treat as command
                    if (strlen(buf) == 0) {
                        send(cfd, "> ", 2, 0);
                        continue;
                    }
                    if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) {
                        send(cfd, "Goodbye\n", strlen("Goodbye\n"), 0);
                        close(cfd);
                        clients[i] = clients[nClients - 1];
                        nClients--;
                        i--;
                        continue;
                    }

                    // Build command that redirects output to out.txt
                    char cmd[1024];
                    snprintf(cmd, sizeof(cmd), "%s%s > out.txt 2>&1", SHELL_PREFIX, buf);
                    system(cmd);
                    // Read out.txt and send back to client
                    FILE *fo = fopen("out.txt", "rb");
                    if (fo) {
                        char outbuf[1024];
                        size_t n;
                        while ((n = fread(outbuf, 1, sizeof(outbuf), fo)) > 0) {
                            send(cfd, outbuf, n, 0);
                        }
                        fclose(fo);
                    } else {
                        char *errmsg = "(no output)\n";
                        send(cfd, errmsg, strlen(errmsg), 0);
                    }
                    // Prompt again
                    send(cfd, "\n> ", 3, 0);
                }
            }
        }
    }

    close(listener);
    return 0;
}