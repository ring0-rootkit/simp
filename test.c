#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AF_MYPROTO 5

int _bind(int port) 
{
    int sockfd;
    struct sockaddr_in addr;

    // Create a socket
    sockfd = socket(AF_MYPROTO, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Bind the socket
    addr.sin_family = AF_MYPROTO;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Socket created and bound successfully\n");
    return sockfd;
}

int main()
{

    int s1 = _bind(42069);
    int s2 = _bind(42069);

    close(s1);
    close(s2);

    int s3 = _bind(42069);
    close(s3);

    return 0;
}
