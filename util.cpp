
#include "util.h"

#include <iostream>
#include <iomanip>
#include <filesystem>

#include <rang.hpp>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>


#define SYSCALL(res, syscall, arg) Util::ckSyscallError(res, syscall, arg)

// consts
static const char sepFilePath = '/';
static const char sepFileExt = '.';

namespace Util {

void runCommand(const std::string &cmd, const std::string &what) {
  int res = system(cmd.c_str());
  if (res == -1)
    std::cerr << "failed to " << what << ": the system error occurred: " << strerror(errno) << std::endl;
  else if (res != 0)
    std::cerr << "failed to " << what << ": the command failed with the exit status " << res << std::endl;

  if (res != 0)
    exit(1);
}

std::string runCommandGetOutput(const std::string &cmd, const std::string &what) {
  // start the command
  FILE *f = ::popen(cmd.c_str(), "r");
  if (f == nullptr) {
    std::cerr << rang::fg::red << "the external command failed (" << cmd << ")" << rang::style::reset << std::endl;
    exit(1);
  }
  // read command's output
  std::ostringstream ss;
  char buf[1025];
  size_t nbytes;
  do {
    nbytes = ::fread(buf, 1, sizeof(buf)-1, f);
    if (nbytes > 0) {
      buf[nbytes] = 0;
      ss << buf;
    }
  } while (nbytes == sizeof(buf)-1);
  // cleanup
  ::fclose(f);
  //
  return ss.str();
}

void ckSyscallError(int res, const char *syscall, const char *arg) {
  if (res == -1) {
    std::cerr << "system call '" << syscall << "' failed, arg=" << arg << ": " << strerror(errno) << std::endl;
    exit(1);
  }
}

std::string tmSecMs() {
  static bool firstTime = true;
  static timeval tpStart;
  if (firstTime) {
    gettimeofday(&tpStart, NULL);
    firstTime = false;
  }

  struct timeval tp;
  gettimeofday(&tp, NULL);

  auto sec = tp.tv_sec - tpStart.tv_sec;
  auto usec = tp.tv_usec - tpStart.tv_usec;
  if (usec < 0) {
    sec--;
    usec += 1000000;
  }

  return STR(sec << "." << std::setw(3) << std::setfill('0') << usec/1000);
}

std::string filePathToBareName(const std::string &path) {
  auto i = path.rfind(sepFilePath);
  std::string p = (i != std::string::npos ? path.substr(i + 1) : path);
  i = p.find(sepFileExt);
  return i != std::string::npos ? p.substr(0, i) : p;
}

namespace Fs {

namespace fs = std::filesystem;

void unlink(const std::string &file) {
  auto res = ::unlink(file.c_str());
  if (res == -1 && errno == EPERM) { // this unlink function clears the schg extended flag in case of EPERM, because in our context EPERM often indicates schg
    SYSCALL(::chflags(file.c_str(), 0/*flags*/), "chflags", file.c_str());
    SYSCALL(::unlink(file.c_str()), "unlink (2)", file.c_str()); // repeat the unlink call
    return;
  }
  SYSCALL(res, "unlink (1)", file.c_str());
}

void rmdir(const std::string &dir) {
  auto res = ::rmdir(dir.c_str());
  if (res == -1 && errno == EPERM) { // this rmdir function clears the schg extended flag in case of EPERM, because in our context EPERM often indicates schg
    SYSCALL(::chflags(dir.c_str(), 0/*flags*/), "chflags", dir.c_str());
    SYSCALL(::rmdir(dir.c_str()), "rmdir (2)", dir.c_str()); // repeat the rmdir call
    return;
  }
  SYSCALL(res, "rmdir (1)", dir.c_str());
}

void rmdirFlat(const std::string &dir) {
  for (const auto &entry : fs::directory_iterator(dir))
    unlink(entry.path());
  rmdir(dir);
}

void rmdirHier(const std::string &dir) {
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_symlink())
      unlink(entry.path());
    else if (entry.is_directory())
      rmdirHier(entry.path());
    else
      unlink(entry.path());
  }
  rmdir(dir);
}

bool rmdirFlatExcept(const std::string &dir, const std::set<std::string> &except) {
  bool someSkipped = false;
  for (const auto &entry : fs::directory_iterator(dir))
    if (except.find(entry.path()) == except.end())
      unlink(entry.path());
    else
      someSkipped = true;
  if (!someSkipped)
    rmdir(dir);
  return someSkipped;
}

bool rmdirHierExcept(const std::string &dir, const std::set<std::string> &except) {
  unsigned cntSkip = 0;
  for (const auto &entry : fs::directory_iterator(dir))
    if (except.find(entry.path()) == except.end()) {
      if (entry.is_symlink())
        unlink(entry.path());
      else if (entry.is_directory())
        cntSkip += rmdirHierExcept(entry.path(), except);
      else
        unlink(entry.path());
    } else {
      cntSkip++;
    }
  if (cntSkip == 0)
    rmdir(dir);
  return cntSkip > 0;
}

}

}
