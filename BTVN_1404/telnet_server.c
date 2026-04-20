#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <ctype.h>
#include <time.h>

#define MAX_CLIENTS 1020

static void append_output_header(FILE *f, const char *user, int cfd, const char *cmd) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    char ts[64] = "";
    if (lt) {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", lt);
    }
    fprintf(f, "\n=== user=%s fd=%d time=%s cmd=\"%s\" ===\n",
            user[0] ? user : "(unknown)", cfd, ts[0] ? ts : "(unknown)", cmd);
}

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
            if (strcmp(pass, "") == 0 && strcmp(p, "nopass") == 0) {
                fclose(f);
                return 1;
            }
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
    
    int opt = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() failed");
        close(listener);
        return 1;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8000);
    
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

    struct client_info { int fd; int stage; char user[64]; } clients[MAX_CLIENTS];
    int nClients = 0;

    char buf[512];
    struct pollfd pfds[MAX_CLIENTS + 1];

    while (1) {
        // Khoi tao tap pollfd
        pfds[0].fd = listener;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        for (int i = 0; i < nClients; i++) {
            pfds[i + 1].fd = clients[i].fd;
            pfds[i + 1].events = POLLIN;
            pfds[i + 1].revents = 0;
        }

        // Goi ham poll (blocking)
        int ret = poll(pfds, nClients + 1, -1);
        if (ret < 0) {
            perror("poll() failed");
            break;
        }
        if (ret == 0) {
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            int client = accept(listener, NULL, NULL);
            if (nClients < MAX_CLIENTS) {
                clients[nClients].fd = client;
                clients[nClients].stage = 0; // waiting for username
                clients[nClients].user[0] = '\0';
                nClients++;
                printf("New client connected: %d\n", client);
                const char *msg = "Welcome\n";
                send(client, msg, strlen(msg), 0);
                send(client, "Username: ", strlen("Username: "), 0);
            } else {
                printf("Too many connections\n");
                const char *msg = "Sorry. Out of slots.\n";
                send(client, msg, strlen(msg), 0);
                close(client);
            }
        }

        for (int i = 0; i < nClients; i++) {
            int cfd = clients[i].fd;
            if (pfds[i + 1].revents & POLLIN) {
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
                        clients[i].stage = 0;
                        clients[i].user[0] = '\0';
                        send(cfd, "Login failed. Try again\n", strlen("Login failed. Try again\n"), 0);
                        send(cfd, "Username: ", strlen("Username: "), 0);
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

                    // Run command to a temp file, then append to shared out.txt
                    char cmd[1024];
                    char tmpout[128];
                    snprintf(tmpout, sizeof(tmpout), "out_%d.tmp", cfd);
                    snprintf(cmd, sizeof(cmd), "%s\"%s\" > %s 2>&1", SHELL_PREFIX, buf, tmpout);
                    system(cmd);

                    FILE *fo = fopen(tmpout, "rb");
                    if (fo) {
                        FILE *fa = fopen("out.txt", "ab");
                        if (fa) {
                            append_output_header(fa, clients[i].user, cfd, buf);
                        }
                        char outbuf[1024];
                        size_t n;
                        while ((n = fread(outbuf, 1, sizeof(outbuf), fo)) > 0) {
                            send(cfd, outbuf, n, 0);
                            if (fa) fwrite(outbuf, 1, n, fa);
                        }
                        if (fa) fclose(fa);
                        fclose(fo);
                        remove(tmpout);
                    } else {
                        const char *errmsg = "(no output)\n";
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