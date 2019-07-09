
#include "cmd.h"
#include "util.h"


namespace Cmd {

const std::string xz = STRg("xz --threads=" << Util::getSysctlInt("hw.ncpu"));

}
