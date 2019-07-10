
#include "args.h"
#include "spec.h"
#include "locs.h"
#include "cmd.h"
#include "mount.h"
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

// uid/gid
static uid_t myuid = ::getuid();
static gid_t mygid = ::getgid();

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

static void runChrootCommand(const std::string &jailPath, const std::string &cmd, const char *descr) {
  Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " " << cmd), descr);
}

static void installAndAddPackagesInJail(const std::string &jailPath,
                                        const std::vector<std::string> &pkgsInstall,
                                        const std::vector<std::string> &pkgsAdd,
                                        const std::vector<std::pair<std::string, std::string>> &pkgLocalOverride,
                                        const std::vector<std::string> &pkgNuke) {
  // local helpers
  auto J = [&jailPath](auto subdir) {
    return STR(jailPath << subdir);
  };

  // notify
  notifyUserOfLongProcess(true, "pkg", STR("install the required packages: " << (pkgsInstall+pkgsAdd)));

  // install
  if (!pkgsInstall.empty())
    runChrootCommand(jailPath, STR("pkg install " << pkgsInstall), "install the requested packages into the jail");
  if (!pkgsAdd.empty()) {
    for (auto &p : pkgsAdd) {
      Util::Fs::copyFile(p, STR(J("/tmp/") << Util::filePathToFileName(p)));
      runChrootCommand(jailPath, STR("pkg add /tmp/" << Util::filePathToFileName(p)), "remove the added package files from jail");
    }
  }

  // override packages with locally avaukable packages
  for (auto lo : pkgLocalOverride) {
    if (!Util::Fs::fileExists(lo.second))
      ERR("package override: failed to find the package file '" << lo.second << "'")
    runChrootCommand(jailPath, STR("pkg delete " << lo.first), CSTR("remove the package '" << lo.first << "' for local override in jail"));
    Util::Fs::copyFile(lo.second, STR(J("/tmp/") << Util::filePathToFileName(lo.second)));
    runChrootCommand(jailPath, STR("pkg add /tmp/" << Util::filePathToFileName(lo.second)), CSTR("add the local override package '" << lo.second << "' in jail"));
    Util::Fs::unlink(J(STR("/tmp/" << Util::filePathToFileName(lo.second))));
  }

  // nuke packages when requested
  for (auto &n : pkgNuke)
    runChrootCommand(jailPath, STR("/usr/local/sbin/pkg-static delete -y -f " << n), "nuke the package in the jail");

  // write the +CRATE.PKGS file
  runChrootCommand(jailPath, STR("pkg info > " << J("/+CRATE.PKGS")), "write +CRATE.PKGS file");
  // cleanup: delete the pkg package: it will not be needed any more, and delete the added package files
  runChrootCommand(jailPath, "pkg delete -f pkg", "remove the 'pkg' package from jail");
  if (!pkgsAdd.empty())
    Util::runCommand(STR("rm " << jailPath << "/tmp/*"), "remove the added package files from jail");
  // notify
  notifyUserOfLongProcess(false, "pkg", STR("install the required packages: " << (pkgsInstall+pkgsAdd)));
}

