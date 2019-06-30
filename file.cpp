
#include "file.h"

#include "util.h"

void CrateFile::create(const std::string &jailPath, const std::string &crateFilePath) {
  Util::runCommand(STR("tar cf - " << jailPath << " | xz --extreme --threads=8 > " << crateFilePath), "compress the jail directory into the crate file");
}

void CrateFile::extract(const std::string &jailPath, const std::string &crateFilePath) {
}
