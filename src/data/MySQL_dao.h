#ifndef MYSQL_DAO_H
#define MYSQL_DAO_H
#include "user_model.h"
#include <mysql/mysql.h>
#include <string>

class MySQLDAO {
public:
  MySQLDAO();
  ~MySQLDAO();
  bool init(const std::string &host, const std::string &user,
            const std::string &pwd, const std::string &db, int port = 3306);

  bool getUserByUsername(const std::string &username, UserModel &out_user);
  bool addUser(const std::string &username, const std::string &password,
               const std::string &nickname);

  void close();

private:
  MYSQL *mysql_;
};
#endif
