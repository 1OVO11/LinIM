#include "protocol.h"
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/socket.h>

int encode(char *buf, const Packet &pkt) {
  int offset = HEAD_SIZE;
  // type：uint16_t，htons转换
  uint16_t type_net = htons(static_cast<uint16_t>(pkt.type));
  std::memcpy(buf + offset, &type_net, sizeof(MsgType));
  offset += sizeof(MsgType);
  // user_id
  uint32_t user_id_net = htonl(static_cast<uint32_t>(pkt.user_id));
  std::memcpy(buf + offset, &user_id_net, sizeof(int));
  offset += sizeof(int);
  // target_id
  uint32_t target_id_net = htonl(static_cast<uint32_t>(pkt.target_id));
  std::memcpy(buf + offset, &target_id_net, sizeof(int));
  offset += sizeof(int);
  // 字符串不需要字节序转换
  std::memcpy(buf + offset, pkt.username, sizeof(pkt.username));
  offset += sizeof(pkt.username);
  std::memcpy(buf + offset, pkt.msg, sizeof(pkt.msg));
  offset += sizeof(pkt.msg);
  // 用实际算出来的offset作为总长度，写入头部
  uint32_t total_len = static_cast<uint32_t>(offset);
  uint32_t net_total_len = htonl(total_len);
  std::memcpy(buf, &net_total_len, HEAD_SIZE);
  return offset;
}

int decode(const char *buf, int data_len, Packet &pkt) {
  // 数据不够包头，直接返回
  if (data_len < HEAD_SIZE) {
    return 0;
  }

  // 从包头读出包长度
  uint32_t total_len_net;
  std::memcpy(&total_len_net, buf, HEAD_SIZE);
  uint32_t total_len_host = ntohl(total_len_net);

  // 缓冲区里数据不够一整个包
  if (data_len < (int)total_len_host) {
    return 0;
  }

  const char *body_buf = buf + HEAD_SIZE;
  int offset = 0;
  // type
  uint16_t type_net;
  std::memcpy(&type_net, body_buf + offset, sizeof(MsgType));
  pkt.type = static_cast<MsgType>(ntohs(type_net));
  offset += sizeof(MsgType);
  // user_id
  uint32_t user_id_net;
  std::memcpy(&user_id_net, body_buf + offset, sizeof(int));
  pkt.user_id = static_cast<int>(ntohl(user_id_net));
  offset += sizeof(int);
  // target_id
  uint32_t target_id_net;
  std::memcpy(&target_id_net, body_buf + offset, sizeof(int));
  pkt.target_id = static_cast<int>(ntohl(target_id_net));
  offset += sizeof(int);
  // 字符串
  std::memcpy(pkt.username, body_buf + offset, sizeof(pkt.username));
  offset += sizeof(pkt.username);
  std::memcpy(pkt.msg, body_buf + offset, sizeof(pkt.msg));
  offset += sizeof(pkt.msg);

  return (int)total_len_host;
}

int send_package(int fd, const Packet &pkt) {
  char buf[512] = {0};
  int len = encode(buf, pkt);
  return send(fd, buf, len, 0);
}