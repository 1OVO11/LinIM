#include "protocol.h"
#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <signal.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8088

// ---------- 全局状态 ----------
static std::map<int, std::string> g_online; // uid -> 账号（在线列表）
static int g_uid = -1;                      // 自己的在线uid
static std::string g_account;               // 自己的账号

// ---------- stdin 逐字符读一行（复用旧客户端写法，避免粘贴吞行） ----------
static ssize_t read_line(char *buf, size_t size) {
  size_t i = 0;
  while (i < size - 1) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
      if (i == 0)
        return n;
      break;
    }
    if (c == '\n')
      break;
    if (c == '\r')
      continue; // 忽略回车符，兼容 Windows CRLF 换行
    buf[i++] = c;
  }
  buf[i] = '\0';
  return (ssize_t)i;
}

// ---------- 组包发送 ----------
static bool send_pkt(int fd, MsgType type, int user_id, int target_id,
                     const std::string &username, const std::string &msg) {
  Packet pkt{};
  pkt.type = type;
  pkt.user_id = user_id;
  pkt.target_id = target_id;
  strncpy(pkt.username, username.c_str(), sizeof(pkt.username) - 1);
  strncpy(pkt.msg, msg.c_str(), sizeof(pkt.msg) - 1);
  return send_package(fd, pkt) > 0;
}

// ---------- 解析在线列表 "id=1 alice id=2 bob " ----------
static void parse_online_list(const char *msg) {
  g_online.clear();
  const char *p = msg;
  while (*p) {
    if (strncmp(p, "id=", 3) == 0) {
      p += 3;
      int uid = atoi(p);
      while (*p && *p != ' ')
        ++p;
      while (*p == ' ')
        ++p;
      char acc[64] = {0};
      int i = 0;
      while (*p && *p != ' ' && i < 63)
        acc[i++] = *p++;
      if (uid > 0 && acc[0])
        g_online[uid] = acc;
    } else {
      ++p;
    }
  }
}

// ---------- 处理收到的完整包 ----------
static void handle_packet(int fd, const Packet &pkt) {
  switch (pkt.type) {
  case MSG_REGISTER_ACK:
    std::cout << ">> " << pkt.msg << std::endl;
    break;
  case MSG_LOGIN_ACK:
    if (strstr(pkt.msg, "ok")) {
      g_uid = pkt.user_id;
      std::cout << ">> " << pkt.msg << " (我的uid=" << g_uid << ")"
                << std::endl;
      // 登录成功自动拉一次在线列表
      send_pkt(fd, MSG_ONLINE_LIST, g_uid, 0, "", "");
    } else {
      std::cout << ">> " << pkt.msg << std::endl;
    }
    break;
  case MSG_SYSTEM:
    std::cout << "[系统] " << pkt.msg << std::endl;
    // 收到上线/下线广播时自动刷新在线列表：本地 g_online 是快照，
    // 不刷新的话，后上线的用户在其他客户端本地永远"查不到"。
    if (strstr(pkt.msg, " online") || strstr(pkt.msg, " offline")) {
      send_pkt(fd, MSG_ONLINE_LIST, g_uid, 0, "", "");
    }
    break;
  case MSG_ONLINE_LIST_ACK:
    parse_online_list(pkt.msg);
    std::cout << "[在线 " << g_online.size() << " 人]" << std::endl;
    for (auto &kv : g_online) {
      std::cout << "  uid=" << kv.first << "  " << kv.second << std::endl;
    }
    break;
  case MSG_CHAT_ALL:
    std::cout << "[群聊] " << pkt.username << ": " << pkt.msg << std::endl;
    break;
  case MSG_CHAT_ONE:
    std::cout << "[私聊] " << pkt.username << ": " << pkt.msg << std::endl;
    break;
  default:
    break;
  }
}

// ---------- 接收缓冲 + 循环拆包 ----------
static char g_recv_buf[4096];
static int g_recv_len = 0;

static void process_recv(int fd) {
  int n = read(fd, g_recv_buf + g_recv_len, sizeof(g_recv_buf) - g_recv_len);
  if (n <= 0) {
    std::cout << "\n连接已断开" << std::endl;
    exit(0);
  }
  g_recv_len += n;
  while (g_recv_len >= HEAD_SIZE) {
    Packet pkt;
    int ret = decode(g_recv_buf, g_recv_len, pkt);
    if (ret <= 0)
      break;
    handle_packet(fd, pkt);
    memmove(g_recv_buf, g_recv_buf + ret, g_recv_len - ret);
    g_recv_len -= ret;
  }
}

