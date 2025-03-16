#ifndef _GRTP_H
#define _GRTP_H

#include <cstdlib>
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

// use to send messages that contain complete information
// selfcontained packets will not be resend if packet with the same
// group_id has been sent
#define send_selfcontained

// TODO:
// implement parsing
// 1) group_id generator
// 2) severity handling

typedef enum ErrorCode {
  OK,
  // indicates error with raw sockets
  RAW_SOCKET_ERROR,
} err_code_t;

typedef struct Connection {
  pthread_mutex_t mut;

  int sockfd;

  struct sockaddr remote_addr;
  uint32_t con_id;
  uint32_t avail_seq_number;
} con_t;

typedef struct GroupConnection {
  Connection *con;
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

__inline con_t connect(sockaddr remote_addr);
con_t connect(sockaddr remote_addr) {

  pthread_mutex_t mut = {};
  pthread_mutex_init(&mut, NULL);

  int sockfd = socket(AF_INET, SOCK_RAW, AF_INET);

  return con_t{
      .mut = mut,
      .sockfd = sockfd,
      .remote_addr = remote_addr,
      .con_id = _new_con_id(),
      .avail_seq_number = 1,
  };
}
__inline uint8_t _con_id(const con_t *con);
uint8_t _con_id(const con_t *con) { return con->con_id; }

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
__inline uint8_t _len(const packet_t *pkt);
uint8_t _len(const packet_t *pkt) { return sizeof(*pkt) - HEADER_LEN; }

__inline err_code_t _parse_packet(const void *buf, const uint8_t len,
                                  packet_t *pkt);
err_code_t _parse_packet(const void *buf, const uint8_t len, packet_t *pkt) {
  return OK;
}
// END Packet methods

__inline err_code_t send(con_t *con, void *buf, uint32_t len, uint32_t group_id,
                         urgency_t urgency);
err_code_t send(con_t *con, void *buf, uint32_t len, uint32_t group_id,
                urgency_t urgency) {
  err_code_t err;

  packet_t pkt = packet_t{
      .connection_id = _con_id(con),
      .sequence_number = _next_seq_num(con),
      .urgency_level = (uint8_t)urgency,
      .group_id = group_id,
      .payload_len = len,
      .payload = buf,
  };

  uint32_t pkt_len = sizeof(pkt);

  if (sendto(con->sockfd, (const void *)&pkt, (size_t)pkt_len, 0,
             &con->remote_addr, (socklen_t)sizeof(con->remote_addr)) == -1) {
    return RAW_SOCKET_ERROR;
  }

  return OK;
}
__inline err_code_t recv(con_t *con, packet_t *pkt);
err_code_t recv(con_t *con, packet_t *pkt) {
  void *buf = malloc(MAX_PACKET_SIZE);

  socklen_t socklen = sizeof(con->remote_addr);

  size_t recvd_len = recvfrom(con->sockfd, buf, MAX_PACKET_SIZE, 0,
                              &con->remote_addr, &socklen);

  if (recvd_len == -1) {
    return RAW_SOCKET_ERROR;
  }

  err_code_t err = _parse_packet(buf, recvd_len, pkt);
  if (err != OK) {
    return err;
  }

  return OK;
}

#endif
