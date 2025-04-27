#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUFFER_SIZE 65536

volatile unsigned long data_sent = 0;
volatile unsigned long data_dropped = 0;
volatile unsigned long ctrl_sent = 0;
volatile unsigned long ctrl_dropped = 0;

double drop_rate = 0.0;
int raw_recv_fd = -1;
int raw_send_fd = -1;
char server_ip[INET_ADDRSTRLEN];
int port_a = 0, port_b = 0;

// Pseudo-header for TCP checksum
struct pseudo_header {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t zero;
    uint8_t protocol;
    uint16_t tcp_length;
};

// Generic checksum
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if (len == 1) sum += *((unsigned char*)buf);
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

// IP checksum
unsigned short ip_checksum(struct iphdr *ip) {
    return checksum((unsigned short*)ip, ip->ihl * 4);
}

// TCP checksum
unsigned short tcp_checksum(struct iphdr *ip, struct tcphdr *tcp, uint8_t *payload, int payload_len) {
    struct pseudo_header ph;
    ph.src_addr   = ip->saddr;
    ph.dst_addr   = ip->daddr;
    ph.zero       = 0;
    ph.protocol   = IPPROTO_TCP;
    ph.tcp_length = htons(ntohs(ip->tot_len) - ip->ihl * 4);

    int total_len = sizeof(ph) + ntohs(ph.tcp_length);
    uint8_t *buf = malloc(total_len);
    memcpy(buf, &ph, sizeof(ph));
    memcpy(buf + sizeof(ph), tcp, ntohs(ph.tcp_length));

    unsigned short result = checksum(buf, total_len);
    free(buf);
    return result;
}

void handle_sigint(int signo) {
    printf("\n=== Proxy Statistics ===\n");
    printf("Data Packets Sent:    %lu\n", data_sent);
    printf("Data Packets Dropped: %lu\n", data_dropped);
    printf("Control Packets Sent:    %lu\n", ctrl_sent);
    printf("Control Packets Dropped: %lu\n", ctrl_dropped);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <listen_port> <server_ip> <server_port> <drop_rate>\n", argv[0]);
        return EXIT_FAILURE;
    }
    port_a = atoi(argv[1]);
    strncpy(server_ip, argv[2], sizeof(server_ip)-1);
    server_ip[sizeof(server_ip)-1] = '\0';
    port_b = atoi(argv[3]);
    drop_rate = atof(argv[4]);
    if (drop_rate < 0.0 || drop_rate > 1.0) {
        fprintf(stderr, "Invalid drop rate: %f\n", drop_rate);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);
    srand(time(NULL));

    // Raw sockets
    raw_recv_fd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (raw_recv_fd < 0) { perror("socket raw recv"); return EXIT_FAILURE; }

    raw_send_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (raw_send_fd < 0) { perror("socket raw send"); return EXIT_FAILURE; }
    int on = 1;
    if (setsockopt(raw_send_fd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0) {
        perror("setsockopt IP_HDRINCL");
        return EXIT_FAILURE;
    }

    printf("Proxy %d -> %s:%d and back, drop rate %.2f\n",
           port_a, server_ip, port_b, drop_rate);

    uint8_t buffer[BUFFER_SIZE];
    struct sockaddr_in recv_addr;
    socklen_t addr_len = sizeof(recv_addr);

    while (1) {
        ssize_t len = recvfrom(raw_recv_fd, buffer, BUFFER_SIZE, 0,
                               (struct sockaddr*)&recv_addr, &addr_len);
        if (len < 0) { perror("recvfrom"); continue; }

        struct iphdr *ip = (struct iphdr*)buffer;
        if (ip->protocol != IPPROTO_TCP) continue;

        int ip_hlen = ip->ihl * 4;
        struct tcphdr *tcp = (struct tcphdr*)(buffer + ip_hlen);
        int tcp_hlen = tcp->doff * 4;
        int payload_len = ntohs(ip->tot_len) - ip_hlen - tcp_hlen;

        int dst_port = ntohs(tcp->dest);
        int src_port = ntohs(tcp->source);
        uint32_t src_ip = ip->saddr;

        int is_control = (payload_len == 0);

        // Decide direction and new destination
        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;

        if (dst_port == port_a) {
            // client -> server
            dst.sin_addr.s_addr = inet_addr(server_ip);
            dst.sin_port = htons(port_b);
        } else if (src_port == port_b && src_ip == inet_addr(server_ip)) {
            // server -> client
            dst.sin_addr.s_addr = ip->daddr;  // original client's IP
            dst.sin_port = htons(port_a);
        } else continue;

        // Drop simulation
        if (((double)rand() / RAND_MAX) < drop_rate) {
            if (is_control) ctrl_dropped++;
            else data_dropped++;
            continue;
        }

        // Rewrite headers
        // IP src stays as original sender
        ip->daddr = dst.sin_addr.s_addr;
        ip->ttl = 64;
        ip->check = 0;
        ip->check = ip_checksum(ip);

        // TCP port rewrite
        if (dst_port == port_a) tcp->dest = htons(port_b);
        else tcp->source = htons(port_b), tcp->dest = htons(port_a);
        tcp->check = 0;
        tcp->check = tcp_checksum(ip, tcp, buffer + ip_hlen + tcp_hlen, payload_len);

        // Send
        if (sendto(raw_send_fd, buffer, ntohs(ip->tot_len), 0,
                   (struct sockaddr*)&dst, sizeof(dst)) < 0) {
            perror("sendto");
            continue;
        }

        if (is_control) ctrl_sent++;
        else data_sent++;
    }

    close(raw_recv_fd);
    close(raw_send_fd);
    return 0;
}
