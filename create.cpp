
#include "args.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>

const char *baseArchive = "/home/yuri/jails-learning/base.txz";
const char *jailDirectoryPath = "/home/yuri/jails-learning";
const char *jailName = "_create_jail_";

bool createCrate(const Args &args) {
  
  int res;
  std::string jailPath = std::string(jailDirectoryPath)+"/"+jailName;

  // create a jail directory
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1) {
    std::cerr << "Failed to create the jail directory '" << jailPath << "': " << strerror(errno) << std::endl;
    return false;
  }

  // unpack the base archive
  std::ostringstream ss;
  ss << "tar -xf " << baseArchive << " -C " << jailPath;
  res = system(ss.str().c_str());
  if (res == -1) {
    std::cerr << "Failed to unpack the system base into the jail directory: " << strerror(errno) << std::endl;
    return false;
  }

  // create a jail

  // run commands in the jail

  // destroy the jail

  // pack the jail into a .crate file

  return true;
}
