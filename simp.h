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
#define HEADER_SIZE 8
#define SEND_BUFFER_SIZE 128
#define RCVD_BUFFER_SIZE 128
#define KEEP_ALIVE_INTERVAL 1
#define KEEP_ALIVE_TIMEOUT 5
#define MAX_MISSING_IDS 64
#define MAX_QUEUE_SIZE 1024
#define SIMP_SHM_NAME "/simp_context"

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
    bool in_use;
} buffered_packet_t;

typedef struct {
    uint8_t data[MAX_PACKET_SIZE];
    size_t data_len;
    uint16_t seq_id;
    uint8_t group_id;
    uint8_t flags;
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
    
    pthread_mutex_t addr_mutex;
    pthread_mutex_t pack_info_mutex;
    pthread_mutex_t seq_mutex;
    pthread_mutex_t read_mutex;
    pthread_cond_t read_cond;
    
    atomic_uint_least16_t next_seq_id;
    atomic_uint_least16_t last_rcvd_id;
    atomic_uint_least16_t last_acked_id;
    atomic_uint_least8_t send_cntr;
    atomic_uint_least8_t rcvd_cntr;
    atomic_uint_least8_t pack_info;
    
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
    uint16_t net_len = htons(header->data_len);
    memcpy(buffer, header, 4);
    memcpy(buffer + 4, &net_seq, 2);
    buffer[6] = header->flags;
    memcpy(buffer + 7, &net_len, 2);
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


static inline void simp_deserialize_header(const uint8_t* buffer, packet_header_t* header) {
    memcpy(header, buffer, 4);
    memcpy(&header->seq_id, buffer + 4, 2);
    header->seq_id = ntohs(header->seq_id);
    header->flags = buffer[6];
    memcpy(&header->data_len, buffer + 7, 2);
    header->data_len = ntohs(header->data_len);
}

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
    atomic_init(&ctx->last_acked_id, 0);
    atomic_init(&ctx->pack_info, 0);
    atomic_init(&ctx->connection_active, 0);
    
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_mutex_init(&ctx->addr_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->pack_info_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->seq_mutex, &mutex_attr);
    pthread_mutex_init(&ctx->read_mutex, &mutex_attr);
    
    pthread_mutexattr_destroy(&mutex_attr);
    
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    
    pthread_cond_init(&ctx->read_cond, &cond_attr);
    
    pthread_condattr_destroy(&cond_attr);
    
    queue_init(&ctx->reader_queue);
    queue_init(&ctx->nack_queue);
    queue_init(&ctx->user_queue);
    queue_init(&ctx->send_queue);
    queue_init(&ctx->pending_queue);
    
    return ctx;
}

static simp_context_t* simp_attach_shared_context(const char* name) {
    if (name == NULL) {
        ERR("No shared memory name provided");
        return NULL;
    }
    
    int shm_fd = shm_open(name, O_RDWR, 0666);
    if (shm_fd == -1) {
        ERR("shm_open (attach)");
        return NULL;
    }
    
    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        ERR("fstat");
        close(shm_fd);
        return NULL;
    }
    
    if (shm_stat.st_size != sizeof(simp_context_t)) {
        ERR("Shared memory size mismatch");
        close(shm_fd);
        return NULL;
    }
    
    simp_context_t* ctx = (simp_context_t*)mmap(NULL, sizeof(simp_context_t), 
                                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (ctx == MAP_FAILED) {
        ERR("mmap (attach)");
        close(shm_fd);
        return NULL;
    }
    
    ctx->shm_fd = shm_fd;
    ctx->is_shm_owner = false;
    
    return ctx;
}

