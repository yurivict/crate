
#include "locs.h"


namespace Locations {

const char *jailDirectoryPath = "/var/run/crate";
const std::string baseArchive = std::string(Locations::jailDirectoryPath) + "/base.txz";
const char *baseArchiveUrl = "ftp://ftp1.freebsd.org/pub/FreeBSD/snapshots/amd64/12.0-STABLE/base.txz";

}
