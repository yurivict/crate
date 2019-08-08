// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "cmd.h"
#include "util.h"


namespace Cmd {

const std::string xz = STRg("xz --threads=" << Util::getSysctlInt("hw.ncpu"));
std::string chroot(const std::string &path) {
  return STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << path << " ");
}

}