static void simp_detach_shared_context(simp_context_t* ctx) {
    if (ctx == NULL) return;
    
    if (munmap(ctx, sizeof(simp_context_t)) == -1) {
        ERR("munmap");
    }
    
    if (close(ctx->shm_fd) == -1) {
        ERR("close (shm_fd)");
    }
    
    if (ctx->is_shm_owner) {
        if (shm_unlink(ctx->shm_name) == -1) {
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
    
    memcpy(msg, &queue->messages[queue->head], sizeof(message_t));
    queue->head = (queue->head + 1) % MAX_QUEUE_SIZE;
    queue->count--;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}


static void buffer_add_packet(simp_context_t* ctx, const packet_header_t* header, 
                             const uint8_t* data, packet_priority_t prio) {
    if (prio == PRIO_LOW) return;

    if (prio == PRIO_MEDIUM) {
        message_t temp_msg;
        message_queue_t temp_queue;
        queue_init(&temp_queue);
        
        while (queue_pop(&ctx->pending_queue, &temp_msg) == 0) {
            if (temp_msg.group_id == header->group_id && 
                temp_msg.priority == PRIO_MEDIUM) {
                continue;
            }
            queue_push(&temp_queue, &temp_msg);
        }
        
        while (queue_pop(&temp_queue, &temp_msg) == 0) {
            queue_push(&ctx->pending_queue, &temp_msg);
        }
        
        queue_cleanup(&temp_queue);
    }

    message_t msg;
    msg.seq_id = header->seq_id;
    msg.group_id = header->group_id;
    msg.priority = prio;
    msg.data_len = header->data_len;
    msg.flags = 0;
    memcpy(msg.data, data, header->data_len);

    queue_push(&ctx->pending_queue, &msg);
    atomic_fetch_add(&ctx->send_cntr, 1);
}

static void buffer_cleanup(simp_context_t* ctx, uint16_t last_received_id, 
                          const uint16_t* missing_ids, size_t missing_count) {
    message_t msg;
    message_queue_t temp_queue;
    queue_init(&temp_queue);
    struct sockaddr_in local_addr;
    
    pthread_mutex_lock(&ctx->addr_mutex);
    local_addr = ctx->addr;
    pthread_mutex_unlock(&ctx->addr_mutex);
    
    while (queue_pop(&ctx->pending_queue, &msg) == 0) {
        if (msg.seq_id <= last_received_id) {
            bool missing = false;
            for (size_t j = 0; j < missing_count; j++) {
                if (msg.seq_id == ntohs(missing_ids[j])) {
                    missing = true;
                    break;
                }
            }

            if (missing) {
                if (msg.priority == PRIO_HIGH || msg.priority == PRIO_MEDIUM) {
                    uint8_t buffer[MAX_PACKET_SIZE];
                    packet_header_t header = {
                        .version = SIMP_VERSION,
                        .group_id = msg.group_id,
                        .seq_id = msg.seq_id,
                        .data_len = msg.data_len,
                        .flags = msg.flags
                    };
                    simp_serialize_header(&header, buffer);
                    memcpy(buffer + HEADER_SIZE, msg.data, msg.data_len);
                    sendto(ctx->sockfd, buffer, HEADER_SIZE + msg.data_len, 0,
                          (struct sockaddr*)&local_addr, sizeof(local_addr));
                }
                queue_push(&temp_queue, &msg);
            } else {
                atomic_fetch_sub(&ctx->send_cntr, 1);
            }
        } else {
            queue_push(&temp_queue, &msg);
        }
    }
    
    while (queue_pop(&temp_queue, &msg) == 0) {
        queue_push(&ctx->pending_queue, &msg);
    }
    
    queue_cleanup(&temp_queue);
}

static void* sender_handler(void* arg) {
    simp_context_t* ctx = (simp_context_t*)arg;
    message_t msg;
    uint8_t buffer[MAX_PACKET_SIZE];
    packet_header_t header;
    struct sockaddr_in local_addr;

    while (atomic_load(&ctx->connection_active)) {
        if (queue_pop(&ctx->send_queue, &msg) == 0) {
            header.version = SIMP_VERSION;
            header.group_id = msg.group_id;
            header.seq_id = msg.seq_id;
            header.data_len = msg.data_len;
            header.flags = msg.flags;
            
            simp_serialize_header(&header, buffer);
            memcpy(buffer + HEADER_SIZE, msg.data, msg.data_len);
            
            pthread_mutex_lock(&ctx->addr_mutex);
            local_addr = ctx->addr;
            pthread_mutex_unlock(&ctx->addr_mutex);
            
            int err = sendto(ctx->sockfd, buffer, HEADER_SIZE + msg.data_len, 0,
                           (struct sockaddr*)&local_addr, sizeof(local_addr));
            if (err < 0) {
                ERR("sendto");
            }
        }
        usleep(1000);
    }
    return NULL;
}

static void* simp_reader_handler(void* args) {
    simp_context_t *ctx = (simp_context_t*)args;
    uint8_t buffer[MAX_PACKET_SIZE];
    message_t msg;

    while (atomic_load(&ctx->connection_active)) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        ssize_t len = recvfrom(ctx->sockfd, buffer, MAX_PACKET_SIZE, 0, 
                              (struct sockaddr*)&addr, &addr_len);
        if (len < 0) {
            ERR("recvfrom");
            continue;
        }

        pthread_mutex_lock(&ctx->addr_mutex);
        if (!ctx->addr.sin_family) {
            ctx->addr = addr;
        }
        pthread_mutex_unlock(&ctx->addr_mutex);

        if (len < HEADER_SIZE) continue;

        packet_header_t header;
        simp_deserialize_header(buffer, &header);
        void* data = buffer + HEADER_SIZE;

        atomic_store(&ctx->last_rcvd_id, header.seq_id);

        if(header.flags & FLAG_KEEP_ALIVE_REQ) {
            printf("RECEIVED KA REQUEST!!!!\n");
        }

        if (header.flags & FLAG_KEEP_ALIVE_RESP) {
            uint16_t last_acked = *(uint16_t*)data;
            printf("KA resp, id: %d", last_acked);
            uint16_t* nacked_ids = (uint16_t*)(data + sizeof(uint16_t));
            size_t nacked_count = (header.data_len - sizeof(uint16_t)) / sizeof(uint16_t);
            
            atomic_store(&ctx->last_acked_id, last_acked);
            
            if (nacked_count > 0) {
                msg.seq_id = header.seq_id;
                msg.flags = header.flags;
                msg.data_len = header.data_len;
                memcpy(msg.data, data, header.data_len);
                queue_push(&ctx->nack_queue, &msg);
            }
            
            continue;
        }

        if (header.flags & FLAG_NACK) {
            uint16_t* missing_ids = (uint16_t*)data;
            size_t missing_count = (header.data_len - HEADER_SIZE) / sizeof(uint16_t);
            
            msg.seq_id = header.seq_id;
            msg.flags = header.flags;
            msg.data_len = header.data_len;
            memcpy(msg.data, data, header.data_len);
            queue_push(&ctx->nack_queue, &msg);
            
            continue;
        }

        msg.seq_id = header.seq_id;
        msg.flags = header.flags;
        msg.data_len = header.data_len;
        memcpy(msg.data, data, header.data_len);
        queue_push(&ctx->user_queue, &msg);

        pthread_mutex_lock(&ctx->read_mutex);
        pthread_cond_broadcast(&ctx->read_cond);
        pthread_mutex_unlock(&ctx->read_mutex);
    }
    return NULL;
}

static int simp_receive(simp_context_t* ctx, char* buf, int buf_len) {
    message_t msg;
    
    if (queue_pop(&ctx->user_queue, &msg) != 0) {
        return -1;
    }

    size_t copy_len = (msg.data_len < buf_len) ? msg.data_len : buf_len;
    memcpy(buf, msg.data, copy_len);
    return copy_len;
}

static int simp_init(simp_context_t* ctx, const char* ip, uint16_t port) {
    ctx->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx->sockfd < 0) { 
        ERR("init");
        return -1;
    }

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
    atomic_init(&ctx->last_acked_id, 0);
    atomic_init(&ctx->pack_info, 0);
    atomic_init(&ctx->connection_active, 0);
    
    pthread_mutex_init(&ctx->addr_mutex, NULL);
    pthread_mutex_init(&ctx->pack_info_mutex, NULL);
    pthread_mutex_init(&ctx->seq_mutex, NULL);
    pthread_mutex_init(&ctx->read_mutex, NULL);
    pthread_cond_init(&ctx->read_cond, NULL);
    
    queue_init(&ctx->reader_queue);
    queue_init(&ctx->nack_queue);
    queue_init(&ctx->user_queue);
    queue_init(&ctx->send_queue);
    queue_init(&ctx->pending_queue);

    return 0;
}

