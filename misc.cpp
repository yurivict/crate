#include "misc.h"
#include "util.h"
#include "err.h"
#include "locs.h"

#include <sys/stat.h>
#include <errno.h>

//
// internals
//

static void createDirectoryIfNeeded(const char *dir, const char *what) {
  int res;

  // first, just try to create it. It might already exist which is ok.
  res = ::mkdir(dir, 0700); // it should be only readable/writable by root
  if (res == -1 && errno != EEXIST)
    ERR2(STR("create " << what << " directory"), "failed to create the " << what << " directory '" << dir << "': " << strerror(errno))

  // TODO check that permissions are correct on Locations::jailDirectoryPath
}

//
// interface
//

void createJailsDirectoryIfNeeded(const char *subdir) {
  createDirectoryIfNeeded(CSTR(Locations::jailDirectoryPath << subdir), "jails");
}

void createCacheDirectoryIfNeeded() {
  createDirectoryIfNeeded(Locations::cacheDirectoryPath, "cache");
}
