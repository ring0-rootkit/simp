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
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define ERR(desc) do { \
    fprintf(stderr, "Error at %s:%d: ", __FILE__, __LINE__); \
    perror(desc); \
} while (0)

#define SIMP_VERSION 1
#define MAX_PACKET_SIZE 1500
#define SEND_BUFFER_SIZE 128
#define RCVD_BUFFER_SIZE 128
#define KEEP_ALIVE_INTERVAL 1
#define KEEP_ALIVE_TIMEOUT 5
#define MAX_NACKS_BEFORE_DROP 3
#define MAX_KEEP_ALIVE_MISSES 5

#define ERR_CLOSED 7

// on update update size of pack_info in simp_context_t
#define MAX_MISSING_IDS 64 

#define MAX_QUEUE_SIZE 1024
#define SIMP_SHM_NAME "/simp_context"
#define RCV_TIMEOUT 1

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
    uint8_t group_id;
    uint16_t seq_id;
    uint16_t ack_id;
    uint8_t flags;
    uint16_t data_len;
} packet_header_t;
#pragma pack(pop)
#define HEADER_SIZE sizeof(packet_header_t)

typedef struct {
    uint8_t data[MAX_PACKET_SIZE];
    packet_header_t header;
    packet_priority_t priority;
} message_t;

typedef struct {
    message_t messages[MAX_QUEUE_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} message_queue_t;

typedef struct {
    int sockfd;
    struct sockaddr_in addr;
    pthread_t keep_alive_thread;
    pthread_t reader_thread;
    pthread_t nack_handler_thread;
    pthread_t sender_thread;
    atomic_int connection_active;
    atomic_int remote_peer_active;

    pthread_mutex_t console_write_mutex;
    
    pthread_mutex_t sock_write_mutex;
    pthread_mutex_t addr_mutex;
    pthread_mutex_t pack_info_mutex;
    pthread_mutex_t seq_mutex;
    pthread_mutex_t read_mutex;           // also used for reader_closed_cond
    pthread_mutex_t ka_mutex;             // keep alive
    pthread_mutex_t sender_closed_mutex;  // for sender_closed_cond
    pthread_mutex_t last_remotely_acked_mutex;
    pthread_mutex_t nacks_sent_mutex;

    pthread_cond_t keep_alive_closed_cond;
    pthread_cond_t reader_closed_cond;
    pthread_cond_t sender_closed_cond;
    
    atomic_uint_least16_t next_seq_id;
    atomic_uint_least16_t last_rcvd_id;
    atomic_uint_least16_t last_acked_id;
    atomic_uint_least8_t send_cntr;
    atomic_uint_least8_t rcvd_cntr;
    atomic_uint_least64_t pack_info;
    atomic_uint_least16_t next_avail_packet;
    atomic_uint_least8_t keep_alive_missed;
    int last_remotely_acked_id; // users last_remotely_acked_mutex

    atomic_int should_send_ka_packet;
    atomic_int should_send_ka_resp;
    atomic_uint_least8_t missed_ka_num; // number of missed keep alive packets
    char nacks_sent[64];
    
    message_queue_t reader_queue;
    message_queue_t nack_queue;
    message_queue_t user_queue;
    message_queue_t send_queue;
    message_queue_t pending_queue;
    
    // shared memory stuff
    char shm_name[64];
    int shm_fd;
    bool is_shm_owner;
} simp_context_t;

static inline void simp_serialize_header(const packet_header_t* header, uint8_t* buffer) {
    uint16_t net_seq = htons(header->seq_id);
    uint16_t net_ack = htons(header->ack_id);
    uint16_t net_len = htons(header->data_len);
    memcpy(buffer, header, 2);
    memcpy(buffer + 2, &net_seq, 2);
    memcpy(buffer + 4, &net_ack, 2);
    buffer[6] = header->flags;
    memcpy(buffer + 7, &net_len, 2);
}

static inline void simp_deserialize_header(const uint8_t* buffer, packet_header_t* header) {
    memcpy(header, buffer, 2);
    memcpy(&header->seq_id, buffer + 2, 2);
    memcpy(&header->ack_id, buffer + 4, 2);
    header->seq_id = ntohs(header->seq_id);
    header->ack_id = ntohs(header->ack_id);
    header->flags = buffer[6];
    memcpy(&header->data_len, buffer + 7, 2);
    header->data_len = ntohs(header->data_len);
}


static void queue_init(message_queue_t* queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&queue->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&queue->not_empty, &cond_attr);
    pthread_cond_init(&queue->not_full, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
}

static inline void simp_display_prep_line() {
    printf("                                       │\r");
    printf("│ ");
}

#define RIGHT_PAD_FORMATTED(msg, format) do {   \
        simp_display_prep_line();               \
        printf(msg, format);                    \
        printf("\n");                           \
    } while(0)