// ---------- 登录 / 注册（同步等一个 ack） ----------
// 返回 1 成功，0 失败（业务失败），-1 网络错误
static int do_login_or_register(int fd, bool do_register) {
  char username[32] = {0};
  char password[128] = {0};
  std::cout << "账号: ";
  std::cout.flush();
  if (read_line(username, sizeof(username)) <= 0)
    return -1;
  std::cout << "密码: ";
  std::cout.flush();
  if (read_line(password, sizeof(password)) <= 0)
    return -1;

  g_account = username;
  MsgType t = do_register ? MSG_REGISTER : MSG_LOGIN;
  if (!send_pkt(fd, t, 0, 0, username, password))
    return -1;

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  struct timeval tv{3, 0};
  if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
    return -1;

  int n = read(fd, g_recv_buf, sizeof(g_recv_buf));
  if (n <= 0)
    return -1;
  g_recv_len = n;
  while (g_recv_len >= HEAD_SIZE) {
    Packet pkt;
    int ret = decode(g_recv_buf, g_recv_len, pkt);
    if (ret <= 0)
      return -1;
    if (pkt.type == MSG_LOGIN_ACK || pkt.type == MSG_REGISTER_ACK) {
      handle_packet(fd, pkt);
      memmove(g_recv_buf, g_recv_buf + ret, g_recv_len - ret);
      g_recv_len -= ret;
      return strstr(pkt.msg, "ok") ? 1 : 0;
    }
    memmove(g_recv_buf, g_recv_buf + ret, g_recv_len - ret);
    g_recv_len -= ret;
  }
  return 0;
}

// ---------- 处理一条命令 ----------
static void handle_command(int fd, const char *line) {
  if (strncmp(line, "/quit", 5) == 0) {
    send_pkt(fd, MSG_LOGOUT, g_uid, 0, g_account, "");
    std::cout << "已退出" << std::endl;
    // 优雅退出：给服务端留时间处理此前排队的消息（如刚发的单聊），
    // 避免 socket 一关，服务端就把本连接从在线表移除、丢弃队列中的任务。
    usleep(300000);
  } else if (strncmp(line, "/online", 7) == 0) {
    send_pkt(fd, MSG_ONLINE_LIST, g_uid, 0, "", "");
  } else if (strncmp(line, "/to ", 4) == 0) {
    char target[64] = {0};
    char content[128] = {0};
    const char *p = line + 4;
    int i = 0;
    while (*p && *p != ' ' && i < 63)
      target[i++] = *p++;
    while (*p == ' ')
      ++p;
    strncpy(content, p, sizeof(content) - 1);

    int target_uid = -1;
    // 1) 优先按账号精确匹配在线列表（账号可能是纯数字，如 "222"）
    for (auto &kv : g_online) {
      if (kv.second == target) {
        target_uid = kv.first;
        break;
      }
    }
    // 2) 账号未匹配且是纯数字 → 按 uid 解析，并校验确实在线
    if (target_uid <= 0 && isdigit(target[0])) {
      int uid = atoi(target);
      if (g_online.count(uid) > 0)
        target_uid = uid;
    }
    if (target_uid <= 0) {
      std::cout << "[提示] 目标不在线或不存在: " << target << std::endl;
    } else if (!content[0]) {
      std::cout << "[提示] 用法: /to <账号|uid> <内容>" << std::endl;
    } else {
      send_pkt(fd, MSG_CHAT_ONE, g_uid, target_uid, g_account, content);
      std::cout << "[我 -> " << target << "] " << content << std::endl;
    }
  } else if (line[0]) {
    send_pkt(fd, MSG_CHAT_ALL, g_uid, 0, g_account, line);
  }
}

int main(int argc, char *argv[]) {
  signal(SIGPIPE, SIG_IGN);

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SERVER_PORT);
  if (inet_pton(AF_INET, SERVER_IP, &addr.sin_addr) <= 0) {
    perror("inet_pton");
    return -1;
  }
  if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("connect");
    return -1;
  }
  std::cout << "已连接服务器 " << SERVER_IP << ":" << SERVER_PORT << std::endl;

  // 启动菜单
  char choice[16] = {0};
  while (true) {
    std::cout << "1-登录  2-注册: ";
    std::cout.flush();
    if (read_line(choice, sizeof(choice)) <= 0)
      return 0;
    int sel = atoi(choice);
    if (sel == 1) {
      break;
    } else if (sel == 2) {
      int r = do_login_or_register(fd, true);
      if (r < 0) {
        std::cout << "网络错误，退出" << std::endl;
        return 0;
      }
      if (r == 0) {
        std::cout << "注册未成功，请重试" << std::endl;
        continue;
      }
      std::cout << "注册成功，请登录" << std::endl;
      break;
    } else {
      std::cout << "无效选择" << std::endl;
    }
  }

  // 登录
  int r = do_login_or_register(fd, false);
  if (r < 0) {
    std::cout << "网络错误，退出" << std::endl;
    return 0;
  }
  if (r == 0) {
    std::cout << "登录未成功，退出" << std::endl;
    return 0;
  }

  std::cout << "\n======== 聊天模式 ========\n"
            << "直接输入        = 群聊\n"
            << "/to 账号|uid 内容 = 单聊\n"
            << "/online         = 在线列表\n"
            << "/quit           = 退出\n"
            << "==========================" << std::endl;

  // 主循环：select 同时监听 stdin 和 socket
  char line[512];
  while (true) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(fd, &rfds);
    int maxfd = fd > STDIN_FILENO ? fd : STDIN_FILENO;
    if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (FD_ISSET(fd, &rfds)) {
      process_recv(fd);
    }
    if (FD_ISSET(STDIN_FILENO, &rfds)) {
      ssize_t n = read_line(line, sizeof(line));
      if (n <= 0)
        break;
      handle_command(fd, line);
      if (strncmp(line, "/quit", 5) == 0)
        break;
    }
  }

  close(fd);
  return 0;
}
