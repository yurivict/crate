// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "args.h"
#include "spec.h"
#include "locs.h"
#include "cmd.h"
#include "mount.h"
#include "net.h"
#include "scripts.h"
#include "ctx.h"
#include "util.h"
#include "err.h"
#include "commands.h"

#include <rang.hpp>

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/param.h>
extern "C" { // sys/jail.h isn't C++-safe: https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=238928
#include <sys/jail.h>
}
#include <sys/uio.h>
#include <jail.h>
#include <ctype.h>

#include <string>
#include <list>
#include <iostream>
#include <memory>
#include <limits>
#include <filesystem>

#define ERR(msg...) ERR2("running a crate container", msg)

#define LOG(msg...) \
  { \
    if (args.logProgress) \
      std::cerr << rang::fg::gray << Util::tmSecMs() << ": " << msg << rang::style::reset << std::endl; \
  }


static uid_t myuid = ::getuid();
static gid_t mygid = ::getgid();
static const char* user = ::getenv("USER");

// options
static bool optionInitializeRc = false; // this pulls a lot of dependencies, and starts a lot of things that we don't need in crate
static unsigned fwRuleBaseIn = 19000;  // ipfw rule number base for in rules: in rules should be before out rules because of rule conflicts
static unsigned fwRuleBaseOut = 59000; // ipfw rule number base TODO Need to investigate how to eliminate rule conflicts.

