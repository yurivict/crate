
#include "args.h"
#include "spec.h"
#include "locs.h"
#include "mount.h"
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
#include <ctype.h>

#include <string>
#include <list>
#include <iostream>
#include <filesystem>
#include <memory>

// 'sysctl security.jail.allow_raw_sockets=1' is needed to ping from jail

#define ERR(msg...) ERR2("running a crate container", msg)

#define LOG(msg...) \
  { \
    if (args.logProgress) \
      std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl; \
  }


static uid_t myuid = ::getuid();
static gid_t mygid = ::getgid();
static const char* user = ::getenv("USER");

//
// helpers
//
static std::string argsToString(int argc, char** argv) {
  std::ostringstream ss;
  for (int i = 0; i < argc; i++)
    ss << " " << argv[i];
  return ss.str();
}

//
// interface
//
bool runCrate(const Args &args, int argc, char** argv, int &outReturnCode) {
  LOG("'run' command is invoked, " << argc << " arguments are provided")

  // variables
  int res;
  auto homeDir = STR("/home/" << user);

  // create the jail directory
  auto jailPath = STR(Locations::jailDirectoryPath << "/jail-" << Util::filePathToBareName(args.runCrateFile) << "-pid" << ::getpid());
  Util::Fs::mkdir(jailPath, S_IRUSR|S_IWUSR|S_IXUSR);
  auto J = [&jailPath](auto subdir) {
    return STR(jailPath << subdir);
  };

  // mounts
  std::list<std::unique_ptr<Mount>> mounts;
  auto mount = [&mounts](Mount *m) {
    mounts.push_front(std::unique_ptr<Mount>(m)); // push_front so that the last added is also last removed
    m->mount();
  };

  // extract the crate archive into the jail directory
  LOG("extracting the crate file " << args.runCrateFile << " into " << jailPath)
  Util::runCommand(STR("tar xf " << args.runCrateFile << " -C " << jailPath), "extract the crate file into the jail directory");

  // parse +CRATE.SPEC
  auto spec = parseSpec(J("/+CRATE.SPEC")).preprocess();

  // mount devfs
  mount(new Mount("devfs", J("/dev"), ""));

  auto jailXname = STR(Util::filePathToBareName(args.runCrateFile) << "_pid" << ::getpid());

  // environment in jail
  std::string jailEnv;
  auto setJailEnv = [&jailEnv](auto var, auto val) {
    if (!jailEnv.empty())
      jailEnv = jailEnv + ' ';
    jailEnv = jailEnv + var + '=' + val;
  };
  setJailEnv("CRATE", "yes"); // let the app know that it runs from the crate. CAVEAT if you remove this, the env(1) command below needs to be removed when there is no env

  // turn options on
  if (spec.optionExists("x11")) {
    LOG("x11 option is requested: mount the X11 socket in jail")
    // create the X11 socket directory
    Util::Fs::mkdir(J("/tmp/.X11-unix"), 0777);
    // mount the X11 socket directory in jail
    mount(new Mount("nullfs", J("/tmp/.X11-unix"), "/tmp/.X11-unix"));
    // DISPLAY variable copied to jail
    auto *display = ::getenv("DISPLAY");
    if (display == nullptr)
      ERR("DISPLAY environment variable is not set")
    setJailEnv("DISPLAY", display);
  }
  std::string ipv4;
  if (spec.optionExists("net")) {
    ipv4 = "192.168.5.203";
    // copy /etc/resolv.conf into jail
    Util::Fs::copyFile("/etc/resolv.conf", J("/etc/resolv.conf"));
    // enable the IP alias, which enables networking both inside and outside of jail
    Util::runCommand(STR("ifconfig sk0 alias " << ipv4), "enable networking in /etc/rc.conf");
  }

  // create jail
  LOG("creating jail " << jailXname)
  res = ::jail_setv(JAIL_CREATE,
    "path", jailPath.c_str(),
    "host.hostname", Util::gethostname().c_str(),
    "ip4.addr", spec.optionExists("net") ? ipv4.c_str() : nullptr,
    "persist", nullptr,
    "allow.raw_sockets", spec.optionExists("net") ? "true" : "false",
    "allow.socket_af", spec.optionExists("net") ? "true" : "false",
    nullptr);
  if (res == -1)
    ERR("failed to create jail: " << jail_errmsg)
  int jid = res;
  LOG("jail " << jailXname << " has been created, jid=" << jid)

  // helper
  auto runCommandInJail = [jid](auto cmd, auto descr) {
    Util::runCommand(STR("jexec " << jid << " " << cmd), descr);
  };

  // rc-initializion (is this really needed?) This depends on the executables /bin/kenv, /sbin/sysctl, /bin/date which need to be kept during the 'create' phase
  //runCommandInJail("/bin/sh /etc/rc", "exec.start");

  // add the same user to jail, make group=user for now
  {
    LOG("create user's home directory " << homeDir << ", uid=" << myuid << " gid=" << mygid)
    Util::Fs::mkdir(J("/home"), 0755);
    Util::Fs::mkdir(J(homeDir), 0755);
    Util::Fs::chown(J(homeDir), myuid, mygid);
    LOG("add group " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw groupadd " << user << " -g " << mygid), "add the group in jail");
    LOG("add user " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw useradd " << user << " -u " << myuid << " -g " << mygid << " -s /bin/sh -d " << homeDir), "add the user in jail");
    // "video" option requires the corresponding user/group: create the identical user/group to jail
    if (spec.optionExists("video")) {
      static const char *devName = "/dev/video";
      static unsigned devNameLen = ::strlen(devName);
      int videoUid = -1;
      int videoGid = -1;
      for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
        auto cpath = entry.path().native();
        if (cpath.size() >= devNameLen+1 && cpath.substr(0, devNameLen) == devName && ::isdigit(cpath[devNameLen])) {
          struct stat sb;
          if (::stat(cpath.c_str(), &sb) != 0)
            ERR("can't stat the video device '" << cpath << "'");
          if (videoUid == -1) {
            videoUid = sb.st_uid;
            videoGid = sb.st_gid;
          } else if (sb.st_uid != videoUid || sb.st_gid != videoGid) {
            WARN("video devices have different uid/gid combinations")
          }
        }
      }

      // add video users and group, and add our user to this group
      if (videoUid != -1) {
        // CAVEAT we assume that videoUid/videoGid aren't the same UID/GID that the user has
        runCommandInJail(STR("/usr/sbin/pw groupadd videoops -g " << videoGid), "add the videoops group");
        runCommandInJail(STR("/usr/sbin/pw groupmod videoops -m " << user), "add the main user to the videoops group");
        runCommandInJail(STR("/usr/sbin/pw useradd video -u " << videoUid << " -g " << videoGid), "add the video user in jail");
      } else {
        WARN("the app expects video, but no video devices are present")
      }
    }
  }

  // share directories if requested
  for (auto &dirShare : spec.dirsShare) {
    // does the host directory exist?
    if (!Util::Fs::dirExists(dirShare.first))
      ERR("shared directory '" << dirShare.first << "' doesn't exist, can't run the app")
    // create the directory in jail
    Util::runCommand(STR("mkdir -p " << J(dirShare.second)), "create the shared directory in jail"); // TODO replace with API-based calls
    // mount it as nullfs
    mount(new Mount("nullfs", J(dirShare.second), dirShare.first));
  }

  // start services, if any
  if (!spec.runServices.empty())
    for (auto &service : spec.runServices)
      runCommandInJail(STR("/usr/sbin/service " << service << " onestart"), "start the service in jail");

  // copy X11 authentication files into the user's home directory in jail
  if (spec.optionExists("x11")) {
    // copy the .Xauthority and .ICEauthority files if they are present
    for (auto &file : {STR(homeDir << "/.Xauthority"), STR(homeDir << "/.ICEauthority")})
      if (Util::Fs::fileExists(file)) {
        Util::Fs::copyFile(file, J(file));
        Util::Fs::chown(J(file), myuid, mygid);
      }
  }

  // run the process
  int returnCode = 0;
  if (!spec.runExecutable.empty()) {
    LOG("running the command in jail: env=" << jailEnv)
    returnCode = ::system(CSTR("jexec -l -U " << user << " " << jid
                               << " /usr/bin/env " << jailEnv
                               << (spec.optionExists("dbg-ktrace") ? " /usr/bin/ktrace" : "")
                               << " " << spec.runExecutable << argsToString(argc, argv)));
    // XXX 256 gets returned, what does this mean?
    LOG("command has finished in jail: returnCode=" << returnCode)
  }

  // stop services, if any
  if (!spec.runServices.empty())
    for (auto &service : spec.runServices)
      runCommandInJail(STR("/usr/sbin/service " << service << " onestop"), "stop the service in jail");

  if (spec.optionExists("dbg-ktrace"))
    Util::Fs::copyFile(J(STR(homeDir << "/ktrace.out")), "ktrace.out");

  // rc-uninitializion (is this really needed?)
  //runCommandInJail("/bin/sh /etc/rc.shutdown", "exec.stop");

  // turn options off
  if (spec.optionExists("net")) {
    Util::runCommand(STR("ifconfig sk0 -alias " << ipv4), "enable networking in /etc/rc.conf");
  }

  // stop and remove jail
  LOG("removing jail " << jailXname << " jid=" << jid << " ...")
  res = ::jail_remove(jid);
  if (res == -1)
    ERR("failed to remove jail: " << strerror(errno))
  LOG("removing jail " << jailXname << " jid=" << jid << " done")

  // unmount all
  for (auto &m : mounts)
    m->unmount();

  // remove the jail directory
  LOG("removing the jail directory " << jailPath << " ...")
  Util::Fs::rmdirHier(jailPath);
  LOG("removing the jail directory " << jailPath << " done")

  // done
  outReturnCode = returnCode;
  LOG("'run' command has succeeded")
  return true;
}
