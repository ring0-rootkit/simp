#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 6000

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t len;
    char buffer[1024];
    int count = 0;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (count < 1000) {
        len = sizeof(client_addr);
        int n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                         (struct sockaddr *)&client_addr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            count++;
        }
    }

    printf("Received %d packets\n", count);
    close(sockfd);
    return 0;
}
