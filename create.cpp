
#include "args.h"
#include "spec.h"
#include "file.h"
#include "elf.h"
#include "util.h"
#include "commands.h"

#include <rang.hpp>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>


#define ERR(msg...) ERR2("creating a crate", msg)

#define LOG(msg...) \
  { \
    if (args.logProgress) \
      std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl; \
  }

#define SYSCALL(res, syscall, arg) Util::ckSyscallError(res, syscall, arg)

// used paths
static const char *baseArchive = "/home/yuri/jails-learning/base.txz";
static const char *jailDirectoryPath = "/home/yuri/github/crate";
static const char *jailName = "_jail_create_";

//
// helpers
//

static std::string guessCrateName(const Spec &spec) {
  if (!spec.runExecutable.empty())
    return spec.runExecutable.substr(spec.runExecutable.rfind('/') + 1);
  else
    return spec.runService[0]; // XXX service might have arguments, etc.
}

static void notifyUserOfLongProcess(bool begin, const std::string &processName, const std::string &doingWhat) {
  std::cout << rang::fg::blue;
  std::cout << "==" << std::endl;
  if (begin)
    std::cout << "== Running " << processName << " in order to " << doingWhat << std::endl;
  else
    std::cout << "== " << processName << " has finished to " << doingWhat << std::endl;
  std::cout << "==" << rang::fg::reset << std::endl;
}

static void installPackagesInJail(const std::string &jailPath, const std::vector<std::string> &pkgs) {
  Util::runCommand(STR("mount -t devfs / " << jailPath << "/dev"), "mount devfs in the jail directory");
  notifyUserOfLongProcess(true, "pkg", STR("install the required packages: " << pkgs));
  Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " pkg install " << pkgs), "install the requested packages into the jail");
  Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " pkg delete -f pkg"), "remove the 'pkg' package from jail");
  notifyUserOfLongProcess(false, "pkg", STR("install the required packages: " << pkgs));
  Util::runCommand(STR("umount " << jailPath << "/dev"), "unmount devfs in the jail directory");
}

static std::set<std::string> getElfDependencies(const std::string &elfPath, const std::string &jailPath) {
  std::set<std::string> dset;
  std::istringstream is(Util::runCommandGetOutput(
    STR("/usr/sbin/chroot " << jailPath << " /bin/sh -c \"ldd " << elfPath << " | grep '=>' | sed -e 's|.* => ||; s| .*||'\""), "get elf dependencies"));
  std::string s;
  while (std::getline(is, s, '\n'))
    if (!s.empty())
      dset.insert(s);
  return dset;
}

static void removeRedundantJailParts(const std::string &jailPath, const Spec &spec) {
  namespace Fs = Util::Fs;
  
  // helpers
  auto P = [&jailPath](auto subdir) {
    return STR(jailPath << subdir);
  };
  auto toJailPath = [P](const std::set<std::string> &in, std::set<std::string> &out) {
    for (auto &p : in)
      out.insert(P(p));
  };

  // form the 'except' set
  std::set<std::string> except;
  if (!spec.runExecutable.empty()) {
    except.insert(P(spec.runExecutable));
    toJailPath(getElfDependencies(spec.runExecutable, jailPath), except);
  }
  for (auto &keepFile : spec.baseKeep) {
    except.insert(P(keepFile));
    // TODO check if keepFile is an executable and elf (allow non-executable/elf files be kept)
    toJailPath(getElfDependencies(keepFile, jailPath), except);
  }
  except.insert(P("/usr/libexec/ld-elf.so.1"));

  // remove items
  Fs::rmdirFlatExcept(P("/bin"), except);
  Fs::rmdirHier(P("/boot"));
  Fs::rmdirHier(P("/etc/periodic"));
  Fs::unlink(P("/usr/lib/include"));
  Fs::rmdirHierExcept(P("/lib"), except);
  Fs::rmdirHierExcept(P("/usr/lib"), except);
  Fs::rmdirHier(P("/usr/lib32"));
  Fs::rmdirHier(P("/usr/include"));
  Fs::rmdirHier(P("/sbin"));
  Fs::rmdirHier(P("/usr/sbin"));
  Fs::rmdirHierExcept(P("/usr/libexec"), except);
  Fs::rmdirHier(P("/usr/share/dtrace"));
  Fs::rmdirHier(P("/usr/share/doc"));
  Fs::rmdirHier(P("/usr/share/examples"));
  Fs::rmdirHier(P("/usr/share/bsdconfig"));
  Fs::rmdirHier(P("/usr/share/games"));
  Fs::rmdirHier(P("/usr/share/i18n"));
  Fs::rmdirHier(P("/usr/share/man"));
  Fs::rmdirHier(P("/usr/share/misc"));
  Fs::rmdirHier(P("/usr/share/pc-sysinstall"));
  Fs::rmdirHier(P("/usr/share/openssl"));
  Fs::rmdirHier(P("/usr/tests"));
  Fs::rmdir    (P("/usr/src"));
  Fs::rmdir    (P("/usr/obj"));
  Fs::rmdirHier(P("/var/db/etcupdate"));
  Fs::rmdirHierExcept(P("/usr/bin"), except);
  Fs::rmdirFlat(P("/rescue"));
  if (!spec.pkgInstall.empty()) {
    Fs::rmdirFlat(P("/var/cache/pkg"));
    Fs::rmdirFlat(P("/var/db/pkg"));
  }
}

//
// interface
//

bool createCrate(const Args &args, const Spec &spec) {
  int res;

  LOG("'create' command is invoked")

  // create a jail directory
  auto jailPath = STR(jailDirectoryPath << "/" << jailName);
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1)
    ERR("failed to create the jail directory '" << jailPath << "': " << strerror(errno))

  // unpack the base archive
  LOG("unpacking the base archive")
  Util::runCommand(STR("cat " << baseArchive << " | xz --decompress --threads=8 | tar -xf - --uname \"\" --gname \"\" -C " << jailPath), "unpack the system base into the jail directory");

  // install packages in the jail, if needed
  if (!spec.pkgInstall.empty()) {
    LOG("before installing packages")
    installPackagesInJail(jailPath, spec.pkgInstall);
    LOG("after installing packages")
  }

  // remove parts that aren't needed
  LOG("removing unnecessary parts")
  removeRedundantJailParts(jailPath, spec);

  // create +CRATE-* files
  Util::runCommand(STR("cp " << args.createSpec << " " << jailPath << "/+CRATE.SPEC"), "copy the spec file into jail");

  // pack the jail into a .crate file
  LOG("creating the crate file")
  CrateFile::create(
    jailPath,
    !args.createOutput.empty() ? args.createOutput : STR(guessCrateName(spec) << ".crate")
  );

  // remove the create directory
  LOG("removing the the jail directory")
  Util::Fs::rmdirHier(jailPath);

  return true;
}
