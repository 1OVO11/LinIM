# LinIM —— Linux 终端即时通讯系统

基于 **epoll + 线程池 + MySQL** 的 C++11 即时通讯系统，含服务端与终端客户端，支持注册登录、群聊、单聊、在线列表与上下线广播。

## 功能特性

- 注册 / 登录 / 防重复登录
- 群聊广播（直接输入即群聊）
- 单聊（`/to 账号|uid 内容`）
- 在线列表查询（`/online`）
- 上线 / 下线广播（`xx online` / `xx offline`）
- 登出（`/quit`）

## 技术栈与架构

```
┌─────────────────────────┐      TCP/174字节协议      ┌─────────────────────┐
│  linim_client (终端)    │ ────────────────────────▶ │  linim_server        │
│  select 监听 stdin+fd   │ ◀──────────────────────── │  epoll + 线程池      │
└─────────────────────────┘                          │  service 业务层      │
                                                     │  ClientManager 在线表 │
                                                     └──────────┬──────────┘
                                                                │ MySQL (smallchat)
```

- **网络层**：epoll 非阻塞 IO，单线程读包 + 线程池处理业务；4 字节长度头拆包，处理粘包/半包
- **业务层**：`packet_callback` 按消息类型分发；per-fd 锁保证同一连接消息按序处理
- **在线管理**：全局 `ClientManager`（fd↔uid↔账号 三向映射），登录登记、断线清理
- **协议**：手工大端序编解码（非 struct 内存布局），174 字节/包，避免对齐陷阱

## 环境要求

- WSL Ubuntu（或 Linux），g++ 支持 C++11
- cmake ≥ 3.10
- MySQL 8.x（服务端依赖），需安装 `libmysqlclient-dev`

```bash
sudo apt install -y g++ cmake mysql-server libmysqlclient-dev
```

## 快速开始

```bash
# 1. 初始化数据库（建库建表）
mysql -uroot -p < database.sql

# 2. 编译
cmake -S . -B build
cmake --build build -j4
# 产物: build/linim_server  build/linim_client

# 3. 运行服务端（默认 127.0.0.1:8088）
./build/linim_server

# 4. 打开两个终端各起一个客户端
./build/linim_client
# 菜单选注册/登录，登录后：
#   直接输入        = 群聊
#   /to 账号|uid 内容 = 单聊
#   /online         = 在线列表
#   /quit           = 退出
```

> 服务端数据库连接参数在 `src/data/MySQL_dao.cpp` 中配置（默认 127.0.0.1 / root / 051011 / smallchat），按需修改。

## 协议说明（174 字节/包）

| 偏移 | 大小 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | length | 整包长度（网络字节序） |
| 4 | 2 | type | 消息类型 |
| 6 | 4 | user_id | 用户在线 uid |
| 10 | 4 | target_id | 单聊目标 uid |
| 14 | 32 | username | 账号 |
| 46 | 128 | msg | 消息内容 |

| 类型值 | 含义 |
|---|---|
| 0/1 | 注册 / 注册应答 |
| 2/3 | 群聊 / 单聊 |
| 4 | 登出 |
| 5 | 系统消息（target offline、xx online/offline 广播） |
| 6/7 | 登录 / 登录应答 |
| 8/9 | 查询在线列表 / 在线列表应答（msg 携带 `id=uid 账号`） |

> 注意：`uid` 是**会话级**编号（服务端启动后自增），不是数据库 id，服务端重启会重置。在线列表/单聊均基于 uid。

## 目录结构

```
LinIM/
├── CMakeLists.txt      # 双目标构建: linim_server + linim_client
├── database.sql        # 建库建表脚本
├── common/             # 公共协议层 protocol.h/cpp
├── src/
│   ├── data/           # MySQL DAO（user / friend_relation / message）
│   ├── network/        # epoll 网络层 + ClientManager + 线程池
│   ├── service/        # 业务逻辑（注册/登录/聊天/在线/登出）
│   └── main.cpp
└── client/             # 终端客户端 client.cpp
```

## 踩坑与修复记录

改造与调试过程中修复了 7 个真实 bug（幽灵在线、线程池 race、消息丢失竞态、数字账号单聊失效、在线列表快照过期等）：
1. 双 ClientManager 实例导致的幽灵在线
2. 断线清理与线程池任务的并发竞态
3. 客户端缓存在线列表快照过期

## 后续规划

- 离线消息（message 表已建）
- 好友系统（friend_relation 表已建）
- 心跳 / 断线重连
- 消息确认 / 已读
- 客户端 GUI


