// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "mount.h"
#include "util.h"
#include "err.h"

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <vector>

#define ERR(msg...) ERR2("mount/unmount directories", msg)

Mount::Mount(const char *newFstype, const std::string &newFspath, const std::string &newTarget)
: fstype(newFstype), fspath(newFspath), target(newTarget)
{ }

Mount::~Mount() {
  if (mounted)
    unmount(false/*doThrow*/); // called after some failure: it will only warn about the failed mount, and the destructor would continue
}

void Mount::mount() {
  std::vector<struct iovec> iov;
  auto param = [&iov](const char *name, void *val, size_t len) {
    auto i = iov.size();
    iov.resize(i + 2);
    iov[i].iov_base = ::strdup(name);
    iov[i].iov_len = ::strlen(name) + 1;
    i++;
    iov[i].iov_base = val;
    iov[i].iov_len = len == (size_t)-1 ?
                       val != nullptr ?
                         strlen((const char*)val) + 1
                         :
                         0
                       :
                       len;
  };
  char errmsg[255] = {0};
  errmsg[0] = '\0';
  param("fstype", (void*)fstype,         (size_t)-1);
  param("fspath", (void*)fspath.c_str(), (size_t)-1);
  if (!target.empty())
    param("target", (void*)target.c_str(), (size_t)-1);
  param("errmsg", errmsg,                sizeof(errmsg));
  int res = ::nmount(&iov[0], iov.size(), 0/*flags*/);
  if (res != 0)
    ERR("nmount of '" << target << "' on '" << fspath << "' failed: " << strerror(errno) << (errmsg[0] ? STR(" (" << errmsg << ")") : ""))
  mounted = true;

  for (unsigned i = 0; i < iov.size(); i += 2)
    ::free(iov[i].iov_base);
}

void Mount::unmount(bool doThrow) {
  int res = ::unmount(fspath.c_str(), 0/*flags*/);
  if (res == -1)
    ERR("unmount of '" << fspath << "' failed: " << strerror(errno));
  mounted = false;
}
