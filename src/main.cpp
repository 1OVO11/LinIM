#include "data/MySQL_dao.h"
#include "network/network.h"
#include "service/service.h"
#include <iostream>

// 全局db对象！service.cpp用extern引用
MySQLDAO db;
// 全局在线会话管理器！service.cpp用extern引用（业务层用来登记/转发/广播）
ClientManager g_client_mgr;

int main() {
  if (!db.init("127.0.0.1", "root", "051011", "smallchat", 3306)) {
    std::cout << "数据库连接失败\n";
    return -1;
  }
  std::cout << "✅ MySQL连接成功，启动IM服务\n";

  // 端口说明：本机 WSL 为 mirrored 网络模式，8888 被 Windows 侧 svchost 占用，
  // 若 bind 失败且无错误检查会静默绑定临时端口；故改用 8088。
  ChatServer server(8088);
  server.setPacketHandler(packet_callback);
  server.setCloseHandler(close_callback);

  server.run();

  db.close();
  return 0;
}