static inline void simp_display_packet(simp_context_t *ctx, packet_header_t* header, uint8_t* data) {
    pthread_mutex_lock(&ctx->console_write_mutex);
    printf("\n┌──────────────────────────────────────┐\n");
    printf("│ Context Information                  │\n");
    RIGHT_PAD_FORMATTED("Last rcvd: %d", header->version);
    printf("│ Last 32 packet bitmap:               │\n");
    printf("│ ");
    for (int i = sizeof(ctx->pack_info) * 8 / 2 - 1; i >= 0; i--) {
        printf("%lu", (ctx->pack_info >> i) & 1);
    }
    printf("     │\n");
    printf("├──────────────────────────────────────┤\n");
    printf("│ Packet Information                   │\n");
    printf("├──────────────────────────────────────┤\n");
    RIGHT_PAD_FORMATTED("Version: %d", header->version);
    RIGHT_PAD_FORMATTED("Group ID: %d", header->group_id);
    RIGHT_PAD_FORMATTED("Sequence ID: %d", header->seq_id);
    RIGHT_PAD_FORMATTED("Acknowledgment ID: %d", header->ack_id);
    printf("│ Flags:                               │\n");
    RIGHT_PAD_FORMATTED("  Keep Alive Request: %s", 
           (header->flags & FLAG_KEEP_ALIVE_REQ) ? "Yes" : "No ");
    RIGHT_PAD_FORMATTED("  Keep Alive Response: %s", 
           (header->flags & FLAG_KEEP_ALIVE_RESP) ? "Yes" : "No ");
    RIGHT_PAD_FORMATTED("  NACK: %s", 
           (header->flags & FLAG_NACK) ? "Yes" : "No ");
    RIGHT_PAD_FORMATTED("Data Length: %d bytes", header->data_len);
    printf("├──────────────────────────────────────┤\n");
    
    if (header->data_len > 0) {
        printf("│ Data:                                │\n");
        simp_display_prep_line();
        for (int i = 0; i < header->data_len; i++) {
            printf("%02X ", data[i]);
            if ((i + 1) % 8 == 0) {
                printf("\n");
                simp_display_prep_line();
            }
        }
        if (header->data_len % 8 != 0) {
            printf("\n");
        }
        printf("\r└──────────────────────────────────────┘\n");
    } else {
        printf("│ No data payload                      │\n");
        printf("└──────────────────────────────────────┘\n");
    }
    pthread_mutex_unlock(&ctx->console_write_mutex);
}
#undef RIGHT_PAD_FORMATTED

static simp_context_t* simp_create_shared_context(const char* name) {
    char shm_name[64];
    if (name == NULL) {
        snprintf(shm_name, sizeof(shm_name), "%s_%d", SIMP_SHM_NAME, (int)getpid());
    } else {
        snprintf(shm_name, sizeof(shm_name), "%s", name);
    }
    
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        ERR("shm_open");
        return NULL;
    }
    
    if (ftruncate(shm_fd, sizeof(simp_context_t)) == -1) {
        ERR("ftruncate");
        close(shm_fd);
        shm_unlink(shm_name);
        return NULL;
    }
    
    simp_context_t* ctx = (simp_context_t*)mmap(NULL, sizeof(simp_context_t), 
                                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ctx == MAP_FAILED) {
        ERR("mmap");
        close(shm_fd);
        shm_unlink(shm_name);
        return NULL;
    }
    
    memset(ctx, 0, sizeof(simp_context_t));
    
    strncpy(ctx->shm_name, shm_name, sizeof(ctx->shm_name) - 1);
    ctx->shm_fd = shm_fd;
    ctx->is_shm_owner = true;
    
    atomic_init(&ctx->next_seq_id, 1);
    atomic_init(&ctx->send_cntr, 0);
    atomic_init(&ctx->rcvd_cntr, 0);
    atomic_init(&ctx->last_rcvd_id, 0);
    atomic_init(&ctx->next_avail_packet, 1);
    atomic_init(&ctx->keep_alive_missed, 0);
    atomic_init(&ctx->last_acked_id, 0);
    atomic_init(&ctx->pack_info, 0xFFFFFFFF);
    atomic_init(&ctx->connection_active, 0);
    atomic_init(&ctx->remote_peer_active, 0);
    atomic_init(&ctx->should_send_ka_packet, 0);
    atomic_init(&ctx->should_send_ka_resp, 0);
    
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_mutex_init(&ctx->console_write_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->sock_write_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->addr_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->pack_info_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->seq_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->read_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->ka_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->sender_closed_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->last_remotely_acked_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->nacks_sent_mutex, &mutex_attr);
    
    pthread_mutexattr_destroy(&mutex_attr);
    
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);

    pthread_cond_init(&ctx->keep_alive_closed_cond, &cond_attr);
    pthread_cond_init(&ctx->reader_closed_cond, &cond_attr);
    pthread_cond_init(&ctx->sender_closed_cond, &cond_attr);
    
    pthread_condattr_destroy(&cond_attr);
    
    queue_init(&ctx->reader_queue);
    queue_init(&ctx->nack_queue);
    queue_init(&ctx->user_queue);
    queue_init(&ctx->send_queue);
    queue_init(&ctx->pending_queue);
    
    return ctx;
}

