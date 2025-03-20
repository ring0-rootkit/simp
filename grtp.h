#ifndef _GRTP_H
#define _GRTP_H

#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>

// max ip data len assuming header size of 60bytes
#define MAX_PACKET_SIZE 65475

#define send_high
#define send_low
#define send_medium
#define send_once


// TODO:
// use to send messages that contain complete information
// selfcontained packets will not be resend if packet with the same
// group_id has been sent
// #define send_selfcontained send


#define RAW_SOCKET_ERROR 1 /* raw sockets error */


// TODO:
// implement parsing
// 1) group_id generator
// 2) severity handling

typedef struct Connection {
  pthread_mutex_t mut;

  int sockfd;

  struct sockaddr remote_addr;
  uint32_t con_id;
  uint32_t avail_seq_number;
} con_t;

typedef struct GroupConnection {
  con_t *con;
  uint32_t group_id;
} group_con_t;

typedef enum Urgency {
  LOW,
  MEDIUM,
  HIGH,
} urgency_t;

typedef struct Packet {
  uint8_t connection_id;
  uint32_t sequence_number;
  uint8_t urgency_level;
  uint32_t group_id;
  uint32_t payload_len;
  void *payload;
} packet_t;

#define HEADER_LEN (sizeof(packet_t) - sizeof(void *))

// BEGIN Connection methods

__inline uint32_t _new_con_id();
uint32_t _new_con_id() {
  struct timeval time;
  gettimeofday(&time, NULL);
  return time.tv_usec;
}

__inline int grtp_bind(struct sockaddr remote_addr, con_t *con);
int grtp_bind(struct sockaddr remote_addr, con_t *con) {

  pthread_mutex_t mut = {};
  pthread_mutex_init(&mut, NULL);

  int sockfd = socket(AF_INET, SOCK_RAW, AF_INET);
  if (sockfd == -1) {
    return RAW_SOCKET_ERROR;
  }

  con->mut = mut;
  con->sockfd = sockfd;
  con->remote_addr = remote_addr;
  con->con_id = _new_con_id();
  con->avail_seq_number = 1;

  return 0;
}

__inline int grtp_listen(con_t *con, int backlog);
int grtp_listen(con_t *con, int backlog) {
  return listen(con->sockfd, backlog);
}

__inline uint8_t _con_id( con_t *con);
uint8_t _con_id( con_t *con) { return con->con_id; }

__inline uint32_t _next_seq_num(con_t *con);
uint32_t _next_seq_num(con_t *con) {
  uint32_t new_seq_num;

  pthread_mutex_lock(&con->mut);
  new_seq_num = con->avail_seq_number++;
  pthread_mutex_unlock(&con->mut);

  return new_seq_num;
}
// END Connection methoods

// BEGIN Packet methods
__inline uint8_t _len( packet_t *pkt);
uint8_t _len( packet_t *pkt) { return sizeof(*pkt) - HEADER_LEN; }

__inline int _parse_packet( void *buf,  uint8_t len,
                                  packet_t *pkt);
int _parse_packet( void *buf,  uint8_t len, packet_t *pkt) {
  return 0;
}
// END Packet methods

__inline int grtp_send(con_t *con, void *buf, uint32_t len, uint32_t group_id,
                         urgency_t urgency);
int grtp_send(con_t *con, void *buf, uint32_t len, uint32_t group_id,
                urgency_t urgency) {
  int err;

  packet_t pkt = {
      .connection_id = _con_id(con),
      .sequence_number = _next_seq_num(con),
      .urgency_level = (uint8_t)urgency,
      .group_id = group_id,
      .payload_len = len,
      .payload = buf,
  };

  uint32_t pkt_len = sizeof(pkt);

  if (sendto(con->sockfd, ( void *)&pkt, (size_t)pkt_len, 0,
             &con->remote_addr, (socklen_t)sizeof(con->remote_addr)) == -1) {
    return RAW_SOCKET_ERROR;
  }

  return 0;
}
__inline int grtp_recv(con_t *con, packet_t *pkt);
int grtp_recv(con_t *con, packet_t *pkt) {
  void *buf = malloc(MAX_PACKET_SIZE);

  socklen_t socklen = sizeof(con->remote_addr);

  size_t recvd_len = recvfrom(con->sockfd, buf, MAX_PACKET_SIZE, 0,
                              &con->remote_addr, &socklen);

  if (recvd_len == -1) {
    return RAW_SOCKET_ERROR;
  }

  int err = _parse_packet(buf, recvd_len, pkt);
  if (err != 0) {
    return err;
  }

  return 0;
}

#endif
