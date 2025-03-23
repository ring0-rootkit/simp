#ifndef SIMP_H
#define SIMP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define SIMP_VERSION 1
#define MAX_PACKET_SIZE 1500
#define HEADER_SIZE 8
#define SEND_BUFFER_SIZE 128
#define RCVD_BUFFER_SIZE 128
#define KEEP_ALIVE_INTERVAL 1
#define KEEP_ALIVE_TIMEOUT 5
#define MAX_MISSING_IDS 64

typedef enum {
    PRIO_HIGH,
    PRIO_MEDIUM,
    PRIO_LOW
} packet_priority_t;

#define FLAG_KEEP_ALIVE_REQ  0x01
#define FLAG_KEEP_ALIVE_RESP 0x02
#define FLAG_NACK            0x04

#pragma pack(push, 1)
typedef struct {
    uint8_t version : 4;
    uint8_t reserved : 4;
    uint8_t rsnd_bitmap;
    uint8_t group_id;
    uint16_t seq_id;
    uint8_t flags;
    uint16_t data_len;
} packet_header_t;
#pragma pack(pop)

typedef struct {
    uint16_t seq_id;
    uint8_t group_id;
    packet_priority_t priority;
    uint8_t data[MAX_PACKET_SIZE - HEADER_SIZE];
    size_t data_len;
} buffered_packet_t;

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    pthread_t keep_alive_thread;
    pthread_t reader_thread;
    int connection_active;
    uint16_t next_seq_id;
    buffered_packet_t send_buffer[SEND_BUFFER_SIZE];
    buffered_packet_t rcvd_buffer[RCVD_BUFFER_SIZE];
    uint8_t send_cntr;
    uint8_t rcvd_cntr;
    uint8_t pack_info;
    pthread_mutex_t send_buffer_mutex;
    pthread_mutex_t rcvd_buffer_mutex;
    pthread_mutex_t pack_info_mutex;
    pthread_mutex_t seq_mutex;
    uint16_t last_rcvd_id;
    uint16_t not_received_ids[SEND_BUFFER_SIZE];
} simp_context_t;

static inline void simp_serialize_header(const packet_header_t* header, uint8_t* buffer) {
    uint16_t net_seq = htons(header->seq_id);
    uint16_t net_len = htons(header->data_len);
    memcpy(buffer, header, 4);  // version, rsnd_bitmap, group_id
    memcpy(buffer + 4, &net_seq, 2);
    buffer[6] = header->flags;
    memcpy(buffer + 7, &net_len, 2);
}

static inline void simp_deserialize_header(const uint8_t* buffer, packet_header_t* header) {
    memcpy(header, buffer, 4);
    memcpy(&header->seq_id, buffer + 4, 2);
    header->seq_id = ntohs(header->seq_id);
    header->flags = buffer[6];
    memcpy(&header->data_len, buffer + 7, 2);
    header->data_len = ntohs(header->data_len);
}

static void buffer_add_packet(simp_context_t* ctx, const packet_header_t* header, 
                             const uint8_t* data, packet_priority_t prio) {
    if (prio == PRIO_LOW) return;

    pthread_mutex_lock(&ctx->send_buffer_mutex);
    
    // For medium priority: clear old group packets
    if (prio == PRIO_MEDIUM) {
        for (int i = 0; i < SEND_BUFFER_SIZE; i++) {
            if (ctx->send_buffer[i].group_id == header->group_id && 
                ctx->send_buffer[i].priority == PRIO_MEDIUM) {
                memset(&ctx->send_buffer[i], 0, sizeof(buffered_packet_t));
            }
        }
    }

    int idx = header->seq_id % SEND_BUFFER_SIZE;
    ctx->send_buffer[idx].seq_id = header->seq_id;
    ctx->send_buffer[idx].group_id = header->group_id;
    ctx->send_buffer[idx].priority = prio;
    ctx->send_buffer[idx].data_len = header->data_len;
    memcpy(ctx->send_buffer[idx].data, data, header->data_len);

    ctx->send_cntr++;

    pthread_mutex_unlock(&ctx->send_buffer_mutex);
}

static void buffer_cleanup(simp_context_t* ctx, uint16_t last_received_id, 
                          const uint16_t* missing_ids, size_t missing_count) {
    pthread_mutex_lock(&ctx->send_buffer_mutex);
    
    for (int i = 0; i < SEND_BUFFER_SIZE; i++) {
        buffered_packet_t* pkt = &ctx->send_buffer[i];
        if (pkt->seq_id == 0) continue;

        if (pkt->seq_id <= last_received_id) {
            bool missing = false;
            for (size_t j = 0; j < missing_count; j++) {
                if (pkt->seq_id == ntohs(missing_ids[j])) {
                    missing = true;
                    break;
                }
            }

            if (missing) {
                if (pkt->priority == PRIO_HIGH || pkt->priority == PRIO_MEDIUM) {
                    sendto(ctx->sockfd, pkt->data, pkt->data_len + HEADER_SIZE, 0,
                          (struct sockaddr*)&ctx->addr, sizeof(ctx->addr));
                }
            } else {
                ctx->send_cntr--;
                memset(pkt, 0, sizeof(buffered_packet_t));
            }
        }
    }

    pthread_mutex_unlock(&ctx->send_buffer_mutex);
}