static void simp_detach_shared_context(simp_context_t* ctx) {
    if (ctx == NULL) return;
    
    // Store local copies of the info we need for cleanup
    // since we're about to unmap the memory
    int shm_fd = ctx->shm_fd;
    bool is_owner = ctx->is_shm_owner;
    char shm_name[64];
    strncpy(shm_name, ctx->shm_name, sizeof(shm_name));
    
    fprintf(stdout, "Closing at %d: \n", __LINE__);
    // Now unmap the shared memory
    if (munmap(ctx, sizeof(simp_context_t)) == -1) {
        ERR("munmap");
    }
    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    // Close the file descriptor
    if (close(shm_fd) == -1) {
        ERR("close (shm_fd)");
    }
    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    // If this process is the owner, also unlink the shared memory
    if (is_owner) {
        if (shm_unlink(shm_name) == -1) {
            ERR("shm_unlink");
        }
    }
}

static void queue_cleanup(message_queue_t* queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
}

static void queue_push(message_queue_t* queue, const message_t* msg) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count >= MAX_QUEUE_SIZE) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    memcpy(&queue->messages[queue->tail], msg, sizeof(message_t));
    queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;
    queue->count++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

static int queue_pop(message_queue_t* queue, message_t* msg) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == -1) { // count set to -1 only in closing state
        goto exit_error;
    }
    
    memcpy(msg, &queue->messages[queue->head], sizeof(message_t));
    queue->head = (queue->head + 1) % MAX_QUEUE_SIZE;
    queue->count--;

    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 0;

exit_error:
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return -1;
}

// TODO: implement
static void clear_group_from_queue(message_queue_t *queue, uint16_t group_id) {
}

static void* sender_handler(void* arg) {
    simp_context_t* ctx = (simp_context_t*)arg;
    message_t msg;
    uint8_t buffer[MAX_PACKET_SIZE];
    packet_header_t header;
    struct sockaddr_in local_addr;

    while (atomic_load(&ctx->connection_active)) {
        if (queue_pop(&ctx->send_queue, &msg) == 0) {
            msg.header.version = SIMP_VERSION;
            msg.header.seq_id = atomic_fetch_add(&ctx->next_seq_id, 1);
            msg.header.ack_id = atomic_load(&ctx->last_acked_id);

            if (msg.priority == PRIO_HIGH) {
                queue_push(&ctx->pending_queue, &msg);
            }

            if (msg.header.seq_id == 2) {
                continue;
            }

            header = msg.header;

            if (atomic_load(&ctx->should_send_ka_packet)) {
                atomic_fetch_add(&ctx->keep_alive_missed, 1);
                atomic_store(&ctx->should_send_ka_packet, 0);
                header.flags |= FLAG_KEEP_ALIVE_REQ;
            }
            if (atomic_load(&ctx->should_send_ka_resp)) {
                atomic_store(&ctx->should_send_ka_resp, 0);
                header.flags |= FLAG_KEEP_ALIVE_RESP;
            }
            
            simp_serialize_header(&header, buffer);
            memcpy(buffer + HEADER_SIZE, msg.data, msg.header.data_len);
            
            pthread_mutex_lock(&ctx->addr_mutex);
            local_addr = ctx->addr;
            pthread_mutex_unlock(&ctx->addr_mutex);

            printf("---------------------------------\n");
            printf("SND:\n");
            simp_display_packet(ctx, &header, msg.data);
            printf("---------------------------------\n");

            pthread_mutex_lock(&ctx->sock_write_mutex);
            int err = sendto(ctx->sockfd, buffer, HEADER_SIZE + msg.header.data_len, 0,
                           (struct sockaddr*)&local_addr, sizeof(local_addr));
            pthread_mutex_unlock(&ctx->sock_write_mutex);

            if (err < 0) {
                ERR("sendto");
            }
        }
        usleep(1000);
    }

    pthread_mutex_lock(&ctx->sender_closed_mutex);
    pthread_cond_broadcast(&ctx->sender_closed_cond);
    pthread_mutex_unlock(&ctx->sender_closed_mutex);
    return NULL;
}

