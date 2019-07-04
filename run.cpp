
#include "args.h"
#include "spec.h"
#include "file.h"
#include "locs.h"
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


static uid_t myuid = getuid();
static gid_t mygid = getgid();

// used paths
static const char *jailName = "_jail_run_";

//
// interface
//

bool runCrate(const Args &args, int argc, char** argv, int &outReturnCode) {
  LOG("'run' command is invoked, " << argc << " arguments are provided")

  int res;

  // create the jail directory
  auto jailPath = STR(Locations::jailDirectoryPath << "/" << jailName);
  Util::Fs::mkdir(jailPath, S_IRUSR|S_IWUSR|S_IXUSR);
  auto J = [&jailPath](auto subdir) {
    return STR(jailPath << subdir);
  };

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
    "host.hostname", jailXname.c_str(),
    "path", jailPath.c_str(),
    "persist", NULL,
    //"allow.raw_sockets", "true",
    //"allow.socket_af", "true",
    //"allow.mount", "true",
    NULL);
  if (res == -1)
    ERR("failed to create jail: " << strerror(errno))
  int jid = res;
  LOG("jail " << jailXname << " has been created, jid=" << jid)

  auto runCommandInJail = [jid](auto cmd, auto descr) {
    Util::runCommand(STR("jexec " << jid << " " << cmd), descr);
  };

  // environment in jail
  std::string jailEnv;
  auto setJailEnv = [&jailEnv](auto var, auto val) {
    if (!jailEnv.empty())
      jailEnv = jailEnv + ':';
    jailEnv = jailEnv + var + '=' + val;
  };
  setJailEnv("CRATE", "yes"); // let the app know that it runs from the crate. CAVEAT if you remove this, the env(1) command below needs to be removed when there is no env

  // add the same user to jail, make group=user for now
  {
    auto user = ::getenv("USER");
    auto homeDir = STR("/home/" << user);
    LOG("create user's home directory " << homeDir << ", uid=" << myuid << " gid=" << mygid)
    Util::Fs::mkdir(J("/home"), 0755);
    Util::Fs::mkdir(J(homeDir), 0755);
    Util::Fs::chown(J(homeDir), myuid, mygid);
    LOG("add group " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw groupadd " << user << " -g " << mygid), "add the group in jail");
    LOG("add user " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw useradd " << user << " -u " << myuid << " -g " << mygid << " -s /bin/sh -d " << homeDir << " " << user), "add the user in jail");
  }

  // satisfy options, if any
  if (spec.optionExists("x11")) {
    LOG("x11 option is requested: mount the X11 socket in jail")
    // create the X11 socket directory
    Util::Fs::mkdir(J("/tmp/.X11-unix"), 0777);
    // mount the X11 socket directory in jail
    Util::runCommand(STR("mount -t nullfs /tmp/.X11-unix " << J("/tmp/.X11-unix")), "mount nullfs for X11 socket in the jail directory");
    // DISPLAY variable copied to jail
    auto *display = ::getenv("DISPLAY");
    if (display == nullptr)
      ERR("DISPLAY environment variable is not set")
    setJailEnv("DISPLAY", display);
  }

  // start services, if any
  if (!spec.runServices.empty())
    for (auto &service : spec.runServices)
      runCommandInJail(STR("/usr/sbin/service " << service << " onestart"), "start the service in jail");

  // run the process
  int returnCode = 0;
  if (!spec.runExecutable.empty()) {
    LOG("running the command in jail: env=" << jailEnv)
    returnCode = ::system(STR("jexec -U " << ::getenv("USER") << " " << jid << " /usr/bin/env \"" << jailEnv << "\" " << spec.runExecutable).c_str());
    // XXX 256 gets returned, what does this mean???
    LOG("command has finished in jail: returnCode=" << returnCode)
  }

  // stop services, if any
  if (!spec.runServices.empty())
    for (auto &service : spec.runServices)
      runCommandInJail(STR("/usr/sbin/service " << service << " onestop"), "stop the service in jail");

  if (spec.optionExists("x11")) {
    Util::runCommand(STR("umount " << J("/tmp/.X11-unix")), "unmount nullfs for X11 socket in the jail directory");
  }

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
  LOG("'run' command has succeeded")
  return true;
}
