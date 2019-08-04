// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#pragma once

#include <unistd.h>

#include <set>
#include <string>

namespace Ctx {

class FwUsers {
  int fd; // lock
  std::set<pid_t> pids;
  bool inMemory;
  bool changed;
  FwUsers();
public:
  ~FwUsers();
  static FwUsers* lock();       // lock, open, and read
  void unlock();            // unlock, close, and possibly save
  bool isEmpty() const;
  void add(pid_t pid);
  void del(pid_t pid);
private:
  static std::string file();
  void readIntoMemory();
  void writeToFile() const;
};

}