static void simp_send_nacks(simp_context_t *ctx) {
    message_t keep_alive_msg = {
        .header = {
            .version = SIMP_VERSION,
            .flags = FLAG_NACK,
        },
        .priority = PRIO_HIGH,
    };
    
    uint16_t buf[MAX_MISSING_IDS*sizeof(ctx->next_seq_id)];
    int buf_idx = 0;
    pthread_mutex_lock(&ctx->pack_info_mutex);
    uint32_t pack_info = atomic_load(&ctx->pack_info);
    uint16_t cur_id = atomic_load(&ctx->last_rcvd_id);
    uint16_t last_acked = atomic_load(&ctx->last_acked_id);
    int i;
    printf("last acked: %d cur_id: %d\n", last_acked, cur_id);

    pthread_mutex_lock(&ctx->nacks_sent_mutex);
    for(i = 0; i < MAX_MISSING_IDS && cur_id > last_acked; i++) {
        printf("checking %d packet\n", i);
        if(!(pack_info & (1<<i))) {
            printf("found nack\n");

            int next = atomic_load(&ctx->next_avail_packet);
            if (ctx->nacks_sent[next & 0x7F] >= MAX_NACKS_BEFORE_DROP) {
                ctx->nacks_sent[next & 0x7F] = 0;
                atomic_store(&ctx->next_avail_packet, next+1);
                pack_info = pack_info | (1<<i);
            }

            ctx->nacks_sent[cur_id&0x7F]++;
            if (ctx->nacks_sent[cur_id&0x7F] < MAX_NACKS_BEFORE_DROP) {
                buf[buf_idx] = htons(cur_id); 
                buf_idx++;
            }
        }
        cur_id--;
    }
    atomic_store(&ctx->pack_info, pack_info);
    pthread_mutex_unlock(&ctx->pack_info_mutex);
    pthread_mutex_unlock(&ctx->nacks_sent_mutex);

    if (buf_idx == 0) {
        return;
    }

    keep_alive_msg.header.data_len = buf_idx*2; // 2 byte per packet_id
    memcpy(keep_alive_msg.data, buf, keep_alive_msg.header.data_len);
    queue_push(&ctx->send_queue, &keep_alive_msg);
}

static void simp_update_last_acked(simp_context_t *ctx) {
    pthread_mutex_lock(&ctx->pack_info_mutex);
    int last_acked = atomic_load(&ctx->last_acked_id);
    int not_acked_num = atomic_load(&ctx->last_rcvd_id) - last_acked;
    int pack_info = atomic_load(&ctx->pack_info);
    int mask = 1 << not_acked_num;
    while(mask) {
        if (!(pack_info & mask)) {
            break;
        }
        last_acked++;
        mask >>= 1;
    }
    atomic_store(&ctx->last_acked_id, last_acked-1);
    pthread_mutex_unlock(&ctx->pack_info_mutex);
}

static int simp_remove_pending_acked_packets(simp_context_t *ctx) {
    pthread_mutex_lock(&ctx->pending_queue.mutex);
    int count = ctx->pending_queue.count;
    pthread_mutex_unlock(&ctx->pending_queue.mutex);
    pthread_mutex_lock(&ctx->last_remotely_acked_mutex);
    int last_acked = ctx->last_remotely_acked_id;
    pthread_mutex_unlock(&ctx->last_remotely_acked_mutex);
    message_t msg;
    for (int i = 0; i < count; i++) {
        if (queue_pop(&ctx->pending_queue, &msg)) {
            ERR("queue pop");
            return -1;
        }
        if (msg.header.seq_id > last_acked) {
            queue_push(&ctx->pending_queue, &msg); 
        }
    }
    return 0;
}

/* 
 * returns: non 0 if packet should be dropped 
 * */
