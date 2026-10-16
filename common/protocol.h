#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <cstdint>
#include <cstring>

enum MsgType : uint16_t {
  MSG_REGISTER = 0,
  MSG_REGISTER_ACK = 1,
  MSG_CHAT_ALL = 2,
  MSG_CHAT_ONE = 3,
  MSG_LOGOUT = 4,
  MSG_SYSTEM = 5,
  MSG_LOGIN = 6,
  MSG_LOGIN_ACK = 7,
  MSG_ONLINE_LIST = 8,    // 查询在线列表（客户端->服务端）
  MSG_ONLINE_LIST_ACK = 9 // 在线列表回复（服务端->客户端，msg携带 "id=x 账号"）
};

struct Packet {
  MsgType type;
  int user_id;
  int target_id;
  char username[32];
  char msg[128];
};

#define HEAD_SIZE 4
#define BODY_SIZE sizeof(Packet)
const uint32_t TOTAL_PACK_SIZE = HEAD_SIZE + BODY_SIZE;

// 编码
int encode(char *buf, const Packet &pkt);
// 解码
int decode(const char *buf, int data_len, Packet &pkt);
// 发包
int send_package(int fd, const Packet &pkt);

#endif