#include "network.h"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

static int setNonBlock(int fd) {
  int flag = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flag | O_NONBLOCK);
  return 0;
}

ChatServer::ChatServer(int port, int thread_num)
    : m_port(port), m_pool(thread_num) { // 构造函数，初始化端口和线程池大小

  signal(SIGPIPE,
         SIG_IGN); // 忽略SIGPIPE信号，防止写入已关闭的socket导致程序退出

  // 创建epoll
  m_epoll_fd = epoll_create1(0);

  // 创建监听socket
  m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  int opt = 1;
  setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt,
             sizeof(opt)); // 设置SO_REUSEADDR选项，允许地址重用

  setNonBlock(m_listen_fd); // 设置监听socket为非阻塞模式

  // 初始化服务器地址结构
  sockaddr_in serv_addr{};
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(m_port);

  bind(m_listen_fd, (sockaddr *)&serv_addr,
       sizeof(serv_addr)); // 绑定监听socket到指定端口

  listen(m_listen_fd, 128); // 开始监听连接请求，设置最大连接队列长度为128

  addEpoll(m_listen_fd, EPOLLIN); // 将监听socket添加到epoll实例中，监听可读事件
}

// 析构函数，关闭epoll和监听socket
ChatServer::~ChatServer() {
  close(m_epoll_fd);
  close(m_listen_fd);
}

// 设置收到数据包的回调函数
void ChatServer::setPacketHandler(PacketHandler handler) {
  m_packet_handler = std::move(handler);
}

// 设置连接关闭的回调函数
void ChatServer::setCloseHandler(CloseHandler handler) {
  m_close_handler = std::move(handler);
}

// 将文件描述符添加到epoll实例中，并设置事件类型
void ChatServer::addEpoll(int fd, uint32_t events) {
  epoll_event ev{};
  ev.data.fd = fd;
  ev.events = events;
  epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

// 线程池任务侧查询：该fd是否已关闭（防止任务在断线后误登记幽灵在线）
bool ChatServer::isFdClosed(int fd) {
  std::lock_guard<std::mutex> lock(m_closed_mtx);
  return m_closed_fds.count(fd) > 0;
}

// 将文件描述符从epoll实例中删除
void ChatServer::delEpoll(int fd) {
  epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
  m_conn_buf.erase(fd);
  g_client_mgr.removeClient(fd);
  if (m_close_handler) {
    m_close_handler(fd);
  }
}

// 接受新的客户端连接
void ChatServer::acceptConn() {
  sockaddr_in client_addr{};
  socklen_t sock_len = sizeof(client_addr);
  int client_fd =
      accept4(m_listen_fd, (sockaddr *)&client_addr, &sock_len, SOCK_NONBLOCK);
  if (client_fd < 0) {
    return;
  }
  // 该fd可能复用自刚关闭的连接，清除其"已关闭"标记，避免新连接的数据被误丢弃
  {
    std::lock_guard<std::mutex> lock(m_closed_mtx);
    m_closed_fds.erase(client_fd);
  }
  // 客户端连接使用边沿触发 ET：状态变化时通知一次，必须循环读直到 EAGAIN
  addEpoll(client_fd, EPOLLIN | EPOLLET);
}

// 处理客户端的读事件，读取数据包并调用回调函数
// ET 模式下事件只通知一次，必须循环 read 直到 EAGAIN，否则剩余数据无人处理
void ChatServer::handleRead(int fd) {
  auto &conn_buf = m_conn_buf[fd];
  while (true) {
    // 缓冲已满仍无可拆完整包（协议包 174 字节远小于 4096，正常不会发生），
    // 暂停读取等待下一次事件，避免死循环
    if (conn_buf.len == (int)sizeof(conn_buf.buf)) {
      break;
    }
    int n = read(fd, conn_buf.buf + conn_buf.len,
                 sizeof(conn_buf.buf) - conn_buf.len);
    if (n > 0) {
      conn_buf.len += n;

      // 循环拆包
      while (conn_buf.len >= HEAD_SIZE) {
        Packet pkt;
        int ret = decode(conn_buf.buf, conn_buf.len, pkt);
        if (ret <= 0)
          break;

        // 解析成功，提交任务到线程池，调用上层回调
        m_pool.Submit([this, fd, pkt]() {
          if (isFdClosed(fd)) {
            return; // 连接已断开，丢弃该包，避免业务层误登记
          }
          if (m_packet_handler) {
            m_packet_handler(fd, pkt);
          }
        });
        // 缓冲区前移
        memmove(conn_buf.buf, conn_buf.buf + ret, conn_buf.len - ret);
        conn_buf.len -= ret;
      }
      continue; // ET 下继续读，直到读干净
    }

    if (n == 0) {
      // 对端关闭：标记该fd已关闭，再清理。标记要赶在队列中的
      // 任务执行前，否则任务会把已关闭的fd重新登记为在线（幽灵在线）。
      {
        std::lock_guard<std::mutex> lock(m_closed_mtx);
        m_closed_fds.insert(fd);
      }
      delEpoll(fd);
      return;
    }

    // n < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break; // 数据已读完（ET 模式收尾）
    // 其他错误：按断开处理
    {
      std::lock_guard<std::mutex> lock(m_closed_mtx);
      m_closed_fds.insert(fd);
    }
    delEpoll(fd);
    return;
  }
}
// 启动服务器，开始监听和处理连接
void ChatServer::run() {
  epoll_event events[128];
  while (true) {
    int n_ready = epoll_wait(m_epoll_fd, events, 128, -1);
    for (int i = 0; i < n_ready; i++) {
      int fd = events[i].data.fd;
      if (fd == m_listen_fd) {
        acceptConn();
      } else {
        if (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
          handleRead(fd);
        }
      }
    }
  }
}