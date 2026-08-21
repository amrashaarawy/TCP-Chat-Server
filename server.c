#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "9034"
#define BACKLOG 10

int main(void) {
    struct addrinfo hints, *ai, *p;
    int rv;
    int listenfd;
    int yes;

    //config settings
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

    // TODO: poll() loop goes here — Step 2

    close(listenfd);
    return 0;
}