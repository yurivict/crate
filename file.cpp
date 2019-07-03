
#include "file.h"
#include "util.h"

#include <unistd.h>

static uid_t myuid = getuid();
static gid_t mygid = getgid();

void CrateFile::create(const std::string &jailPath, const std::string &crateFilePath) {
  Util::runCommand(STR("tar cf - -C " << jailPath << " . | xz --extreme --threads=8 > " << crateFilePath), "compress the jail directory into the crate file");
  Util::Fs::chown(crateFilePath, myuid, mygid);
}

void CrateFile::extract(const std::string &jailPath, const std::string &crateFilePath) {
}
