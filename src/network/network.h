#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "../../common/protocol.h"
#include "client_manager.h"
#include "threadpool.h"
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// 两个回调：收到数据包回调、连接断开回调
using PacketHandler = std::function<void(int fd, const Packet &pkt)>;
using CloseHandler = std::function<void(int fd)>;

// 全局在线会话管理器（main.cpp 定义）。
// 网络层与业务层必须共享同一个实例，否则"登录登记"和"断开清理"会操作
// 两份不同的在线表，导致断开后用户无法被清理（幽灵在线）。
extern ClientManager g_client_mgr;

class ChatServer {
public:
  ChatServer(int port, int thread_num = 4); // 构造函数，初始化端口和线程池大小
  ~ChatServer();

  // 注册上层回调函数，网络层收到包或者连接关闭时调用业务层
  void setPacketHandler(PacketHandler handler); // 设置收到数据包的回调函数
  void setCloseHandler(CloseHandler handler);   // 设置连接关闭的回调函数

  void run(); // 启动服务器，开始监听和处理连接

private:
  int m_epoll_fd;             // epoll文件描述符
  int m_listen_fd;            // 监听套接字文件描述符
  int m_port;                 // 服务器监听的端口号
  ThreadPool m_pool;          // 线程池，用于处理客户端请求

  PacketHandler m_packet_handler; // 收到数据包的回调函数
  CloseHandler m_close_handler;   // 连接关闭的回调函数

  // 单连接缓冲区
  struct ConnBuffer {
    char buf[4096]; // ET 模式要求一次把数据读完，缓冲加大避免满缓冲卡死
    int len;
    ConnBuffer() : len(0) {
      memset(buf, 0, sizeof(buf));
    } // 初始化缓冲区长度为0，并将缓冲区清零
  };
  std::unordered_map<int, ConnBuffer> m_conn_buf;

  // 已关闭的fd集合：线程池任务执行前检查，防止"连接已断开但任务仍登记"的幽灵在线
  std::unordered_set<int> m_closed_fds;
  std::mutex m_closed_mtx;
  bool isFdClosed(int fd); // 线程池任务侧查询

  void acceptConn();       // 接受新的客户端连接
  void handleRead(int fd); // 处理客户端的读事件，读取数据包并调用回调函数
  void
  addEpoll(int fd,
           uint32_t events); // 将文件描述符添加到epoll实例中，并设置事件类型
  void delEpoll(int fd);     // 将文件描述符从epoll实例中删除
};

#endif