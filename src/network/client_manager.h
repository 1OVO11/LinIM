#ifndef __CLIENT_MANAGER_H__
#define __CLIENT_MANAGER_H__

#include "session.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ClientManager 实现ISession接口，管理所有在线连接
class ClientManager : public ISession {
public:
  ClientManager();

  ~ClientManager() override;

  int registerClient(int fd, const std::string &account) override;

  void setLoginState(int uid, bool login) override;

  bool sendToUid(int uid, const Packet &pkt) override;

  void broadcast(const Packet &pkt,
                 const std::unordered_set<int> &fd_set) override;

  std::string getOnlineUserList() override;

  int getUidByAccount(const std::string &account) override;

  int getUidByFd(int fd) override;

  void removeClient(int fd) override;

  // —— 以下为业务层补的只读查询接口（ISession 之外的扩展） ——

  // 获取所有已登录(login=true)的uid列表
  std::vector<int> getAllOnlineUid();

  // 根据uid查账号名，不存在返回空串
  std::string getAccountByUid(int uid);

private:
  std::mutex m_mtx;
  int m_next_uid; // 自增在线uid（连接id）

  // fd -> uid
  std::unordered_map<int, int> m_fd_uid;
  // uid -> fd
  std::unordered_map<int, int> m_uid_fd;
  // uid -> account账号名
  std::unordered_map<int, std::string> m_uid_account;
  // uid -> 是否登录
  std::unordered_map<int, bool> m_uid_login;
};

#endif