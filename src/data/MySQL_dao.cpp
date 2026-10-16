#include "MySQL_dao.h"
#include <cstring>
#include <iostream>

MySQLDAO::MySQLDAO() : mysql_(nullptr) {}
MySQLDAO::~MySQLDAO() { close(); }

bool MySQLDAO::init(const std::string &host, const std::string &user,
                    const std::string &pwd, const std::string &db, int port) {
  mysql_ = mysql_init(nullptr);
  if (!mysql_)
    return false;
  if (!mysql_real_connect(mysql_, host.c_str(), user.c_str(), pwd.c_str(),
                          db.c_str(), port, nullptr, 0)) {
    std::cerr << "mysql connect err: " << mysql_error(mysql_) << std::endl;
    return false;
  }
  mysql_set_character_set(mysql_, "utf8mb4");
  return true;
}

bool MySQLDAO::getUserByUsername(const std::string &username,
                                 UserModel &out_user) {
  char sql[512];
  sprintf(sql,
          "SELECT id,username,password,nickname FROM user WHERE username='%s'",
          username.c_str());
  if (mysql_query(mysql_, sql)) {
    std::cerr << "query err:" << mysql_error(mysql_) << std::endl;
    return false;
  }
  MYSQL_RES *res = mysql_store_result(mysql_);
  if (!res)
    return false;
  MYSQL_ROW row = mysql_fetch_row(res);
  if (!row) {
    mysql_free_result(res);
    return false;
  }
  out_user.id = atoi(row[0]);
  out_user.username = row[1];
  out_user.password = row[2];
  out_user.nickname = row[3];
  mysql_free_result(res);
  return true;
}

bool MySQLDAO::addUser(const std::string &username, const std::string &password,
                       const std::string &nickname) {
  char sql[512];
  sprintf(sql,
          "INSERT INTO user(username,password,nickname) VALUES('%s','%s','%s')",
          username.c_str(), password.c_str(), nickname.c_str());
  if (mysql_query(mysql_, sql)) {
    std::cerr << "insert err:" << mysql_error(mysql_) << std::endl;
    return false;
  }
  return true;
}

void MySQLDAO::close() {
  if (mysql_) {
    mysql_close(mysql_);
    mysql_ = nullptr;
  }
}
