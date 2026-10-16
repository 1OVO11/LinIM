#include "service.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// 外部全局对象
extern MySQLDAO db;
extern ClientManager g_client_mgr;

// 业务层自维护 fd->账号 映射：
// network层断线时是先 removeClient(fd) 再回调 close_callback，届时从
// ClientManager 已查不到该fd，所以业务层自己记一份，用于断线/登出时识别身份。
static std::mutex g_conn_mtx;
static std::unordered_map<int, std::string> g_fd_account; // fd -> 账号

// 同一连接（fd）的消息按到达顺序串行处理：
// 网络层把每个包作为一个任务投递给线程池，多线程并发执行时，
// 后到的登出/断开可能先于前面的单聊执行，导致 requireLogin 失败、
// 消息被静默丢弃（"发了消息立刻退出，对方收不到"）。所以按 fd 加锁串行化。
static std::mutex g_fd_locks_mtx;
static std::unordered_map<int, std::unique_ptr<std::mutex>> g_fd_locks;

static std::mutex &fdLock(int fd) {
  std::lock_guard<std::mutex> lk(g_fd_locks_mtx);
  auto &p = g_fd_locks[fd];
  if (!p)
    p.reset(new std::mutex());
  return *p;
}

static std::string getAccountByFd(int fd) {
  std::lock_guard<std::mutex> lock(g_conn_mtx);
  auto it = g_fd_account.find(fd);
  return it == g_fd_account.end() ? std::string() : it->second;
}

static void setAccountByFd(int fd, const std::string &account) {
  std::lock_guard<std::mutex> lock(g_conn_mtx);
  g_fd_account[fd] = account;
}

static void eraseAccountByFd(int fd) {
  std::lock_guard<std::mutex> lock(g_conn_mtx);
  g_fd_account.erase(fd);
}

// 回一个简单消息包（msg 字段携带文本）
static void replyMsg(int fd, MsgType type, const std::string &msg) {
  Packet pkt{};
  pkt.type = type;
  strncpy(pkt.msg, msg.c_str(), sizeof(pkt.msg) - 1);
  pkt.msg[sizeof(pkt.msg) - 1] = '\0';
  send_package(fd, pkt);
}

// 校验该fd是否已登录：已登录返回uid，否则回错误并返回-1
static int requireLogin(int fd) {
  int uid = g_client_mgr.getUidByFd(fd);
  if (uid < 0) {
    replyMsg(fd, MSG_SYSTEM, "please login first");
    return -1;
  }
  return uid;
}