static std::set<std::string> getElfDependencies(const std::string &elfPath, const std::string &jailPath,
                                                std::function<bool(const std::string&)> filter = [](const std::string &path) {return true;})
{
  std::set<std::string> dset;
  // It is possible to use elf(3) to read shared library dependences from the elf file,
  // but it's hard to then find the disk location, ld-elf.so does this through some WooDoo magic.
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
    keepFile("/usr/sbin/service");  // needed to run a service
    keepFile("/bin/cat");           // based on ktrace of 'service {name} start'
    keepFile("/bin/chmod");         // --"--
    keepFile("/usr/bin/env");       // --"--
    keepFile("/bin/kenv");          // --"--
    keepFile("/bin/mkdir");         // --"--
    keepFile("/usr/bin/touch");     // --"--
    keepFile("/usr/bin/procstat");  // --"--
    keepFile("/usr/bin/grep");      // ?? needed?
    keepFile("/sbin/sysctl");       // ??
    keepFile("/usr/bin/limits");    // ??
    keepFile("/usr/sbin/daemon");   // services are often run with daemon(8)
    if (spec.runExecutable.empty())
      keepFile("/bin/sleep");       // our idle script runs
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

  // output crate file name
  auto crateFileName = !args.createOutput.empty() ? args.createOutput : STR(guessCrateName(spec) << ".crate");

  // download the base archive if not yet
  if (!Util::Fs::fileExists(Locations::baseArchive)) {
    std::cout << "downloading base.txz from " << Locations::baseArchiveUrl << " ..." << std::endl;
    Util::runCommand(STR("fetch -o " << Locations::baseArchive << " " << Locations::baseArchiveUrl), "download base.txz");
    std::cout << "base.txz has finished downloading" << std::endl;
  }

  // create the jail directory
  auto jailPath = STR(Locations::jailDirectoryPath << "/chroot-create-" << Util::filePathToBareName(crateFileName) << "-pid" << ::getpid());
  res = mkdir(jailPath.c_str(), S_IRUSR|S_IWUSR|S_IXUSR);
  if (res == -1)
    ERR("failed to create the jail directory '" << jailPath << "': " << strerror(errno))

  // unpack the base archive
  LOG("unpacking the base archive")
  Util::runCommand(STR("cat " << Locations::baseArchive
                       << " | " << Cmd::xz << " --decompress | tar -xf - --uname \"\" --gname \"\" -C " << jailPath),
                   "unpack the system base into the jail directory");

  // copy /etc/resolv.conf into the jail directory such that pkg would be able to resolve addresses
  Util::Fs::copyFile("/etc/resolv.conf", STR(jailPath << "/etc/resolv.conf"));

  // mount devfs
  LOG("mounting devfs in jail")
  Mount mountDevfs("devfs", STR(jailPath << "/dev"), "");
  mountDevfs.mount();

  // mount the pkg cache
  LOG("mounting pkg cache and as nullfs in jail")
  Util::Fs::mkdir(STR(jailPath << "/var/cache/pkg"), 0755);
  Mount mountPkgCache("nullfs", STR(jailPath << "/var/cache/pkg"), "/var/cache/pkg");
  mountPkgCache.mount();

  // install packages into the jail, if needed
  if (!spec.pkgInstall.empty() || !spec.pkgAdd.empty()) {
    LOG("installing packages ...")
    installAndAddPackagesInJail(jailPath, spec.pkgInstall, spec.pkgAdd, spec.pkgLocalOverride, spec.pkgNuke);
    LOG("done installing packages")
  }

  // unmount
  LOG("unmounting devfs in jail")
  mountDevfs.unmount();
  LOG("unmounting pkg cache in jail")
  mountPkgCache.unmount();

  // remove parts that aren't needed
  LOG("removing unnecessary parts")
  removeRedundantJailParts(jailPath, spec);

  // remove /etc/resolv.conf in the jail directory
  Util::Fs::unlink(STR(jailPath << "/etc/resolv.conf"));

  // write the +CRATE-SPEC file
  LOG("write the +CRATE.SPEC file")
  Util::Fs::copyFile(args.createSpec, STR(jailPath << "/+CRATE.SPEC"));

  // pack the jail into a .crate file
  LOG("creating the crate file " << crateFileName)
  Util::runCommand(STR("tar cf - -C " << jailPath << " . | " << Cmd::xz << " --extreme > " << crateFileName), "compress the jail directory into the crate file");
  Util::Fs::chown(crateFileName, myuid, mygid);

  // remove the create directory
  LOG("removing the the jail directory")
  Util::Fs::rmdirHier(jailPath);

  // finished
  std::cout << "the crate file '" << crateFileName << "' has been created" << std::endl;
  LOG("'create' command has succeeded")
  return true;
}
