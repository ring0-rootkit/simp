#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define AF_MYPROTO 5

int main()
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
    // addr.sin_family = AF_MYPROTO;
    // addr.sin_port = htons(42069);
    // addr.sin_addr.s_addr = INADDR_ANY;
    //
    // if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
    //     perror("bind");
    //     close(sockfd);
    //     exit(EXIT_FAILURE);
    // }

    printf("Socket created and bound successfully\n");

    // Close the socket
    close(sockfd);
    return 0;
}
