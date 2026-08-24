/*
** client.c - full client code
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#define PORT "9034" // the port client will be connecting to
#define MAXDATASIZE 100

void *get_in_addr(struct sockaddr *sa);
void trim_newline(char *str);
int sendall(int s, const char *buf, int *len);
int send_line(int sockfd, const char *line);

int main(int argc, char *argv[])
{
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    struct pollfd fds[2]; 

    if (argc != 2) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }

    //config settings
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p=servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
        printf("client: attempting connection to %s\n", s);
        
        if (connect(sockfd, p->ai_addr,p->ai_addrlen)== -1) {
            perror("client: connect");
            close(sockfd);
            continue;
        }
        
        break;
    }

    if(p == NULL){
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }
    
    printf("client: connected to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure

    fds[0].fd = sockfd;
    fds[0].events = POLLIN;
    fds[1].fd = STDIN_FILENO;
    fds[1].events = POLLIN;

    while(1){
        int poll_count = poll(fds, 2, -1);
            
        if(poll_count == -1){ 
                perror("poll");
                exit(1);
        }

        //received msg from server
        if(fds[0].revents & POLLIN){
                
            int recvd = recv(sockfd, buf, sizeof(buf), 0);
            if(recvd == 0){
                printf("client: connection closed\n");
                break;
            }  
            else if(recvd == -1){
                perror("recv");
                exit(1);
            } 
            else {
                buf[recvd] = '\0';
                printf("%s", buf);
            }
        }

        if(fds[1].revents & POLLIN){
 
            if(fds[1].revents & POLLIN){
               
                if(fgets(buf, sizeof(buf), stdin) == NULL){
                    printf("client: EOF on stdin, exiting\n");
                    break;
                }

                if (strchr(buf, '\n') == NULL) {
                    printf("Message too long — max %d characters. Please try again.\n", MAXDATASIZE - 1);
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF);
                    continue;
                }

                trim_newline(buf);
                if (send_line(sockfd, buf) == -1){
                    perror("send");
                    break;
                }        
            }
        }
    }
    close(sockfd);
    return 0;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void trim_newline(char *str) {
    str[strcspn(str, "\r\n")] = '\0';
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

int send_line(int sockfd, const char *line){
    int len = strlen(line);
    return sendall(sockfd, line, &len);
}