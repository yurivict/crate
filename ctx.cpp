// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "ctx.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include <iostream>

#include "util.h"
#include "err.h"
#include "locs.h"

#define ERR(msg...) ERR2("managing context info", msg)

namespace Ctx {

FwUsers::FwUsers()
: fd(-1), inMemory(false), changed(false)
{ }

FwUsers::~FwUsers() {
  if (fd != -1)
    WARN("closing file on destructor: " << file())
  if (fd != -1 && ::close(fd) == -1) // unlock() wasn't called: must be an erorr condition
    WARN("unable to close the file " << file() << ": " << strerror(errno))
}

FwUsers* FwUsers::lock() {
  FwUsers *ctx = new FwUsers;
  // open and lock the file
  ctx->fd = ::open(file().c_str(), O_RDWR|O_CREAT|O_EXLOCK, 0600);
  if (ctx->fd == -1)
    ERR("failed to open file " << file() << ": " << strerror(errno))
  return ctx;
}

void FwUsers::unlock() {
  // write if the content has changed
  if (changed)
    writeToFile();
  // close
  if (::close(fd) == -1) {
    fd = -1;
    ERR("failed to close file " << file() << ": " << strerror(errno))
  }
  fd = -1;
}

bool FwUsers::isEmpty() const {
  if (inMemory)
    return pids.empty();
  else
    return Util::Fs::getFileSize(fd) == 0;
}

void FwUsers::add(pid_t pid) {
  if (inMemory) {
    pids.insert(int(pid));
    changed = true;
  } else {
    if (::lseek(fd, 0, SEEK_END) == -1)
      ERR("failed to seek in file " << file() << ": " << strerror(errno))
    Util::Fs::writeFile(STR(pid << std::endl), fd);
  }
}

void FwUsers::del(pid_t pid) {
  if (!inMemory)
    readIntoMemory();
  pids.erase(pids.find(pid));
  changed = true;
}

/// internals

std::string FwUsers::file() {
  return Locations::ctxFwUsersFilePath;
}

void FwUsers::readIntoMemory() {
  for (auto const &line : Util::Fs::readFileLines(fd))
    pids.insert(std::stoul(line));
  inMemory = true;
}

void FwUsers::writeToFile() const {
  // form the file content
  std::ostringstream ss;
  for (auto pid : pids)
    ss << pid << std::endl;
  // write
  if (::ftruncate(fd, 0) == -1)
    ERR("failed to truncate file " << file() << ": " << strerror(errno) << " (fd=" << fd << ")")
  if (::lseek(fd, 0, SEEK_SET) == -1)
    ERR("failed to seek in file " << file() << ": " << strerror(errno) << " (fd=" << fd << ")")
  Util::Fs::writeFile(ss.str(), fd);
}

}