static void simp_cleanup(simp_context_t* ctx) {
    atomic_store(&ctx->connection_active, 0);
    
    pthread_mutex_lock(&ctx->read_mutex);
    pthread_cond_broadcast(&ctx->read_cond);
    pthread_mutex_unlock(&ctx->read_mutex);
    
    close(ctx->sockfd);
    
    queue_cleanup(&ctx->reader_queue);
    queue_cleanup(&ctx->nack_queue);
    queue_cleanup(&ctx->user_queue);
    queue_cleanup(&ctx->send_queue);
    queue_cleanup(&ctx->pending_queue);
    
    pthread_mutex_destroy(&ctx->addr_mutex);
    pthread_mutex_destroy(&ctx->pack_info_mutex);
    pthread_mutex_destroy(&ctx->seq_mutex);
    pthread_mutex_destroy(&ctx->read_mutex);
    pthread_cond_destroy(&ctx->read_cond);
    
    simp_detach_shared_context(ctx);
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

    while (atomic_load(&ctx->connection_active)) {
        pthread_mutex_lock(&ctx->addr_mutex);
        addr_valid = ctx->addr.sin_family != 0;
        if (addr_valid) {
            local_addr = ctx->addr;
        }
        pthread_mutex_unlock(&ctx->addr_mutex);


        printf("loaded address: %s\n", inet_ntoa(local_addr.sin_addr));

        if (addr_valid) {
            keep_alive.seq_id = atomic_load(&ctx->last_acked_id);

            simp_serialize_header(&keep_alive, buffer);
            printf("sending KA packet\n");
            int err = sendto(ctx->sockfd, buffer, HEADER_SIZE, 0,
                  (struct sockaddr*)&local_addr, sizeof(local_addr));
            if (err < 0) {
                ERR("Keep alive send");
            }
        }

        sleep(KEEP_ALIVE_INTERVAL);
    }
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
            uint16_t* missing_ids = (uint16_t*)msg.data;
            size_t missing_count = (msg.data_len - HEADER_SIZE) / sizeof(uint16_t);
            
            pthread_mutex_lock(&ctx->addr_mutex);
            local_addr = ctx->addr;
            pthread_mutex_unlock(&ctx->addr_mutex);
            
            for (size_t i = 0; i < missing_count; i++) {
                uint16_t nacked_id = ntohs(missing_ids[i]);
                
                message_t pending_msg;
                bool found = false;
                
                message_queue_t temp_queue;
                queue_init(&temp_queue);
                
                while (queue_pop(&ctx->pending_queue, &pending_msg) == 0) {
                    if (pending_msg.seq_id == nacked_id) {
                        header.version = SIMP_VERSION;
                        header.group_id = pending_msg.group_id;
                        header.seq_id = pending_msg.seq_id;
                        header.data_len = pending_msg.data_len;
                        header.flags = pending_msg.flags;
                        
                        simp_serialize_header(&header, buffer);
                        memcpy(buffer + HEADER_SIZE, pending_msg.data, pending_msg.data_len);
                        
                        int err = sendto(ctx->sockfd, buffer, HEADER_SIZE + pending_msg.data_len, 0,
                                      (struct sockaddr*)&local_addr, sizeof(local_addr));
                        if (err < 0) {
                            ERR("resend");
                        }
                        found = true;
                    } else {
                        queue_push(&temp_queue, &pending_msg);
                    }
                }
                
                while (queue_pop(&temp_queue, &pending_msg) == 0) {
                    queue_push(&ctx->pending_queue, &pending_msg);
                }
                
                queue_cleanup(&temp_queue);
            }
        }
        usleep(10000);
    }
    return NULL;
}

static int simp_start(simp_context_t *ctx) {
    atomic_store(&ctx->connection_active, 1);
    
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

static int simp_send(simp_context_t* ctx, const uint8_t* data, size_t len,
                    packet_priority_t prio, uint8_t group_id) {
    if (prio == PRIO_LOW) return 0;

    packet_header_t header = {
        .version = SIMP_VERSION,
        .group_id = group_id,
        .data_len = (uint16_t)len,
        .flags = 0
    };

    header.seq_id = atomic_fetch_add(&ctx->next_seq_id, 1);

    message_t msg;
    msg.seq_id = header.seq_id;
    msg.group_id = header.group_id;
    msg.data_len = header.data_len;
    msg.flags = header.flags;
    msg.priority = prio;
    memcpy(msg.data, data, len);

    queue_push(&ctx->pending_queue, &msg);
    
    queue_push(&ctx->send_queue, &msg);
    return len;
}

static simp_context_t* simp_new() {
    return simp_create_shared_context(NULL);
}

#endif // SIMP_H