static int simp_update_pack_info(simp_context_t *ctx, packet_header_t *header) {
    int should_drop = 1;

    pthread_mutex_lock(&ctx->pack_info_mutex);
    int packet_diff = header->seq_id - ctx->last_rcvd_id;
    int pack_info = atomic_load(&ctx->pack_info);

    if (packet_diff < 0) {
        pack_info |= (1 << -packet_diff);
        should_drop = 0;
        goto done;
    } 

    // ensure that pack_info has 
    // 'packet_diff' number of 1 in the leftmost position
    int pack_diff_bitmap = (0xFFFFFFFF << (MAX_MISSING_IDS - packet_diff));
    if ((pack_info & pack_diff_bitmap) == pack_diff_bitmap) {
        pack_info = pack_info << packet_diff;
        pack_info |= 1;
        atomic_store(&ctx->last_rcvd_id, header->seq_id);
        should_drop = 0;
        goto done;
    }

done:

    atomic_store(&ctx->pack_info, pack_info);
    pthread_mutex_unlock(&ctx->pack_info_mutex);

    return should_drop;
}

static void* simp_reader_handler(void* args) {
    simp_context_t *ctx = (simp_context_t*)args;
    uint8_t buffer[MAX_PACKET_SIZE];
    message_t msg;
    int err;

    packet_header_t keep_alive_resp = {
        .version = SIMP_VERSION,
        .flags = FLAG_KEEP_ALIVE_RESP,
        .data_len = 0
    };

    while (atomic_load(&ctx->connection_active)) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        ssize_t len = recvfrom(ctx->sockfd, buffer, MAX_PACKET_SIZE, 0, 
                              (struct sockaddr*)&addr, &addr_len);
        if (!atomic_load(&ctx->connection_active)) {
            break;
        }
        if (len < 0) {
            ERR("recvfrom");
            continue;
        }

        if (len < HEADER_SIZE) continue;

        pthread_mutex_lock(&ctx->addr_mutex);
        if (!ctx->addr.sin_family) {
            ctx->addr = addr;
        }
        pthread_mutex_unlock(&ctx->addr_mutex);

        packet_header_t header;
        simp_deserialize_header(buffer, &header);
        void* data = buffer + HEADER_SIZE;

        if (header.flags & FLAG_KEEP_ALIVE_RESP) {
            atomic_store(&ctx->keep_alive_missed, 0);
        }


        err = simp_update_pack_info(ctx, &header);
        if (err) {
            //drop the packet
            printf("dropped packet with seq_id: %d\n", header.seq_id);
            continue;
        }

        int last_rcvd_id = atomic_load(&ctx->last_rcvd_id);
        if (header.seq_id > last_rcvd_id) {
            atomic_store(&ctx->last_rcvd_id, header.seq_id);
        }

        simp_update_last_acked(ctx);
        simp_send_nacks(ctx);

        pthread_mutex_lock(&ctx->last_remotely_acked_mutex);
        if (ctx->last_remotely_acked_id < msg.header.ack_id) {
            ctx->last_remotely_acked_id = msg.header.ack_id;
        }
        pthread_mutex_unlock(&ctx->last_remotely_acked_mutex);
        simp_remove_pending_acked_packets(ctx);

        printf("---------------------------------\n");
        printf("RCV:\n");
        simp_display_packet(ctx, &header, data);
        printf("---------------------------------\n");

        if(header.flags & FLAG_KEEP_ALIVE_REQ) {
            atomic_store(&ctx->should_send_ka_resp, 1);
        }

        if (header.flags & FLAG_NACK) {
            printf("FOUND NACK PACKET\n");

            if (header.seq_id < last_rcvd_id) {
                continue;
            }

            uint16_t* missing_ids = (uint16_t*)data;
            size_t missing_count = (header.data_len - HEADER_SIZE) / sizeof(uint16_t);
            
            msg.header = header;

            memcpy(msg.data, data, header.data_len);
            queue_push(&ctx->nack_queue, &msg);
            
            continue;
        }

        if (header.data_len == 0) {
            continue;
        }

        printf("new packet with data: %.*s\n", header.data_len, (char*)data);

        msg.header = header;

        memcpy(msg.data, data, header.data_len);
        queue_push(&ctx->user_queue, &msg);
    }

    pthread_mutex_lock(&ctx->read_mutex);
    pthread_cond_broadcast(&ctx->reader_closed_cond);
    pthread_mutex_unlock(&ctx->read_mutex);
    return NULL;
}

// returns: ERR_CLOSED if connection has already been closed by remote peer
static int simp_recv(simp_context_t* ctx, char* buf, int buf_len) {
    if (!atomic_load(&ctx->remote_peer_active)) {
        return -ERR_CLOSED;
    }
    message_t msg;
    
    while (queue_pop(&ctx->user_queue, &msg) == 0) {
        if (msg.header.seq_id == atomic_load(&ctx->next_avail_packet)) {
            atomic_fetch_add(&ctx->next_avail_packet, 1);
            break;
        } else {
            queue_push(&ctx->user_queue, &msg);
        }
    }

    size_t copy_len = (msg.header.data_len < buf_len) ? msg.header.data_len : buf_len;
    memcpy(buf, msg.data, copy_len);
    return copy_len;
}

