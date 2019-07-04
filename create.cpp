
#include "args.h"
#include "spec.h"
#include "file.h"
#include "elf.h"
#include "locs.h"
#include "util.h"
#include "commands.h"

#include <rang.hpp>

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <functional>

#define ERR(msg...) ERR2("creating a crate", msg)

#define LOG(msg...) \
  { \
    if (args.logProgress) \
      std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl; \
  }

#define SYSCALL(res, syscall, arg) Util::ckSyscallError(res, syscall, arg)

// used paths
static const char *baseArchive = "/home/yuri/jails-learning/base.txz"; // ftp://ftp1.freebsd.org/pub/FreeBSD/snapshots/arm64/12.0-STABLE/base.txz
static const char *jailName = "_jail_create_";

//
// helpers
//

static std::string guessCrateName(const Spec &spec) {
  if (!spec.runExecutable.empty())
    return spec.runExecutable.substr(spec.runExecutable.rfind('/') + 1);
  else
    return spec.runServices[0]; // XXX service might have arguments, etc.
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

static void installAndAddPackagesInJail(const std::string &jailPath, const std::vector<std::string> &pkgsInstall, const std::vector<std::string> &pkgsAdd) {
  // mount devfs
  Util::runCommand(STR("mount -t devfs / " << jailPath << "/dev"), "mount devfs in the jail directory");
  // notify
  notifyUserOfLongProcess(true, "pkg", STR("install the required packages: " << (pkgsInstall+pkgsAdd)));
  // install
  if (!pkgsInstall.empty())
    Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " pkg install " << pkgsInstall), "install the requested packages into the jail");
  if (!pkgsAdd.empty()) {
    for (auto &p : pkgsAdd) {
      Util::runCommand(STR("cp " << p << " " << jailPath << "/tmp/"), "copy the package to add into the jail");
      Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " pkg add /tmp/" << Util::filePathToFileName(p)), "remove the added package files from jail");
    }
  }
  // cleanup: delete the pkg package: it will not be needed any more, and delete the added package files
  Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " pkg delete -f pkg"), "remove the 'pkg' package from jail");
  if (!pkgsAdd.empty())
    Util::runCommand(STR("rm " << jailPath << "/tmp/*"), "remove the added package files from jail");
  // notify
  notifyUserOfLongProcess(false, "pkg", STR("install the required packages: " << (pkgsInstall+pkgsAdd)));
  // unmount devfs
  Util::runCommand(STR("umount " << jailPath << "/dev"), "unmount devfs in the jail directory");
}

static std::set<std::string> getElfDependencies(const std::string &elfPath, const std::string &jailPath,
                                                std::function<bool(const std::string&)> filter = [](const std::string &path) {return true;})
{
  std::set<std::string> dset;
  // It is possible to use elf(3) to read shared library dependences fron the elf file,
  // but it's hard to then find the disk location, ld-elf.so does this through the woodoo magic.
  // Instead, we just use ldd(1) to read this information.
  std::istringstream is(Util::runCommandGetOutput(
    STR("/usr/sbin/chroot " << jailPath << " /bin/sh -c \"ldd " << elfPath << " | grep '=>' | sed -e 's|.* => ||; s| .*||'\""), "get elf dependencies"));
  std::string s;
  while (std::getline(is, s, '\n'))
    if (!s.empty() && filter(s))
      dset.insert(s);
  return dset;
}

