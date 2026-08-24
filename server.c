#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <arpa/inet.h>

#define PORT "9034"
#define BACKLOG 10
#define MAXDATASIZE 100

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
int handle_command(struct pollfd *fds, Client *clients, int i, int c_count, int listenfd, char *buf);
void send_client_list(struct pollfd *fds, Client *clients, int c_count, int listenfd, int requester_fd);
int try_set_username(struct pollfd *fds, Client *clients, int i, int c_count, const char *name);

int main(void) {

    struct addrinfo hints, *ai, *p;
    int rv;
    int listenfd;
    int yes = 1;
    char buf[MAXDATASIZE];
    
    struct pollfd *fds = NULL;
    Client *clients = NULL;
    int c_count = 0;
    int c_size = 0;
    char str[INET6_ADDRSTRLEN];

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
    while (1) {

        int poll_count = poll(fds, c_count, -1);
        if (poll_count == -1){
            perror("poll");
            exit(1);
        }

        for(int i = 0; i < c_count; i++){
            //NO EVENTS
            if((fds[i].revents & POLLIN) == 0){
                continue;
            }

            //LISTENER SOCKET READY
            if(fds[i].fd == listenfd){
                struct sockaddr_storage their_addr;
                socklen_t addr_size = sizeof their_addr;
                int newfd = accept(listenfd, (struct sockaddr *)&their_addr, &addr_size);
                if(newfd == -1){
                    perror("accept");
                    continue;
                }
                add_client(&fds, &clients, newfd, &c_count, &c_size);
                inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), str, sizeof str);
                printf("server: new connection from %s on socket %d\n", str, newfd);
                send_to_one(newfd, "Welcome to the chat server! Please set your username using /username <name>:\n");
                send_to_one(newfd, "Commands: \n /username <new name>\n /list\n /disconnect\n\n");
            }
            
            //CLIENT SOCKET READY
            else{
                memset(buf, 0, sizeof buf);
                int nbytes = recv(fds[i].fd, buf, sizeof buf, 0);

                //CLIENT DISCONNECTED OR RECV() FAIL
                if(nbytes <= 0){
                    if(nbytes == 0){
                        if(clients[i].has_username) {
                            char message[MAXDATASIZE + 32 + 8];
                            snprintf(message, sizeof message, "%s has been disconnected.\n", clients[i].username);
                            broadcast(fds, c_count, fds[i].fd, listenfd, message);
                        }
                        else{
                            printf("server: socket %d hung up\n", fds[i].fd);
                        } 
                    } 
                    else {
                        perror("recv");
                    }
                    close(fds[i].fd);
                    remove_client(fds, clients, i, &c_count);
                    i--;
                } 
                //REAL DATA RECEIVED
                //SET USERNAME
                else {
                    trim_newline(buf);
                    if(!clients[i].has_username){
                        if(strcmp(buf, "/disconnect") == 0){
                            send_to_one(fds[i].fd, "Disconnecting...\n");
                            close(fds[i].fd);
                            remove_client(fds, clients, i, &c_count);
                            i--;
                        }   
                        else{ 
                            char cmd[32] = {0};
                            char arg[MAXDATASIZE] = {0};
                            sscanf(buf, "%31s %99[^\n]", cmd, arg);

                            if (strcmp(cmd, "/username") != 0) {
                                send_to_one(fds[i].fd, "Please set a username first: /username <name>\n");
                            }
                            else if (try_set_username(fds, clients, i, c_count, arg)) {
                                clients[i].has_username = 1;
                                send_to_one(fds[i].fd, "Username set successfully.\n");
                                char join_msg[64];
                                snprintf(join_msg, sizeof join_msg, "%s has joined\n", clients[i].username);
                                broadcast(fds, c_count, fds[i].fd, listenfd, join_msg);
                            }
                        }
                    }
                    //COMMAND OR BROADCAST MESSAGE
                    else {
                            if (buf[0] == '/') {
                                int disconnect_requested = handle_command(fds, clients, i, c_count, listenfd, buf);
                                if (disconnect_requested) {
                                    char message[MAXDATASIZE + 32 + 8];
                                    snprintf(message, sizeof message, "%s has left the chat.\n", clients[i].username);
                                    broadcast(fds, c_count, fds[i].fd, listenfd, message);
                                    close(fds[i].fd);
                                    remove_client(fds, clients, i, &c_count);
                                    i--;
                                }
                            } else {
                                char message[MAXDATASIZE + 32 + 8];
                                snprintf(message, sizeof message, "%s: %s\n", clients[i].username, buf);
                                broadcast(fds, c_count, fds[i].fd, listenfd, message);
                            }
                    }

                }
            }    
       
        }
            
    }

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

int handle_command(struct pollfd *fds, Client *clients, int i, int c_count, int listenfd, char *buf) {
    char cmd[32] = {0};
    char arg[MAXDATASIZE] = {0};
    sscanf(buf, "%31s %99[^\n]", cmd, arg);

    if (strcmp(cmd, "/username") == 0) {
        char old_name[32];
        strncpy(old_name, clients[i].username, sizeof old_name - 1);
        old_name[sizeof old_name - 1] = '\0';

        if (try_set_username(fds, clients, i, c_count, arg)) {
            send_to_one(fds[i].fd, "Username changed successfully.\n");
            char msg[96];
            snprintf(msg, sizeof msg, "%s is now known as %s\n", old_name, clients[i].username);
            broadcast(fds, c_count, fds[i].fd, listenfd, msg);
        }
        return 0;
    }

    if (strcmp(cmd, "/list") == 0) {
        send_client_list(fds, clients, c_count, listenfd, fds[i].fd);
        return 0;
    }

    if (strcmp(cmd, "/disconnect") == 0) {
        send_to_one(fds[i].fd, "Disconnecting...\n");
        return 1;
    }

    send_to_one(fds[i].fd, "Unknown command. Available commands: /username <name>, /list, /disconnect\n");
    return 0;
}

int try_set_username(struct pollfd *fds, Client *clients, int i, int c_count, const char *name) {
    if (name[0] == '\0') {
        send_to_one(fds[i].fd, "Usage: /username <name>\n");
        return 0;
    }
    if (find_client_by_username(clients, c_count, name) != -1) {
        send_to_one(fds[i].fd, "Username already taken. Please choose another one.\n");
        return 0;
    }
    strncpy(clients[i].username, name, sizeof clients[i].username - 1);
    clients[i].username[sizeof clients[i].username - 1] = '\0';
    return 1;
}

void send_client_list(struct pollfd *fds, Client *clients, int c_count, int listenfd, int requester_fd) {
    char list_msg[1024];
    int offset = snprintf(list_msg, sizeof list_msg, "Connected users:\n");

    for (int j = 0; j < c_count && offset < (int)sizeof list_msg; j++) {

        if (fds[j].fd == listenfd || !clients[j].has_username) { continue; }

        int n = snprintf(list_msg + offset, sizeof list_msg - offset, "- %s%s\n",
                          clients[j].username, fds[j].fd == requester_fd ? " (you)" : "");
        if (n < 0) { break; }
        offset += n;
    }

    if (offset >= (int)sizeof list_msg) {
        offset = sizeof list_msg - 1;
    }
    list_msg[offset] = '\0';

    send_to_one(requester_fd, list_msg);
}