static int simp_init(simp_context_t* ctx, const char* ip, uint16_t port) {
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) { 
        ERR("init");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = RCV_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    printf("created new socket, fd: %d\n", ctx->sockfd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    int addr_len = sizeof(addr);

    int err = bind(ctx->sockfd, (struct sockaddr*)&addr, addr_len);
    if (err) {
        ERR("bind");
        return err;
    }

    atomic_init(&ctx->next_seq_id, 1);
    atomic_init(&ctx->send_cntr, 0);
    atomic_init(&ctx->rcvd_cntr, 0);
    atomic_init(&ctx->last_rcvd_id, 0);
    atomic_init(&ctx->next_avail_packet, 1);
    atomic_init(&ctx->keep_alive_missed, 0);
    atomic_init(&ctx->last_acked_id, 0);
    atomic_init(&ctx->pack_info, 0xFFFFFFFF);
    atomic_init(&ctx->connection_active, 0);
    atomic_init(&ctx->remote_peer_active, 0);
    
    pthread_mutex_init(&ctx->console_write_mutex, NULL);
    pthread_mutex_init(&ctx->sock_write_mutex, NULL);
    pthread_mutex_init(&ctx->addr_mutex, NULL);
    pthread_mutex_init(&ctx->pack_info_mutex, NULL);
    pthread_mutex_init(&ctx->seq_mutex, NULL);
    pthread_mutex_init(&ctx->read_mutex, NULL);
    pthread_mutex_init(&ctx->ka_mutex, NULL);
    pthread_mutex_init(&ctx->sender_closed_mutex, NULL);
    pthread_mutex_init(&ctx->last_remotely_acked_mutex, NULL);
    pthread_mutex_init(&ctx->nacks_sent_mutex, NULL);
    
    queue_init(&ctx->reader_queue);
    queue_init(&ctx->nack_queue);
    queue_init(&ctx->user_queue);
    queue_init(&ctx->send_queue);
    queue_init(&ctx->pending_queue);

    return 0;
}

// returns: ERR_CLOSED if connection has already been closed by remote peer
static int simp_send(simp_context_t* ctx, const uint8_t* data, size_t len,
                    packet_priority_t prio, uint8_t group_id) {

    if (!atomic_load(&ctx->remote_peer_active)) {
        return -ERR_CLOSED;
    }
    message_t msg = {
        .header = {
            .group_id = group_id,
            .data_len = (uint16_t)len,
            .flags = 0
        },
        .priority = prio,
    };
    memcpy(msg.data, data, len);

    queue_push(&ctx->send_queue, &msg);
    return len;
}


static void simp_cleanup(simp_context_t* ctx) {

    if (ctx == NULL) {
        return;
    }

    fprintf(stdout, "Closing at %d: \n", __LINE__);
    //
    // int err = simp_send(ctx, (uint8_t *)"CLOSE", 5, PRIO_HIGH, -1); // placeholder for actual close
    // if (err < 0) {
    //     ERR("failed to send CLOSE packet\n");
    // }
    // //TODO: wait for close accept

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    pthread_mutex_lock(&ctx->send_queue.mutex);
    while(ctx->send_queue.count > 0) {
        pthread_cond_wait(&ctx->send_queue.not_full, &ctx->send_queue.mutex);
    }
    pthread_mutex_unlock(&ctx->send_queue.mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    // lock before setting connection state to 0, so nothing will exit before we can know
    pthread_mutex_lock(&ctx->ka_mutex);
    pthread_mutex_lock(&ctx->send_queue.mutex);
    pthread_mutex_lock(&ctx->sender_closed_mutex);
    pthread_mutex_lock(&ctx->read_mutex);
    pthread_mutex_lock(&ctx->nack_queue.mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    atomic_store(&ctx->connection_active, 0);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    pthread_cond_wait(&ctx->keep_alive_closed_cond, &ctx->ka_mutex);
    pthread_mutex_unlock(&ctx->ka_mutex);


    fprintf(stdout, "Closing at %d: \n", __LINE__);

    //make sender leave queue_pop with error by sending not_empty notification with count = -1
    
    ctx->send_queue.count = -1;
    pthread_cond_broadcast(&ctx->send_queue.not_empty);
    pthread_mutex_unlock(&ctx->send_queue.mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    ctx->nack_queue.count = -1;
    pthread_cond_broadcast(&ctx->nack_queue.not_empty);
    pthread_mutex_unlock(&ctx->nack_queue.mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    pthread_cond_wait(&ctx->sender_closed_cond, &ctx->sender_closed_mutex);
    pthread_mutex_unlock(&ctx->sender_closed_mutex);
    
    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    // close sockeet before closing reader so reader can stop reading
    // and can be notified
    close(ctx->sockfd);

    fprintf(stdout, "Closing at %d: \n", __LINE__);

    pthread_cond_wait(&ctx->reader_closed_cond, &ctx->read_mutex);
    pthread_mutex_unlock(&ctx->read_mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    queue_cleanup(&ctx->reader_queue);
    queue_cleanup(&ctx->nack_queue);
    queue_cleanup(&ctx->user_queue);
    queue_cleanup(&ctx->send_queue);
    queue_cleanup(&ctx->pending_queue);

    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    pthread_mutex_destroy(&ctx->console_write_mutex);
    pthread_mutex_destroy(&ctx->sock_write_mutex);
    pthread_mutex_destroy(&ctx->addr_mutex);
    pthread_mutex_destroy(&ctx->pack_info_mutex);
    pthread_mutex_destroy(&ctx->seq_mutex);
    pthread_mutex_destroy(&ctx->read_mutex);
    pthread_mutex_destroy(&ctx->ka_mutex);
    pthread_mutex_destroy(&ctx->sender_closed_mutex);
    pthread_mutex_destroy(&ctx->last_remotely_acked_mutex);
    pthread_mutex_destroy(&ctx->nacks_sent_mutex);

    fprintf(stdout, "Closing at %d: \n", __LINE__);


    pthread_cond_destroy(&ctx->keep_alive_closed_cond);
    pthread_cond_destroy(&ctx->reader_closed_cond);
    pthread_cond_destroy(&ctx->sender_closed_cond);

    fprintf(stdout, "Closing at %d: \n", __LINE__);
    
    simp_detach_shared_context(ctx);

    fprintf(stdout, "Closing at %d: \n", __LINE__);
}

static void* keep_alive_handler(void* arg) {
    printf("started KA job\n");
    simp_context_t* ctx = (simp_context_t*)arg;
    uint8_t buffer[HEADER_SIZE];
    packet_header_t keep_alive = {
        .version = SIMP_VERSION,
        .flags = FLAG_KEEP_ALIVE_REQ,
        .data_len = 0
    };
    struct sockaddr_in local_addr;
    bool addr_valid;

    while (atomic_load(&ctx->connection_active) && atomic_load(&ctx->remote_peer_active)) {
        printf("KA alive\n");

        if(atomic_load(&ctx->keep_alive_missed) == MAX_KEEP_ALIVE_MISSES) {
            atomic_store(&ctx->remote_peer_active, 0);
        }

        pthread_mutex_lock(&ctx->addr_mutex);
        addr_valid = ctx->addr.sin_family != 0;
        if (addr_valid) {
            local_addr = ctx->addr;
        }
        pthread_mutex_unlock(&ctx->addr_mutex);

        pthread_mutex_lock(&ctx->send_queue.mutex);
        atomic_store(&ctx->should_send_ka_packet, (ctx->send_queue.count != 0));
        pthread_mutex_unlock(&ctx->send_queue.mutex);

        printf("before if with addr_valid:%b, and should_send_ka: %b\n", addr_valid, atomic_load(&ctx->should_send_ka_packet));
        if (addr_valid && !atomic_load(&ctx->should_send_ka_packet)) {
            if (atomic_load(&ctx->should_send_ka_resp)) {
                atomic_store(&ctx->should_send_ka_resp, 0);
                keep_alive.flags |= FLAG_KEEP_ALIVE_RESP;
            }

            keep_alive.seq_id = atomic_fetch_add(&ctx->next_seq_id, 1);
            keep_alive.ack_id = atomic_load(&ctx->last_acked_id);

            simp_serialize_header(&keep_alive, buffer);

            printf("---------------------------------\n");
            printf("SND KA:\n");
            simp_display_packet(ctx, &keep_alive, (uint8_t*)"");
            printf("---------------------------------\n");

            atomic_fetch_add(&ctx->keep_alive_missed, 1);

            pthread_mutex_lock(&ctx->sock_write_mutex);
            int err = sendto(ctx->sockfd, buffer, HEADER_SIZE, 0,
                  (struct sockaddr*)&local_addr, sizeof(local_addr));
            pthread_mutex_unlock(&ctx->sock_write_mutex);

            keep_alive.flags &= ~FLAG_KEEP_ALIVE_RESP;

            if (err < 0) {
                ERR("Keep alive send");
            }
        }
        sleep(KEEP_ALIVE_INTERVAL);
    }

    pthread_mutex_lock(&ctx->ka_mutex);
    pthread_cond_broadcast(&ctx->keep_alive_closed_cond);
    pthread_mutex_unlock(&ctx->ka_mutex);
    return NULL;
}

static void* nack_handler(void* arg) {
    simp_context_t* ctx = (simp_context_t*)arg;
    message_t msg;
    uint8_t buffer[MAX_PACKET_SIZE];
    packet_header_t header;
    struct sockaddr_in local_addr;

    while (atomic_load(&ctx->connection_active)) {
        if (queue_pop(&ctx->nack_queue, &msg) == 0) {
            printf("found new NACK, restoring...\n");

            uint16_t* missing_ids = (uint16_t*)msg.data;
            size_t missing_count = msg.header.data_len / sizeof(uint16_t);
            
            pthread_mutex_lock(&ctx->addr_mutex);
            local_addr = ctx->addr;
            pthread_mutex_unlock(&ctx->addr_mutex);
            
            for (size_t i = 0; i < missing_count; i++) {
                uint16_t nacked_id = ntohs(missing_ids[i]);
                
                message_t pending_msg;
                bool found = false;
                
                while (!found && queue_pop(&ctx->pending_queue, &pending_msg) == 0) {
                    if (pending_msg.header.seq_id == nacked_id) {
                        header = pending_msg.header;

                        header.version = SIMP_VERSION;
                        header.ack_id = atomic_load(&ctx->last_acked_id);
                        
                        simp_serialize_header(&header, buffer);
                        memcpy(buffer + HEADER_SIZE, pending_msg.data, pending_msg.header.data_len);
                        
                        pthread_mutex_lock(&ctx->sock_write_mutex);
                        int err = sendto(ctx->sockfd, buffer, HEADER_SIZE + pending_msg.header.data_len, 0,
                                      (struct sockaddr*)&local_addr, sizeof(local_addr));
                        pthread_mutex_unlock(&ctx->sock_write_mutex);

                        if (err < 0) {
                            ERR("resend");
                        }
                        found = true;
                    } 
                    queue_push(&ctx->pending_queue, &pending_msg);
                }
            }
        }
        usleep(10000);
    }
    printf("nack queue closed\n");
    return NULL;
}

static int simp_is_connected(simp_context_t *ctx) {
    return atomic_load(&ctx->remote_peer_active);
}

static int simp_start(simp_context_t *ctx) {
    atomic_store(&ctx->connection_active, 1);
    atomic_store(&ctx->remote_peer_active, 1);
    
    int err = pthread_create(&ctx->sender_thread, NULL, sender_handler, ctx);
    if (err) {
        ERR("start");
        return err;
    }
    
    err = pthread_create(&ctx->keep_alive_thread, NULL, keep_alive_handler, ctx);
    if (err) {
        ERR("start");
        return err;
    }
    
    err = pthread_create(&ctx->reader_thread, NULL, simp_reader_handler, ctx);
    if (err) {
        ERR("start");
        return err;
    }
    
    err = pthread_create(&ctx->nack_handler_thread, NULL, nack_handler, ctx);
    if (err) {
        ERR("start");
        return err;
    }
    
    return 0;
}

static int simp_connect(simp_context_t* ctx, const char* ip, uint16_t port) {
    pthread_mutex_lock(&ctx->addr_mutex);
    memset(&ctx->addr, 0, sizeof(ctx->addr));
    ctx->addr.sin_family = AF_INET;
    ctx->addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ctx->addr.sin_addr);
    pthread_mutex_unlock(&ctx->addr_mutex);

    atomic_store(&ctx->connection_active, 1);
    atomic_store(&ctx->remote_peer_active, 1);
    
    int err = pthread_create(&ctx->sender_thread, NULL, sender_handler, ctx);
    if (err) {
        ERR("connect");
        return err;
    }
    
    err = pthread_create(&ctx->reader_thread, NULL, simp_reader_handler, ctx);
    if (err) {
        ERR("connect");
        return err;
    }

    err = pthread_create(&ctx->keep_alive_thread, NULL, keep_alive_handler, ctx);
    if (err) {
        ERR("connect");
        return err;
    }
    
    err = pthread_create(&ctx->nack_handler_thread, NULL, nack_handler, ctx);
    if (err) {
        ERR("connect");
        return err;
    }

    return 0;
}

static simp_context_t* simp_new() {
    return simp_create_shared_context(NULL);
}

#endif // SIMP_H
