#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

#define PORT "9034"
#define BACKLOG 10

typedef struct {
        char username[32];
        int has_username;
    } Client;

void *get_in_addr(struct sockaddr *sa);
void add_client(struct pollfd **fds, Client **clients, int newfd, int *count, int *size);
void remove_client(struct pollfd *fds, Client *clients, int i, int *count);
int find_client_by_username(Client *clients, int count, const char *name);
void trim_newline(char *str);
void broadcast(struct pollfd *fds, int count, int sender_fd, int listenfd, const char *msg);
void send_to_one(int fd, const char *msg); 
int sendall(int s, const char *buf, int *len);

int main(void) {

    struct addrinfo hints, *ai, *p;
    int rv;
    int listenfd;
    int yes = 1;
    
    struct pollfd *fds = NULL;
    Client *clients = NULL;
    int c_count = 0;
    int c_size = 0;

    //listener config settings
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    //connection trial loop
    for (p = ai; p != NULL; p = p->ai_next) {
        
        listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listenfd == -1) { continue; }

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(listenfd);
            continue;
        }

    break;
    }

    freeaddrinfo(ai);

    //error catch - never entered loop
    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("listen");
        return 3;
    }

    printf("server: listening on port %s\n", PORT);   //listen() success
    add_client(&fds, &clients, listenfd, &c_count, &c_size);
    
    // TODO: MAIN CONTROL LOOP - poll() loop goes here 

    close(listenfd);
    return 0;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void add_client(struct pollfd **fds, Client **clients, int newfd, int *count, int *size) {
    if (*count == *size) {
        *size = (*size == 0) ? 1 : (*size * 2);
        *fds = realloc(*fds, sizeof(struct pollfd) * (*size));
        *clients = realloc(*clients, sizeof(Client) * (*size));
    }
    (*fds)[*count].fd = newfd;
    (*fds)[*count].events = POLLIN;
    (*fds)[*count].revents = 0;

    (*clients)[*count].has_username = 0;
    memset((*clients)[*count].username, 0, sizeof (*clients)[*count].username);

    (*count)++;
}

void remove_client(struct pollfd *fds, Client *clients, int i, int *count) {
    if (i < *count - 1) {
        fds[i] = fds[*count - 1];
        clients[i] = clients[*count - 1];
    }
    (*count)--;
}

int find_client_by_username(Client *clients, int c_count, const char *name)
{
    for (int i = 0; i < c_count; i++) {
        if (clients[i].has_username && strcmp(clients[i].username, name) == 0) {
            return i;
        }
    }
    return -1;
}   

void trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
}

void broadcast(struct pollfd *fds, int count, int sender_fd, int listenfd, const char *msg) {
    int len = strlen(msg);
    for (int j = 0; j < count; j++) {
        if (fds[j].fd == sender_fd || fds[j].fd == listenfd) {
            continue;
        }
        int msg_len = len;
        sendall(fds[j].fd, msg, &msg_len);
    }
}

void send_to_one(int fd, const char *msg) {
    int len = strlen(msg);
    sendall(fd, msg, &len);
}

int sendall(int s, const char *buf, int *len) {
    int total = 0;
    int bytesleft = *len;
    int n;

    while (total < *len) {
        n = send(s, buf + total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total;
    return n == -1 ? -1 : 0;
}