static int simp_init(simp_context_t* ctx, const char* ip, uint16_t port) {
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) { 
        perror("init");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(ctx->addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ctx->addr.sin_addr);
    int addr_len = sizeof(addr);

    int err = bind(ctx->sockfd, (struct sockaddr*)&addr, addr_len);
    if (err) {
        perror("bind");
        return err;
    }

    ctx->next_seq_id = 1;
    pthread_mutex_init(&ctx->send_buffer_mutex, NULL);
    pthread_mutex_init(&ctx->rcvd_buffer_mutex, NULL);
    pthread_mutex_init(&ctx->pack_info_mutex, NULL);
    pthread_mutex_init(&ctx->seq_mutex, NULL);
    memset(ctx->send_buffer, 0, sizeof(ctx->send_buffer));

    return 0;
}

static void simp_cleanup(simp_context_t* ctx) {
    ctx->connection_active = 0;
    close(ctx->sockfd);
    pthread_mutex_destroy(&ctx->send_buffer_mutex);
    pthread_mutex_destroy(&ctx->rcvd_buffer_mutex);
    pthread_mutex_destroy(&ctx->pack_info_mutex);
    pthread_mutex_destroy(&ctx->seq_mutex);
}

static void* simp_reader_handler(void* args) {
    simp_context_t *ctx = (simp_context_t*)args;
    uint8_t buffer[MAX_PACKET_SIZE];

    while (ctx->connection_active) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        ssize_t len = recv(ctx->sockfd, buffer, MAX_PACKET_SIZE, 0);

        if (len < HEADER_SIZE) continue;

        packet_header_t header;
        simp_deserialize_header(buffer, &header);
        void* data = buffer + HEADER_SIZE;

        ctx->last_rcvd_id = header.seq_id;

        // TODO: update nacked

        if (header.flags & FLAG_KEEP_ALIVE_RESP) {
            printf("recvd KA message");
            //TODO: keep alive response + send_buf cleanup
        } else {
            // TODO: put to recvd table
        }
    }
    return NULL;
}


static void* keep_alive_handler(void* arg) {
    simp_context_t* ctx = (simp_context_t*)arg;
    uint8_t buffer[HEADER_SIZE];
    packet_header_t keep_alive = {
        .version = SIMP_VERSION,
        .flags = FLAG_KEEP_ALIVE_REQ,
        .data_len = 0
    };

    //TODO: send last acked packet and all nacked packets
    while (ctx->connection_active) {
        simp_serialize_header(&keep_alive, buffer);
        sendto(ctx->sockfd, buffer, HEADER_SIZE, 0,
              (struct sockaddr*)&ctx->addr, sizeof(ctx->addr));
        printf("sending KA message");

        sleep(KEEP_ALIVE_INTERVAL);
    }
    return NULL;
}

static int simp_start(simp_context_t *ctx) {
    ctx->connection_active = 1;
    int err = pthread_create(&ctx->keep_alive_thread, NULL, keep_alive_handler, ctx);
    if (err) {
        return err;
    }
    err = pthread_create(&ctx->reader_thread, NULL, simp_reader_handler, ctx);
    if (err) {
        return err;
    }
    return 0;
}


static int simp_connect(simp_context_t* ctx, const char* ip, uint16_t port) {
    memset(&ctx->addr, 0, sizeof(ctx->addr));
    ctx->addr.sin_family = AF_INET;
    ctx->addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ctx->addr.sin_addr);

    ctx->connection_active = 1;
    int err = pthread_create(&ctx->reader_thread, NULL, simp_reader_handler, ctx);
    if (err) {
        return err;
    }
    printf("reader");

    return pthread_create(&ctx->keep_alive_thread, NULL, keep_alive_handler, ctx);
}

static int simp_send(simp_context_t* ctx, const uint8_t* data, size_t len,
                        packet_priority_t prio, uint8_t group_id) {
    packet_header_t header = {
        .version = SIMP_VERSION,
        .group_id = group_id,
        .data_len = (uint16_t)len
    };

    pthread_mutex_lock(&ctx->seq_mutex);
    header.seq_id = ctx->next_seq_id++;
    pthread_mutex_unlock(&ctx->seq_mutex);

    uint8_t buffer[MAX_PACKET_SIZE];
    simp_serialize_header(&header, buffer);
    memcpy(buffer + HEADER_SIZE, data, len);

    // Add to buffer before sending
    buffer_add_packet(ctx, &header, data, prio);

    return sendto(ctx->sockfd, buffer, HEADER_SIZE + len, 0,
                 (struct sockaddr*)&ctx->addr, sizeof(ctx->addr));
}


static int simp_receive(simp_context_t* ctx, char* buf, int buf_len) {
    // read from rcv queue
    return 0;
}

#endif // SIMP_H
