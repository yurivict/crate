#include "misc.h"
#include "util.h"
#include "locs.h"

#include <sys/stat.h>
#include <errno.h>

void createJailsDirectoryIfNeeded() {
  int res;

  // first, just try to create it. It might already exist which is ok.
  res = ::mkdir(Locations::jailDirectoryPath, 0700); // it should be only readable/writable by root
  if (res == -1 && errno != EEXIST)
    ERR2("create jails directory", "failed to create the jails directory '" << Locations::jailDirectoryPath << "': " << strerror(errno))

  // TODO check that permissions are correct on Locations::jailDirectoryPath
}
