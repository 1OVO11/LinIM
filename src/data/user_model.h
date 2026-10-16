#ifndef USER_MODEL_H
#define USER_MODEL_H
#include <string>

struct UserModel {
  int id;
  std::string username;
  std::string password;
  std::string nickname;
};
#endif
