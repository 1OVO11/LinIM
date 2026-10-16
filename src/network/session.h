#ifndef __SESSION_H__
#define __SESSION_H__

#include "../../common/protocol.h"
#include <string>
#include <unordered_set>

// 给业务层使用的会话接口，业务层不直接操作fd
class ISession {
public:
  virtual ~ISession() = default;

  // 注册客户端连接，分配在线id
  virtual int registerClient(int fd, const std::string &account) = 0;

  // 设置登录状态
  virtual void setLoginState(int uid, bool login) = 0;

  // 通过uid，单发Packet给目标用户
  virtual bool sendToUid(int uid, const Packet &pkt) = 0;

  // 广播数据包到指定fd集合
  virtual void broadcast(const Packet &pkt,
                         const std::unordered_set<int> &fd_set) = 0;

  // 获取在线用户列表字符串
  virtual std::string getOnlineUserList() = 0;

  // 根据账号查找在线uid，不存在返回-1
  virtual int getUidByAccount(const std::string &account) = 0;

  // 根据fd拿到uid
  virtual int getUidByFd(int fd) = 0;

  // 删除连接
  virtual void removeClient(int fd) = 0;
};

#endif