static void removeRedundantJailParts(const std::string &jailPath, const Spec &spec) {
  namespace Fs = Util::Fs;

  const char *prefix = "/usr/local";
  const char *prefixSlash = "/usr/local/";
  auto prefixSlashSz = ::strlen(prefixSlash);
  auto jailPathSz = jailPath.size();
  
  // local helpers
  auto J = [&jailPath](auto subdir) {
    return STR(jailPath << subdir);
  };
  auto toJailPath = [J](const std::set<std::string> &in, std::set<std::string> &out) {
    for (auto &p : in)
      out.insert(J(p));
  };
  auto fromJailPath = [&jailPath,jailPathSz](const std::string &file) {
    auto fileCstr = file.c_str();
    assert(::strncmp(jailPath.c_str(), fileCstr, jailPathSz) == 0); // really begins with jailPath
    return fileCstr + jailPathSz;
  };
  auto isBasePath = [prefixSlash,prefixSlashSz](const std::string &path) {
    return ::strncmp(path.c_str(), prefixSlash, prefixSlashSz) != 0;
  };

  // form the 'except' set: it should only contain files in basem and they should begin with jailPath
  std::set<std::string> except;
  auto keepFile = [&except,&jailPath,J,toJailPath](auto &file) { // any file, not just ELF
    except.insert(J(file));
    if (Fs::isElfFileOrDir(J(file)) == 'E')
      toJailPath(getElfDependencies(file, jailPath), except);
  };
  if (!spec.runExecutable.empty()) {
    if (isBasePath(spec.runExecutable))
      except.insert(J(spec.runExecutable));
    if (Fs::isElfFileOrDir(J(spec.runExecutable)) == 'E')
      toJailPath(getElfDependencies(spec.runExecutable, jailPath), except);
  }
  for (auto &file : spec.baseKeep)
    keepFile(file);
  if (!spec.runServices.empty()) {
    keepFile("/usr/sbin/service");
    keepFile("/bin/kenv");
    keepFile("/bin/mkdir");
    keepFile("/usr/bin/grep");
    keepFile("/sbin/sysctl");
    keepFile("/usr/bin/limits");
  }
  keepFile("/bin/sh"); // allow to create a user in jail, the user has to have the default shell
  keepFile("/usr/bin/env"); // allow to pass environment to jail
  keepFile("/usr/sbin/pw"); // allow to add users in jail
  keepFile("/usr/sbin/pwd_mkdb"); // allow to add users in jail
  keepFile("/usr/libexec/ld-elf.so.1"); // needed to run elf executables

  if (!spec.pkgInstall.empty() || !spec.pkgAdd.empty())
    for (auto &e : Fs::findElfFiles(J(prefix)))
      toJailPath(getElfDependencies(fromJailPath(e), jailPath, [isBasePath](const std::string &path) {return isBasePath(path);}), except);

  // remove items
  Fs::rmdirFlatExcept(J("/bin"), except);
  Fs::rmdirHier(J("/boot"));
  Fs::rmdirHier(J("/etc/periodic"));
  Fs::unlink(J("/usr/lib/include"));
  Fs::rmdirHierExcept(J("/lib"), except);
  Fs::rmdirHierExcept(J("/usr/lib"), except);
  Fs::rmdirHier(J("/usr/lib32"));
  Fs::rmdirHier(J("/usr/include"));
  Fs::rmdirHierExcept(J("/sbin"), except);
  Fs::rmdirHierExcept(J("/usr/bin"), except);
  Fs::rmdirHierExcept(J("/usr/sbin"), except);
  Fs::rmdirHierExcept(J("/usr/libexec"), except);
  Fs::rmdirHier(J("/usr/share/dtrace"));
  Fs::rmdirHier(J("/usr/share/doc"));
  Fs::rmdirHier(J("/usr/share/examples"));
  Fs::rmdirHier(J("/usr/share/bsdconfig"));
  Fs::rmdirHier(J("/usr/share/games"));
  Fs::rmdirHier(J("/usr/share/i18n"));
  Fs::rmdirHier(J("/usr/share/man"));
  Fs::rmdirHier(J("/usr/share/misc"));
  Fs::rmdirHier(J("/usr/share/pc-sysinstall"));
  Fs::rmdirHier(J("/usr/share/openssl"));
  Fs::rmdirHier(J("/usr/tests"));
  Fs::rmdir    (J("/usr/src"));
  Fs::rmdir    (J("/usr/obj"));
  Fs::rmdirHier(J("/var/db/etcupdate"));
  Fs::rmdirFlat(J("/rescue"));
  if (!spec.pkgInstall.empty() || !spec.pkgAdd.empty()) {
    Fs::rmdirFlat(J("/var/cache/pkg"));
    Fs::rmdirFlat(J("/var/db/pkg"));
  }
}

//
// interface
//

bool createCrate(const Args &args, const Spec &spec) {
  int res;

  LOG("'create' command is invoked")

  // create a jail directory
  auto jailPath = STR(Locations::jailDirectoryPath << "/" << jailName);
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1)
    ERR("failed to create the jail directory '" << jailPath << "': " << strerror(errno))

  // unpack the base archive
  LOG("unpacking the base archive")
  Util::runCommand(STR("cat " << baseArchive << " | xz --decompress --threads=8 | tar -xf - --uname \"\" --gname \"\" -C " << jailPath), "unpack the system base into the jail directory");

  // install packages in the jail, if needed
  if (!spec.pkgInstall.empty() || !spec.pkgAdd.empty()) {
    LOG("installing packages ...")
    installAndAddPackagesInJail(jailPath, spec.pkgInstall, spec.pkgAdd);
    LOG("done installing packages")
  }

  // remove parts that aren't needed
  LOG("removing unnecessary parts")
  removeRedundantJailParts(jailPath, spec);

  // create +CRATE-* files
  Util::runCommand(STR("cp " << args.createSpec << " " << jailPath << "/+CRATE.SPEC"), "copy the spec file into jail");

  // pack the jail into a .crate file
  auto crateFileName = !args.createOutput.empty() ? args.createOutput : STR(guessCrateName(spec) << ".crate");
  LOG("creating the crate file")
  CrateFile::create(
    jailPath,
    crateFileName
  );

  // remove the create directory
  LOG("removing the the jail directory")
  Util::Fs::rmdirHier(jailPath);

  std::cout << "the crate file '" << crateFileName << "' has been created" << std::endl;
  LOG("'create' command has succeeded")
  return true;
}
