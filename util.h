#pragma once

#include <string>
#include <set>
#include <iostream>
#include <sstream>

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <rang.hpp>

//
// utility macros useful throughout the program
//

#define STR(msg...) \
  ([=]() { \
    std::ostringstream ss; \
    ss << msg; \
    return ss.str(); \
  }())
#define CSTR(msg...) (STR(msg).c_str())

#define ERR2(loc, msg...) \
  { \
    std::cerr << rang::fg::red << loc << ": " << msg << rang::style::reset << std::endl; \
    exit(1); \
  }

#define WARN(msg...) \
  std::cerr << rang::fg::yellow << msg << rang::style::reset << std::endl;

template<typename T>
inline std::ostream& operator<<(std::ostream &os, const std::vector<T> &v) {
  bool fst = true;
  for (auto &e : v) {
    if (fst)
      fst = false;
    else
      os << " ";
    os << e;
  }
  return os;
}

template<typename T>
inline std::vector<T> operator+(const std::vector<T> &v1, const std::vector<T> &v2) {
  std::vector<T> res = v1;
  res.insert(res.end(), v2.begin(), v2.end());
  return res;
}

//
// utility functions
//

namespace Util {

void runCommand(const std::string &cmd, const std::string &what);
std::string runCommandGetOutput(const std::string &cmd, const std::string &what);
void ckSyscallError(int res, const char *syscall, const char *arg);
std::string tmSecMs();
std::string filePathToBareName(const std::string &path);
std::string filePathToFileName(const std::string &path);

namespace Fs {

bool fileExists(const std::string &path);
void chown(const std::string &path, uid_t owner, gid_t group);
void unlink(const std::string &file);
void mkdir(const std::string &dir, mode_t mode);
void rmdir(const std::string &dir);
void rmdirFlat(const std::string &dir);
void rmdirHier(const std::string &dir);
bool rmdirFlatExcept(const std::string &dir, const std::set<std::string> &except);
bool rmdirHierExcept(const std::string &dir, const std::set<std::string> &except);
bool isXzArchive(const char *file);
char isElfFileOrDir(const std::string &file); // returns 'E'LF, 'D'ir, or 'N'o
std::set<std::string> findElfFiles(const std::string &dir);
bool hasExtension(const char *file, const char *extension);

}

}
