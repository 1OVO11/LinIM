-- ============================================================
-- LinIM 数据库初始化脚本
-- 使用: mysql -uroot -p < database.sql
-- 依赖: MySQL 8.x
-- ============================================================

CREATE DATABASE IF NOT EXISTS `smallchat`
  DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

USE `smallchat`;

-- ------------------------------------------------------------
-- 用户表
--   id       自增主键
--   username 登录账号（唯一）
--   password 密码（当前明文存储，后续可改哈希）
--   nickname 昵称
-- ------------------------------------------------------------
DROP TABLE IF EXISTS `user`;
CREATE TABLE `user` (
  `id`       INT NOT NULL AUTO_INCREMENT,
  `username` VARCHAR(50)  NOT NULL,
  `password` VARCHAR(100) NOT NULL,
  `nickname` VARCHAR(50)  NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `username` (`username`)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci;

-- ------------------------------------------------------------
-- 好友关系表（当前业务未接，为后续功能预留）
--   user_id   用户id
--   friend_id 好友id
-- ------------------------------------------------------------
DROP TABLE IF EXISTS `friend_relation`;
CREATE TABLE `friend_relation` (
  `id`        INT NOT NULL AUTO_INCREMENT,
  `user_id`   INT NOT NULL,
  `friend_id` INT NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_user_friend` (`user_id`, `friend_id`)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci;

-- ------------------------------------------------------------
-- 消息表（当前业务未接，为离线消息功能预留）
--   sender_id   发送者id
--   receiver_id 接收者id
--   content     消息内容
--   read_flag   0=未读 1=已读
-- ------------------------------------------------------------
DROP TABLE IF EXISTS `message`;
CREATE TABLE `message` (
  `id`          INT NOT NULL AUTO_INCREMENT,
  `sender_id`   INT NOT NULL,
  `receiver_id` INT NOT NULL,
  `content`     TEXT NOT NULL,
  `read_flag`   TINYINT DEFAULT 0,
  PRIMARY KEY (`id`)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_0900_ai_ci;

-- ============================================================
-- 完成。服务端连接参数: 127.0.0.1:3306 root/<你的密码> smallchat
-- ============================================================
