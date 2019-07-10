
#include "locs.h"

#include <string>

namespace Locations {

const char *jailDirectoryPath = "/var/run/crate";
const char *cacheDirectoryPath = "/var/cache/crate";
const std::string baseArchive = std::string(Locations::cacheDirectoryPath) + "/base.txz";
const char *baseArchiveUrl = "ftp://ftp1.freebsd.org/pub/FreeBSD/snapshots/amd64/12.0-STABLE/base.txz";

}