// hosts's default gateway network parameters
static std::string gwIface;
static std::string hostIP;
static std::string hostLAN;

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

  RunAtEnd destroyJailDir([&jailPath,&args]() {
    // remove the jail directory
    LOG("removing the jail directory " << jailPath << " ...")
    Util::Fs::rmdirHier(jailPath);
    LOG("removing the jail directory " << jailPath << " done")
  });

  // mounts
  std::list<std::unique_ptr<Mount>> mounts;
  auto mount = [&mounts](Mount *m) {
    mounts.push_front(std::unique_ptr<Mount>(m)); // push_front so that the last added is also the last removed
    m->mount();
  };

  // extract the crate archive into the jail directory
  LOG("extracting the crate file " << args.runCrateFile << " into " << jailPath)
  Util::runCommand(STR(Cmd::xz << " < " << args.runCrateFile << " | tar xf - -C " << jailPath), "extract the crate file into the jail directory");

  // parse +CRATE.SPEC
  auto spec = parseSpec(J("/+CRATE.SPEC")).preprocess();

  // check the pre-conditions
  if (spec.optionExists("net")) {
    // we need to create vnet jails
    if (Util::getSysctlInt("kern.features.vimage") == 0)
      ERR("the crate needs network access, but the VIMAGE feature isn't available in the kernel (kern.features.vimage==0)")
    // ipfw needs the ipfw_nat kernel module in order to function
    Util::ensureKernelModuleIsLoaded("ipfw_nat");
    // net.inet.ip.forwarding needs to be 1 for networking to work XXX it is "bad" to alter this value, need to see if this can be replaced with firewall rules
    if (Util::getSysctlInt("net.inet.ip.forwarding") == 0)
      Util::setSysctlInt("net.inet.ip.forwarding", 1);
  }

  // helper
  auto runScript = [&jailPath,&spec](const char *section) {
    Scripts::section(section, spec.scripts, [&jailPath,section](const std::string &cmd) {
      Util::runCommand(STR("ASSUME_ALWAYS_YES=yes /usr/sbin/chroot " << jailPath << " " << cmd), CSTR("run script#" << section));
    });
  };
  runScript("run:begin");

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

  // create jail // also see https://www.cyberciti.biz/faq/how-to-configure-a-freebsd-jail-with-vnet-and-zfs/
  runScript("run:before-create-jail");
  LOG("creating jail " << jailXname)
  const char *optNet = spec.optionExists("net") ? "true" : "false";
  res = ::jail_setv(JAIL_CREATE,
    "path", jailPath.c_str(),
    //"host.hostname", ON_USE_VNET_NOT(Util::gethostname().c_str()) ON_USE_VNET("rsnapshot"),
    "host.hostname", Util::gethostname().c_str(),
    "persist", nullptr,
    "allow.raw_sockets", optNet, // allow ping-pong
    "allow.socket_af", optNet,
    "vnet", nullptr/*"new"*/, // possible values are: nullptr, { "disable", "new", "inherit" }, see lib/libjail/jail.c
    nullptr);
  if (res == -1)
    ERR("failed to create jail: " << jail_errmsg)
  int jid = res;

  RunAtEnd destroyJail([jid,&jailXname,runScript,&args]() {
    // stop and remove jail
    runScript("run:before-remove-jail");
    LOG("removing jail " << jailXname << " jid=" << jid << " ...")
    if (::jail_remove(jid) == -1)
      ERR("failed to remove jail: " << strerror(errno))
    runScript("run:after-remove-jail");
    LOG("removing jail " << jailXname << " jid=" << jid << " done")
  });

  runScript("run:after-create-jail");
  LOG("jail " << jailXname << " has been created, jid=" << jid)

  // helpers for jail access
  auto runCommandInJail = [jid](auto cmd, auto descr) {
    Util::runCommand(STR("jexec " << jid << " " << cmd), descr);
  };
  auto runCommandInJailSilently = [jid](auto cmd, auto descr) {
    Util::runCommand(STR("jexec " << jid << " " << cmd << " > /dev/null"), descr);
  };
  auto writeFileInJail = [J](auto str, auto file) {
    Util::Fs::writeFile(str, J(file));
  };
  auto appendFileInJail = [J](auto str, auto file) {
    Util::Fs::appendFile(str, J(file));
  };

  // set up networking
  RunAtEnd destroyEpipeAtEnd;
  RunAtEnd destroyFirewallRulesAtEnd;
  auto optionNet = spec.optionNet();
  if (optionNet && (optionNet->allowOutbound() || optionNet->allowInbound())) {
    { // determine host's gateway interface
      auto elts = Util::splitString(
                    Util::runCommandGetOutput("netstat -rn | grep ^default | sed 's| *| |'", "determine host's gateway interface"),
                    " "
                  );
      if (elts.size() != 4)
        ERR("Unable to determine host's gateway IP and interface");
      elts[3] = Util::stripTrailingSpace(elts[3]);
      gwIface = elts[3];
    }
    { // determine host's gateway interface IP and network
      auto ipv4 = Net::getIfaceIp4Addresses(gwIface);
      if (ipv4.empty())
        ERR("Failed to determine host's gateway interface IP: no IPv4 addresses found")
      hostIP  = std::get<0>(ipv4[0]);
      hostLAN = std::get<2>(ipv4[0]);
    }
    // determine the hosts's nameserver
    auto nameserverIp = Net::getNameserverIp();
    // copy /etc/resolv.conf into jail
    if (optionNet->outboundDns)
      Util::Fs::copyFile("/etc/resolv.conf", J("/etc/resolv.conf"));
    // create the epipe
    // set the lo0 IP address (lo0 is always automatically present in vnet jails)
    runCommandInJail(STR("ifconfig lo0 inet 127.0.0.1"), "set up the lo0 interface in jail");
    // create networking interface
    //auto epipeIface = Net::createJailInterface(jailPath);
    std::string epipeIfaceA = Util::stripTrailingSpace(Util::runCommandGetOutput("ifconfig epair create", "create the jail epipe"));
    std::string epipeIfaceB = STR(epipeIfaceA.substr(0, epipeIfaceA.size()-1) << "b"); // jail side
    unsigned epairNum = std::stoul(epipeIfaceA.substr(5/*skip epair*/, epipeIfaceA.size()-5-1));
    auto numToIp = [](unsigned epairNum, unsigned ipIdx2) {
      // XXX use 10.0.0.0/8 network for this purpose because number of containers can be large, and we need to have that many IP addresses available
      unsigned ip = 100 + 2*epairNum + ipIdx2; // 100 to avoid the addresses .0 and .1
      unsigned ip1 = ip % 256;
      ip /= 256;
      unsigned ip2 = ip % 256;
      ip /= 256;
      unsigned ip3 = ip;
      return STR("10." << ip3 << "." << ip2 << "." << ip1);
    };
    auto epipeIpA = numToIp(epairNum, 0), epipeIpB = numToIp(epairNum, 1);
    // transfer the interface into jail
    Util::runCommand(STR("ifconfig " << epipeIfaceB << " vnet " << jid), "transfer the network interface into jail");
    // set the IP addresses on the jail epipe
    runCommandInJail(STR("ifconfig " << epipeIfaceB << " inet " << epipeIpB << " netmask 0xfffffffe"), "set up IP jail epipe addresses");
    Util::runCommand(STR("ifconfig " << epipeIfaceA << " inet " << epipeIpA << " netmask 0xfffffffe"), "set up IP jail epipe addresses");
    // enable firewall in jail
    //if (optionInitializeRc)
      appendFileInJail(STR(
          "firewall_enable=\"YES\""             << std::endl <<
          "firewall_type=\"open\""              << std::endl
        ),
        "/etc/rc.conf");
    // set default route in jail
    runCommandInJailSilently(STR("route add default " << epipeIpA), "set default route in jail");
    // destroy the epipe when finished
    destroyEpipeAtEnd.reset([epipeIfaceA]() {
      Util::runCommand(STR("ifconfig " << epipeIfaceA << " destroy"), CSTR("destroy the jail epipe (" << epipeIfaceA << ")"));
    });
    // add firewall rules to NAT and route packets from jails to host's default GW
    {
      auto cmdFW = [](const std::string &cmd) {
        Util::runCommand(STR("ipfw -q " << cmd), "add firewall rule");
      };
      auto fwRuleInNo  = fwRuleBaseIn + 1/*common rules*/ + epairNum/*per-crate rules*/;
      auto fwNatInNo = fwRuleInNo;
      auto fwNatOutCommonNo = fwRuleBaseOut;
      auto fwRuleOutCommonNo = fwRuleBaseOut;
      auto fwRuleOutNo = fwRuleBaseOut + 1/*common rules*/ + epairNum/*per-crate rules*/;

      // IN rules for this epipe
      if (optionNet->allowInbound()) {
        // create the NAT instance
        auto rangeToStr = [](const Spec::NetOptDetails::PortRange &range) {
          return range.first == range.second ? STR(range.first) : STR(range.first << "-" << range.second);
        };
        std::ostringstream strConfig;
        for (auto &rangePair : optionNet->inboundPortsTcp)
          strConfig << " redirect_port tcp " << epipeIpB << ":" << rangeToStr(rangePair.second) << " " << hostIP << ":" << rangeToStr(rangePair.first);
        for (auto &rangePair : optionNet->inboundPortsUdp)
          strConfig << " redirect_port udp " << epipeIpB << ":" << rangeToStr(rangePair.second) << " " << hostIP << ":" << rangeToStr(rangePair.first);
        cmdFW(STR("nat " << fwNatInNo << " config" << strConfig.str()));
        // create firewall rules: one per port range
        for (auto &rangePair : optionNet->inboundPortsTcp) {
          cmdFW(STR("add " << fwRuleInNo << " nat " << fwNatInNo << " tcp from any to " << hostIP  << " " << rangeToStr(rangePair.first) << " in recv " << gwIface));
          cmdFW(STR("add " << fwRuleInNo << " nat " << fwNatInNo << " tcp from " << epipeIpB << " " << rangeToStr(rangePair.second) << " to any out xmit " << gwIface));
        }
        for (auto &rangePair : optionNet->inboundPortsUdp) {
          cmdFW(STR("add " << fwRuleInNo << " nat " << fwNatInNo << " udp from any to " << hostIP  << " " << rangeToStr(rangePair.first) << " in recv " << gwIface));
          cmdFW(STR("add " << fwRuleInNo << " nat " << fwNatInNo << " udp from " << epipeIpB << " " << rangeToStr(rangePair.second) << " to any out xmit " << gwIface));
        }
      }

      // OUT common rules
      if (optionNet->allowOutbound()) {
        std::unique_ptr<Ctx::FwUsers> fwUsers(Ctx::FwUsers::lock());
        if (fwUsers->isEmpty()) {
          cmdFW(STR("nat " << fwNatOutCommonNo << " config ip " << hostIP));
          cmdFW(STR("add " << fwRuleOutCommonNo << " nat " << fwNatOutCommonNo << " all from any to " << hostIP << " in recv " << gwIface));
        }
        fwUsers->add(::getpid());
        fwUsers->unlock();
      }

      // OUT per-epipe rules: 1. whitewashes, 2. bans, 3. nats
      if (optionNet->allowOutbound()) {
        // allow DNS requests if required
        if (optionNet->outboundDns) {
          cmdFW(STR("add " << fwRuleOutNo << " nat " << fwNatOutCommonNo << " udp from " << epipeIpB << " to " << nameserverIp << " 53 out xmit " << gwIface));
          cmdFW(STR("add " << fwRuleOutNo << " allow udp from " << epipeIpB << " to " << nameserverIp << " 53"));
        }
        cmdFW(STR("add " << fwRuleOutNo << " deny udp from " << epipeIpB << " to any 53"));
        // bans
        if (!optionNet->outboundHost)
          cmdFW(STR("add " << fwRuleOutNo << " deny ip from " << epipeIpB << " to me"));
        if (!optionNet->outboundLan)
          cmdFW(STR("add " << fwRuleOutNo << " deny ip from " << epipeIpB << " to " << hostLAN));
        // nat the rest of the traffic
        cmdFW(STR("add " << fwRuleOutNo << " nat " << fwNatOutCommonNo << " all from " << epipeIpB << " to any out xmit " << gwIface));
      }
      // destroy rules
      destroyFirewallRulesAtEnd.reset([fwRuleInNo, fwRuleOutNo, fwRuleOutCommonNo, optionNet]() {
        // delete the rule(s) for this epipe
        if (optionNet->allowInbound())
          Util::runCommand(STR("ipfw delete " << fwRuleInNo), "destroy firewall rule");
        if (optionNet->allowOutbound()) {
          Util::runCommand(STR("ipfw delete " << fwRuleOutNo), "destroy firewall rule");
          { // possibly delete the common rules if this is the last firewall
            std::unique_ptr<Ctx::FwUsers> fwUsers(Ctx::FwUsers::lock());
            fwUsers->del(::getpid());
            if (fwUsers->isEmpty())
              Util::runCommand(STR("ipfw delete " << fwRuleOutCommonNo), "destroy firewall rule");
            fwUsers->unlock();
          }
        }
      });
    }
  }

  // disable services that normally start by default, but aren't desirable in crates
  if (optionInitializeRc)
    appendFileInJail(STR(
        "sendmail_enable=\"NO\""         << std::endl <<
        "cron_enable=\"NO\""             << std::endl
      ),
      "/etc/rc.conf");

  // rc-initializion (is this really needed?) This depends on the executables /bin/kenv, /sbin/sysctl, /bin/date which need to be kept during the 'create' phase
  if (optionInitializeRc)
    runCommandInJailSilently("/bin/sh /etc/rc", "exec.start");
  else
    runCommandInJailSilently("service ipfw start > /dev/null", "start firewall in jail");

  // add the same user to jail, make group=user for now
  {
    LOG("create user's home directory " << homeDir << ", uid=" << myuid << " gid=" << mygid)
    Util::Fs::mkdir(J("/home"), 0755);
    Util::Fs::mkdir(J(homeDir), 0755);
    Util::Fs::chown(J(homeDir), myuid, mygid);
    runScript("run:before-create-users");
    LOG("add group " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw groupadd " << user << " -g " << mygid), "add the group in jail");
    LOG("add user " << user << " in jail")
    runCommandInJail(STR("/usr/sbin/pw useradd " << user << " -u " << myuid << " -g " << mygid << " -s /bin/sh -d " << homeDir), "add the user in jail");
    runCommandInJail(STR("/usr/sbin/pw usermod " << user << " -G wheel"), "add the group to the user");
    // "video" option requires the corresponding user/group: create the identical user/group to jail
    if (spec.optionExists("video")) {
      static const char *devName = "/dev/video";
      static unsigned devNameLen = ::strlen(devName);
      uid_t videoUid = std::numeric_limits<uid_t>::max();
      gid_t videoGid = std::numeric_limits<gid_t>::max();
      for (const auto &entry : std::filesystem::directory_iterator("/dev")) {
        auto cpath = entry.path().native();
        if (cpath.size() >= devNameLen+1 && cpath.substr(0, devNameLen) == devName && ::isdigit(cpath[devNameLen])) {
          struct stat sb;
          if (::stat(cpath.c_str(), &sb) != 0)
            ERR("can't stat the video device '" << cpath << "'");
          if (videoUid == std::numeric_limits<uid_t>::max()) {
            videoUid = sb.st_uid;
            videoGid = sb.st_gid;
          } else if (sb.st_uid != videoUid || sb.st_gid != videoGid) {
            WARN("video devices have different uid/gid combinations")
          }
        }
      }

      // add video users and group, and add our user to this group
      if (videoUid != std::numeric_limits<uid_t>::max()) {
        // CAVEAT we assume that videoUid/videoGid aren't the same UID/GID that the user has
        runCommandInJail(STR("/usr/sbin/pw groupadd videoops -g " << videoGid), "add the videoops group");
        runCommandInJail(STR("/usr/sbin/pw groupmod videoops -m " << user), "add the main user to the videoops group");
        runCommandInJail(STR("/usr/sbin/pw useradd video -u " << videoUid << " -g " << videoGid), "add the video user in jail");
      } else {
        WARN("the app expects video, but no video devices are present")
      }
    }
    runScript("run:after-create-users");
  }

  // share directories if requested
  for (auto &dirShare : spec.dirsShare) {
    const auto dirJail = Util::pathSubstituteVarsInPath(dirShare.first);
    const auto dirHost = Util::pathSubstituteVarsInPath(dirShare.second);
    // does the host directory exist?
    if (!Util::Fs::dirExists(dirHost))
      ERR("shared directory '" << dirHost << "' doesn't exist on the host, can't run the app")
    // create the directory in jail
    Util::runCommand(STR("mkdir -p " << J(dirJail)), "create the shared directory in jail"); // TODO replace with API-based calls
    // mount it as nullfs
    mount(new Mount("nullfs", J(dirJail), dirHost));
  }

  // share files if requested
  for (auto &fileShare : spec.filesShare) {
    const auto fileJail = Util::pathSubstituteVarsInPath(fileShare.first);
    const auto fileHost = Util::pathSubstituteVarsInPath(fileShare.second);
    // do files exist?
    bool fileHostExists = Util::Fs::fileExists(fileHost);
    bool fileJailExists = Util::Fs::fileExists(J(fileJail));
    if (!fileHostExists && !fileJailExists) {
      ERR("none of the files in a file-share exists: fileHost=" << fileHost << " fileJail=" << fileJail) // alternatively, we can create an empty file (?)
    } else if (fileHostExists && fileJailExists) {
      Util::Fs::unlink(J(fileJail));
      Util::Fs::link(fileHost, J(fileJail));
    } else if (fileHostExists) { // fileHost exists, but fileJail doesn't
      Util::Fs::link(fileHost, J(fileJail));
    } else { // fileJail exists, but fileHost doesn't
      Util::Fs::link(J(fileJail), fileHost);
    }
  }

  // start services, if any
  runScript("run:before-start-services");
  if (!spec.runServices.empty())
    for (auto &service : spec.runServices)
      runCommandInJail(STR("/usr/sbin/service " << service << " onestart"), "start the service in jail");
  runScript("run:after-start-services");

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
  runScript("run:before-execute");
  int returnCode = 0;
  if (!spec.runCmdExecutable.empty()) {
    LOG("running the command in jail: env=" << jailEnv)
    returnCode = ::system(CSTR("jexec -l -U " << user << " " << jid
                               << " /usr/bin/env " << jailEnv
                               << (spec.optionExists("dbg-ktrace") ? " /usr/bin/ktrace" : "")
                               << " " << spec.runCmdExecutable << spec.runCmdArgs << argsToString(argc, argv)));
    // XXX 256 gets returned, what does this mean?
    LOG("command has finished in jail: returnCode=" << returnCode)
  } else {
    // No command is specified to be run.
    // This means that this is a service-only crate. We have to run some command, otherwise the crate would just exit immediately.
    LOG("this is a service-only crate, install and run the command that exits on Ctrl-C")
    auto cmdFile = "/run.sh";
    writeFileInJail(STR(
        "#!/bin/sh"                                                 << std::endl <<
        ""                                                          << std::endl <<
        "trap onSIGNIT 2"                                           << std::endl <<
        ""                                                          << std::endl <<
        "onSIGNIT()"                                                << std::endl <<
        "{"                                                         << std::endl <<
        "  echo \"Caught signal SIGINT ... exiting\""               << std::endl <<
        "  exit 0"                                                  << std::endl <<
        "}"                                                         << std::endl <<
        ""                                                          << std::endl <<
        "echo \"Running the services: " << spec.runServices << "\"" << std::endl <<
        "echo \"Waiting for Ctrl-C to exit ...\""                   << std::endl <<
        "/bin/sleep 1000000000"                                     << std::endl
      ),
      cmdFile
    );
    // set ownership/permissions
    Util::Fs::chown(J(cmdFile), myuid, mygid);
    Util::Fs::chmod(J(cmdFile), 0500); // User-RX
    // run it the same way as we would any other command
    returnCode = ::system(CSTR("jexec -l -U " << user << " " << jid << " " << cmdFile));
  }
  runScript("run:after-execute");

  // stop services, if any
  if (!spec.runServices.empty())
    for (auto &service : Util::reverseVector(spec.runServices))
      runCommandInJail(STR("/usr/sbin/service " << service << " onestop"), "stop the service in jail");

  if (spec.optionExists("dbg-ktrace"))
    Util::Fs::copyFile(J(STR(homeDir << "/ktrace.out")), "ktrace.out");

  // rc-uninitializion (is this really needed?)
  if (optionInitializeRc)
    runCommandInJail("/bin/sh /etc/rc.shutdown", "exec.stop");

  runScript("run:end");

  // release resources
  destroyJail.doNow();
  for (auto &m : mounts)
    m->unmount();
  if (optionNet && (optionNet->allowOutbound() || optionNet->allowInbound())) {
    destroyFirewallRulesAtEnd.doNow();
    destroyEpipeAtEnd.doNow();
  }
  destroyJailDir.doNow();

  // done
  outReturnCode = returnCode;
  LOG("'run' command has succeeded")
  return true;
}
