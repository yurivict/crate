// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "cmd.h"
#include "util.h"


namespace Cmd {

const std::string xz = STRg("xz --threads=" << Util::getSysctlInt("hw.ncpu"));

}
