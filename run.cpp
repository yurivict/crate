
#include "args.h"
#include "spec.h"
#include "file.h"
#include "util.h"
#include "commands.h"

#include <rang.hpp>

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>
extern "C" { // https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=238928
#include <sys/jail.h>
}
#include <sys/uio.h>
#include <jail.h>

#include <iostream>

#define ERR(msg...) ERR2("running a crate container", msg)

#define LOG(msg...) \
  { \
    if (args.logProgress) \
      std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl; \
  }

// used paths
static const char *jailDirectoryPath = "/home/yuri/github/crate";
static const char *jailName = "_jail_run_";

//
// interface
//

bool runCrate(const Args &args, int argc, char** argv, int &outReturnCode) {
  LOG("'create' command is invoked, " << argc << " arguments are provided")

  int res;

  // create the jail directory
  auto jailPath = STR(jailDirectoryPath << "/" << jailName);
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1)
    ERR("failed to create the jail directory '" << jailPath << "': " << strerror(errno))

  // extract the crate archive into the jail directory
  LOG("extracting the crate file " << args.runCrateFile << " into " << jailPath)
  Util::runCommand(STR("tar xf " << args.runCrateFile << " -C " << jailPath), "extract the crate file into the jail directory");

  // parse +CRATE.SPEC
  auto spec = parseSpec(STR(jailPath << "/+CRATE.SPEC"));

  // mount devfs
  Util::runCommand(STR("mount -t devfs / " << jailPath << "/dev"), "mount devfs in the jail directory");

  auto jailXname = STR(Util::filePathToBareName(args.runCrateFile) << "_pid" << ::getpid());

  LOG("creating jail " << jailXname)
  res = jail_setv(JAIL_CREATE,
    "name", jailXname.c_str(),
    "persist", NULL,
    "allow.raw_sockets", "true",
    "allow.socket_af", "true",
    "allow.mount", "true",
    NULL);
  if (res == -1)
    ERR("failed to create jail: " << strerror(errno))
  int jid = res;
  LOG("jail " << jailXname << " has been created, jid=" << jid)

  // run the process
  int returnCode = 0;
  if (!spec.runExecutable.empty())
    returnCode = ::system(STR("jexec " << jid << " " << spec.runExecutable).c_str());
    // XXX 256 gets returned, what does this mean???

  // unmount devfs
  Util::runCommand(STR("umount " << jailPath << "/dev"), "unmount devfs in the jail directory");

  // stop and remove the jail
  LOG("removing jail " << jailXname << " jid=" << jid << " ...")
  res = jail_remove(jid);
  if (res == -1)
    ERR("failed to remove jail: " << strerror(errno))
  LOG("removing jail " << jailXname << " jid=" << jid << " done")

  // remove the jail directory
  LOG("removing the jail directory " << jailPath << " ...")
  Util::Fs::rmdirHier(jailPath);
  LOG("removing the jail directory " << jailPath << " done")

  outReturnCode = returnCode;
  return true;
}