void packet_callback(int fd, const Packet &pkt) {
  std::lock_guard<std::mutex> lk(fdLock(fd));
  std::cout << "收到fd=" << fd << " 的数据包，类型:" << pkt.type << std::endl;

  switch (pkt.type) {
  case MSG_REGISTER: {
    std::string username(pkt.username);
    std::string password(pkt.msg);

    // 用户名已存在则拒绝
    UserModel exist;
    if (db.getUserByUsername(username, exist)) {
      replyMsg(fd, MSG_REGISTER_ACK, "register fail: username exists");
      break;
    }
    bool ret = db.addUser(username.c_str(), password.c_str(), username.c_str());
    replyMsg(fd, MSG_REGISTER_ACK, ret ? "register ok" : "register fail");
    break;
  }

  case MSG_LOGIN: {
    std::string username(pkt.username);
    std::string password(pkt.msg);

    // 同一账号已在线则拒绝（简单防重复登录）
    if (g_client_mgr.getUidByAccount(username) != -1) {
      replyMsg(fd, MSG_LOGIN_ACK, "already online");
      break;
    }
    UserModel user;
    if (!db.getUserByUsername(username, user)) {
      replyMsg(fd, MSG_LOGIN_ACK, "user not found");
      break;
    }
    if (strcmp(user.password.c_str(), password.c_str()) != 0) {
      replyMsg(fd, MSG_LOGIN_ACK, "wrong password");
      break;
    }

    // 登记在线：分配在线uid、标记已登录、业务层记录fd->账号
    int uid = g_client_mgr.registerClient(fd, username);
    g_client_mgr.setLoginState(uid, true);
    setAccountByFd(fd, username);

    std::cout << username << " 登录成功, uid=" << uid << std::endl;
    Packet resp{};
    resp.type = MSG_LOGIN_ACK;
    resp.user_id = uid; // 回给客户端自己的在线id，后续聊天/在线列表都用它
    strncpy(resp.msg, "login ok", sizeof(resp.msg) - 1);
    send_package(fd, resp);

    // 通知其他在线用户该账号上线
    Packet sys{};
    sys.type = MSG_SYSTEM;
    std::string online_msg = username + " online";
    strncpy(sys.msg, online_msg.c_str(), sizeof(sys.msg) - 1);
    for (int tuid : g_client_mgr.getAllOnlineUid()) {
      if (tuid == uid)
        continue; // 不给自己发
      g_client_mgr.sendToUid(tuid, sys);
    }
    break;
  }

  case MSG_CHAT_ALL: {
    int uid = requireLogin(fd);
    if (uid < 0)
      break;
    std::string account = g_client_mgr.getAccountByUid(uid);

    // 服务端用在线身份覆盖发送者字段，防止客户端伪造
    Packet fwd = pkt;
    fwd.type = MSG_CHAT_ALL;
    fwd.user_id = uid;
    memset(fwd.username, 0, sizeof(fwd.username));
    strncpy(fwd.username, account.c_str(), sizeof(fwd.username) - 1);

    for (int tuid : g_client_mgr.getAllOnlineUid()) {
      if (tuid == uid)
        continue; // 不给发送者自己回显
      g_client_mgr.sendToUid(tuid, fwd);
    }
    std::cout << "群聊广播: " << account << " -> " << pkt.msg << std::endl;
    break;
  }

  case MSG_CHAT_ONE: {
    int uid = requireLogin(fd);
    if (uid < 0)
      break;
    std::string account = g_client_mgr.getAccountByUid(uid);

    Packet fwd = pkt;
    fwd.type = MSG_CHAT_ONE;
    fwd.user_id = uid;
    memset(fwd.username, 0, sizeof(fwd.username));
    strncpy(fwd.username, account.c_str(), sizeof(fwd.username) - 1);

    if (!g_client_mgr.sendToUid(fwd.target_id, fwd)) {
      replyMsg(fd, MSG_SYSTEM, "target offline");
    }
    std::cout << "单聊: " << account << " -> uid=" << fwd.target_id << " : "
              << pkt.msg << std::endl;
    break;
  }

  case MSG_ONLINE_LIST: {
    int uid = requireLogin(fd);
    if (uid < 0)
      break;
    replyMsg(fd, MSG_ONLINE_LIST_ACK, g_client_mgr.getOnlineUserList());
    break;
  }

  case MSG_LOGOUT: {
    int uid = g_client_mgr.getUidByFd(fd);
    if (uid >= 0) {
      std::string account = g_client_mgr.getAccountByUid(uid);
      g_client_mgr.removeClient(fd); // 从在线表彻底移除

      // 通知其他在线用户该账号下线（与登录时的 online 广播对称）
      Packet sys{};
      sys.type = MSG_SYSTEM;
      std::string msg = account + " offline";
      strncpy(sys.msg, msg.c_str(), sizeof(sys.msg) - 1);
      for (int tuid : g_client_mgr.getAllOnlineUid()) {
        g_client_mgr.sendToUid(tuid, sys);
      }
      std::cout << "用户登出 fd=" << fd << " uid=" << uid << std::endl;
    }
    eraseAccountByFd(fd);
    replyMsg(fd, MSG_SYSTEM, "logout ok");
    break;
  }

  default:
    std::cout << "未知包类型" << std::endl;
    break;
  }
}

void close_callback(int fd) {
  std::string account = getAccountByFd(fd);
  eraseAccountByFd(fd);

  if (!account.empty()) {
    // 通知其他在线用户该账号下线
    Packet sys{};
    sys.type = MSG_SYSTEM;
    std::string msg = account + " offline";
    strncpy(sys.msg, msg.c_str(), sizeof(sys.msg) - 1);
    for (int tuid : g_client_mgr.getAllOnlineUid()) {
      g_client_mgr.sendToUid(tuid, sys);
    }
  }
  std::cout << "连接断开 fd=" << fd
            << (account.empty() ? "" : " 账号=" + account) << std::endl;
}
