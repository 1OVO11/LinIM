#ifndef __SERVICE_H__
#define __SERVICE_H__

#include "../data/MySQL_dao.h"
#include "../network/network.h"

// 新增：回调函数声明
void packet_callback(int fd, const Packet &pkt);
void close_callback(int fd);

#endif
