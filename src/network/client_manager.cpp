#include "client_manager.h"
#include "../../common/protocol.h"
#include <cstring>
#include <sstream>

// 将开始的m_next_uid初始化为1，避免使用0作为有效的uid
ClientManager::ClientManager() : m_next_uid(1) {}

// 析构函数不需要做任何事情，因为所有的资源都是自动管理的
ClientManager::~ClientManager() {}

int ClientManager::registerClient(int fd, const std::string &account) {
  std::lock_guard<std::mutex> lock(m_mtx);
  int uid = m_next_uid++;
  m_fd_uid[fd] = uid;           // 将文件描述符映射到用户ID
  m_uid_fd[uid] = fd;           // 将用户ID映射到文件描述符
  m_uid_account[uid] = account; // 将用户ID映射到账号名
  m_uid_login[uid] = false;     // 初始化登录状态为false，表示用户尚未登录
  return uid;
}

// 设置用户的登录状态
void ClientManager::setLoginState(int uid, bool login) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_uid_login.find(uid); // 查找用户ID对应的登录状态
  if (it != m_uid_login.end()) {
    it->second = login;
  }
}

bool ClientManager::sendToUid(int uid, const Packet &pkt) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_uid_fd.find(uid);
  if (it == m_uid_fd.end()) { // 如果找不到对应的用户ID，返回false表示发送失败
    return false;
  }
  int fd = it->second;          // 获取对应的文件描述符
  return send_package(fd, pkt); // 调用send_package函数发送数据包
}

// 广播数据包到指定的文件描述符集合
void ClientManager::broadcast(const Packet &pkt,
                              const std::unordered_set<int> &fd_set) {
  std::lock_guard<std::mutex> lock(m_mtx);
  for (int fd : fd_set) {
    send_package(fd, pkt);
  }
}

std::string ClientManager::getOnlineUserList() {
  std::lock_guard<std::mutex> lock(m_mtx);
  std::stringstream ss; // 使用stringstream来构建在线用户列表字符串
  for (auto &pair : m_uid_account) {
    int uid = pair.first;          // 获取用户ID
    std::string acc = pair.second; // 获取账号名
    if (m_uid_login[uid]) {
      ss << "id=" << uid << " " << acc
         << " "; // 如果用户已登录，将其信息添加到字符串中
    }
  }
  return ss.str(); // 返回构建好的在线用户列表字符串
}

int ClientManager::getUidByAccount(const std::string &account) {
  std::lock_guard<std::mutex> lock(m_mtx);
  for (auto &pair : m_uid_account) {
    if (pair.second == account &&
        m_uid_login
            [pair.first]) { // 如果账号名匹配且用户已登录，返回对应的用户ID
      return pair.first;
    }
  }
  return -1;
}

int ClientManager::getUidByFd(int fd) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_fd_uid.find(fd);
  if (it != m_fd_uid.end()) // 如果找到了对应的文件描述符，返回对应的用户ID
    return it->second;
  return -1;
}

void ClientManager::removeClient(int fd) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto fd_it = m_fd_uid.find(fd);
  if (fd_it == m_fd_uid.end())
    return;

  int uid = fd_it->second;
  m_fd_uid.erase(fd_it);    // 从文件描述符到用户ID的映射中删除该条目
  m_uid_fd.erase(uid);      // 从用户ID到文件描述符的映射中删除该条目
  m_uid_account.erase(uid); // 从用户ID到账号名的映射中删除该条目
  m_uid_login.erase(uid);   // 从用户ID到登录状态的映射中删除该条目
}

std::vector<int> ClientManager::getAllOnlineUid() {
  std::lock_guard<std::mutex> lock(m_mtx);
  std::vector<int> uids;
  for (auto &pair : m_uid_login) {
    if (pair.second) {
      uids.push_back(pair.first);
    }
  }
  return uids;
}

std::string ClientManager::getAccountByUid(int uid) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_uid_account.find(uid);
  if (it != m_uid_account.end())
    return it->second;
  return "";
}