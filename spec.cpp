// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "spec.h"
#include "util.h"
#include "err.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml-cpp/yaml.h>
#include <rang.hpp>

#include <string>
#include <set>
#include <map>
#include <list>
#include <iostream>
#include <sstream>

#include "lst-all-script-sections.h" // generated from create.cpp and run.cpp by the Makefile

#define ERR(msg...) \
  ERR2("spec parser", msg)

// all options
static std::list<std::string> allOptionsLst = {"x11", "net", "ssl-certs", "tor", "video", "gl", "no-rm-static-libs", "dbg-ktrace"}; // the order is important for option processing
static std::set<std::string> allOptionsSet(std::begin(allOptionsLst), std::end(allOptionsLst));

// helpers
static std::string AsString(const YAML::Node &node) {
  return node.template as<std::string>();
}

// some generic programming magic
static void Add(std::vector<std::string> &container, const std::string &val) {
  container.push_back(val);
}
static void Add(std::set<std::string> &container, const std::string &val) {
  container.insert(val);
}
static void Add(std::map<std::string, std::shared_ptr<Spec::OptDetails>> &container, const std::string &val) {
  (void)container[val];
}
static void Add(std::vector<std::pair<Spec::NetOptDetails::PortRange, Spec::NetOptDetails::PortRange>> &container, const std::string &val) {
  auto vali = Util::toUInt(val);
  container.push_back({{vali, vali}, {vali, vali}});
}

static std::map<std::string, std::string> parseScriptsSection(const std::string &section, const YAML::Node &node) {
  auto isSequenceOfScalars = [](const YAML::Node &node) {
    if (!node.IsSequence() || node.size() == 0)
      return false;
    for (auto &s : node)
      if (!s.IsScalar())
        return false;

    return true;
  };
  auto isSequenceOfSequenceOfScalars = [isSequenceOfScalars](const YAML::Node &node) {
    if (!node.IsSequence() || node.size() == 0)
      return false;
    for (auto &n : node) {
      if (!isSequenceOfScalars(n))
        return false;
      for (auto &ns : n)
        if (!ns.IsScalar())
          return false;
    }

    return true;
  };
  auto listOrScalar = [&section,isSequenceOfScalars](const YAML::Node &node, std::string &out) { // expect a scalar or the array of scalars
    if (node.IsScalar()) {
      out = STR(AsString(node) << std::endl);
      return true;
    } else if (isSequenceOfScalars(node)) {
      std::ostringstream ss;
      for (auto line : node) {
        if (!line.IsScalar())
          ERR("scalar expected as a script line in '" << section << "': line.Type=" << line.Type())
        ss << AsString(line) << std::endl;
      }
      out = ss.str();
      return true;
    } else {
      return false;
    }
  };

  // Supported layouts:
  // * Scalar
  // * array of scalars => 1 multi-line
  // * array of array of scalar
  // * map of {scalar, array of scalar}

  static const char *errMsg = "scripts must be scalars, arrays of scalars, arrays of arrays of scalars, or maps of scalars or of arrays of scalars";

  std::string str;
  if (listOrScalar(node, str)) { // a single single-line script OR a single multi-line script
    return {{"", str}};
  } else if (isSequenceOfSequenceOfScalars(node)) { // array of array of scalar
    std::map<std::string, std::string> m;
    unsigned idx = 1;
    for (auto elt : node)
      if (listOrScalar(elt, str))
        m[STR("script#" << idx++)] = str;
      else
        ERR(errMsg << " '" << section << "' #1")
    return m;
  } else if (node.IsMap()) { // map of {scalar, array of scalar}
    std::map<std::string, std::string> m;
    for (auto namedScript : node)
      if (listOrScalar(namedScript.second, str))
        m[AsString(namedScript.first)] = str;
      else
        ERR(errMsg << ", problematic section '" << section << "' #2")
    return m;
  } else {
    ERR(errMsg << " '" << section << "' #3")
  }
}

static Spec::NetOptDetails::PortRange parsePortRange(const std::string &str) {
  auto hyphen = str.find('-');
  return hyphen == std::string::npos ?
    Spec::NetOptDetails::PortRange(Util::toUInt(str), Util::toUInt(str))
    :
    Spec::NetOptDetails::PortRange(Util::toUInt(str.substr(0, hyphen)), Util::toUInt(str.substr(hyphen + 1)));
}

//
// methods
//

Spec::OptDetails::~OptDetails() {
}

Spec::NetOptDetails::NetOptDetails()
: outboundWan(false),
  outboundLan(false),
  outboundHost(false),
  outboundDns(false)
{ }

Spec::NetOptDetails* Spec::NetOptDetails::createDefault() {
  // default "net" options allow all outbound and no inbound
  auto d = new NetOptDetails;
  d->outboundWan  = true; // all outbound is allowed by default
  d->outboundLan  = true;
  d->outboundHost = true;
  d->outboundDns  = true;
  return d;
}

bool Spec::NetOptDetails::allowOutbound() const {
  return outboundWan || outboundLan || outboundHost || outboundDns;
}

bool Spec::NetOptDetails::allowInbound() const {
  return !inboundPortsTcp.empty() || !inboundPortsUdp.empty();
}

bool Spec::optionExists(const char* opt) const {
  return options.find(opt) != options.end();
}

const Spec::NetOptDetails* Spec::optionNet() const {
  return getOptionDetails<Spec::NetOptDetails>("net");
}


Spec::NetOptDetails* Spec::optionNetWr() const {
  return getOptionDetailsWr<Spec::NetOptDetails>("net");
}
const Spec::TorOptDetails* Spec::optionTor() const {
  return getOptionDetails<Spec::TorOptDetails>("tor");
}

template<class OptDetailsClass>
const OptDetailsClass* Spec::getOptionDetails(const char *opt) const {
  auto it = options.find(opt);
  if (it == options.end())
    return nullptr;
  return static_cast<const OptDetailsClass*>(it->second.get());
}

template<class OptDetailsClass>
OptDetailsClass* Spec::getOptionDetailsWr(const char *opt) const {
  auto it = options.find(opt);
  if (it == options.end())
    return nullptr;
  return static_cast<OptDetailsClass*>(it->second.get());
}

Spec::TorOptDetails::TorOptDetails()
: controlPort(false)
{ }

Spec::TorOptDetails* Spec::TorOptDetails::createDefault() {
  // default "tor" option runs tor in the default mode, which is to just have the http proxy port open, and nothing else
  return new TorOptDetails;
}

// preprocess function processes some options, etc. for simplicity of use both by users and by our 'create' module
Spec Spec::preprocess() const {
  Spec spec = *this;

  // helpers
  auto O = [&spec](auto o, bool keep) {
    if (spec.optionExists(o)) {
      if (!keep)
        spec.options.erase(o);
      return true;
    }
    return false;
  };

  // ssl-certs option => install the ca_root_nss package
  if (O("ssl-certs", false))
    spec.pkgInstall.push_back("ca_root_nss");

  // tor option => several actions need to be taken
  if (auto optionTor = spec.optionTor()) {
    // install the tor package
    spec.pkgInstall.push_back("tor");
    // run the service before everything else
    spec.runServices.insert(spec.runServices.begin(), "tor");
    // keep some files that are needed by the tor service to run
    spec.baseKeep.push_back("/usr/bin/limits");
    spec.baseKeep.push_back("/usr/bin/su");
    spec.baseKeep.push_back("/bin/ps");                      // for tor service to validate /var/run/tor/tor.pid file
    spec.baseKeep.push_back("/bin/csh");                     // XXX not sure why csh is needed by tor
    spec.baseKeepWildcard.push_back("/usr/lib/pam_*.so");    // pam is needed for su called by tor
    spec.baseKeepWildcard.push_back("/usr/lib/pam_*.so.*");  // pam is needed for su called by tor
    // optional tor control port
    if (optionTor->controlPort)
      spec.scripts["run:before-start-services"]["openTorControlPort"] = "echo ControlPort 9051 >> /usr/local/etc/tor/torrc";
  }

  // gl option => install nvidia-driver & mesa-dri (XXX for now it only works on nvidia-card-based systems)
  if (O("gl", false)) {
    spec.pkgInstall.push_back("mesa-dri");
    spec.pkgInstall.push_back("nvidia-driver");
  }

  // dbg-ktrace option => keep the ktrace executable
  if (O("dbg-ktrace", true))
    spec.baseKeep.push_back("/usr/bin/ktrace");

  return spec;
}

void Spec::validate() const {
  // helpers
  auto isFullPath = [](const std::string &path) {
    return path[0] == '/';
  };

  // must have something to run
  if (runCmdExecutable.empty() && runServices.empty() && !optionExists("tor"))
    ERR("crate has to have either the executable to run, some services to run, or both, it can't have nothing to do")

  // must be no duplicate local package overrides
  if (!pkgLocalOverride.empty()) {
    std::map<std::string, std::string> pkgs;
    for (auto &lo : pkgLocalOverride) {
      if (pkgs.find(lo.first) != pkgs.end())
        ERR("duplicate local override packages: " << lo.first << "->" << pkgs[lo.first] << " and " << lo.first << "->" << lo.second)
      pkgs[lo.first] = lo.second;
    }
  }

  // executable must have full path
  if (!runCmdExecutable.empty())
    if (!isFullPath(runCmdExecutable))
      ERR("the executable path has to be a full path, executable=" << runCmdExecutable)

  // shared directories must be full paths
  for (auto &dirShare : dirsShare)
    if (!isFullPath(dirShare.first) || !isFullPath(Util::pathSubstituteVars(dirShare.second)))
      ERR("the shared directory paths have to be a full paths, share=" << dirShare.first << "->" << dirShare.second)

  // shared files must be full paths
  for (auto &fileShare : filesShare)
    if (!isFullPath(fileShare.first) || !isFullPath(Util::pathSubstituteVars(fileShare.second)))
      ERR("the shared directory paths have to be a full paths, share=" << fileShare.first << "->" << fileShare.second)

  // options must be from the supported set
  for (auto &o : options)
    if (allOptionsSet.find(o.first) == allOptionsSet.end())
      ERR("the unknown option '" << o.first << "' was supplied")

  // script sections must be from the supported set
  for (auto &s : scripts)
    if (s.first.empty() || allScriptSections.find(s.first) == allScriptSections.end())
      ERR("the unknown script section '" << s.first << "' was supplied")

  // port ranges in net options must be consistent
  if (auto optNet = optionNet())
    for (auto pv : {&optNet->inboundPortsTcp, &optNet->inboundPortsUdp})
      for (auto &rangePair : *pv)
        if (rangePair.first.second - rangePair.first.first != rangePair.second.second - rangePair.second.first)
          ERR("port ranges have different spans:"
            << rangePair.first.first << "-" << rangePair.first.second
            << " and "
            << rangePair.second.first << "-" << rangePair.second.second)
}

//
// interface
//

Spec parseSpec(const std::string &fname) {

  Spec spec;

  // helper functions
  auto isKey = [](auto &k, const char *s) {
    return AsString(k.first) == s;
  };
  auto scalar = [](auto &node, std::string &out, const char *opath) {
    if (node.IsScalar()) {
      out = AsString(node);
    } else {
      ERR("unsupported " << opath << " object of type " << node.Type() << ", only scalar is allowed")
    }
  };
  auto listOrScalar = [](auto &node, auto &out, const char *opath) {
    if (node.IsSequence()) {
      for (auto r : node)
        Add(out, AsString(r));
      return true;
    } else if (node.IsScalar()) {
      for (auto &e : Util::splitString(AsString(node), " "))
        Add(out, e);
      return true;
    } else {
      return false;
    }
  };
  auto listOrScalarOnly = [listOrScalar](auto &node, auto &out, const char *opath) {
    if (!listOrScalar(node, out, opath))
      ERR("unsupported " << opath << " object of type " << node.Type() << ", only list or scalar are allowed")
  };

  // parse the spec in the yaml format
  YAML::Node top = YAML::LoadFile(fname);

  // top-level tags
  for (auto k : top) {
    if (isKey(k, "base")) {
      for (auto b : k.second) {
        if (isKey(b, "keep")) {
          listOrScalarOnly(b.second, spec.baseKeep, "base/keep");
        } else if (isKey(b, "keep-wildcard")) {
          listOrScalarOnly(b.second, spec.baseKeepWildcard, "base/keep-wildcard");
        } else if (isKey(b, "remove")) {
          listOrScalarOnly(b.second, spec.baseRemove, "base/remove");
        } else {
          ERR("unknown element base/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "pkg")) {
      for (auto b : k.second) {
        if (isKey(b, "install")) {
          listOrScalarOnly(b.second, spec.pkgInstall, "pkg/install");
        } else if (isKey(b, "local-override")) {
          if (!b.second.IsMap())
            ERR("pkg/local-override must be a map of package name to local package file path")
          for (auto lo : b.second)
            spec.pkgLocalOverride.push_back({AsString(lo.first), AsString(lo.second)});
        } else if (isKey(b, "add")) {
          listOrScalarOnly(b.second, spec.pkgAdd, "pkg/add");
          std::cerr << "pkg/add tag is currently broken" << std::endl;
          abort();
        } else if (isKey(b, "nuke")) {
          listOrScalarOnly(b.second, spec.pkgNuke, "pkg/nuke");
        } else {
          ERR("unknown element pkg/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "run")) {
      for (auto b : k.second) {
        if (isKey(b, "command")) {
          std::string command;
          scalar(b.second, command, "run/command");
          auto space = command.find(' ');
          if (space == std::string::npos)
            spec.runCmdExecutable = command;
          else {
            spec.runCmdExecutable = command.substr(0, space);
            spec.runCmdArgs = command.substr(space);
          }
        } else if (isKey(b, "service")) {
          listOrScalarOnly(b.second, spec.runServices, "run/service");
        } else {
          ERR("unknown element run/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "dirs")) {
      for (auto b : k.second) {
        if (isKey(b, "share")) {
          if (b.second.IsSequence()) {
            for (auto oneShare : b.second) {
              if (oneShare.IsScalar())
                spec.dirsShare.push_back({AsString(oneShare), AsString(oneShare)});
              else if (oneShare.IsSequence() && oneShare.size() == 2) {
                spec.dirsShare.push_back({AsString(oneShare[0]), AsString(oneShare[1])});
              } else {
                ERR("elements of the dirs/share list have to be scalars or lists of size two (fromDir, toDir)")
              }
            }
          } else {
            ERR("dirs/share has to be a list")
          }
        } else {
          ERR("unknown element dirs/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "files")) {
      for (auto b : k.second) {
        if (isKey(b, "share")) {
          if (b.second.IsSequence()) {
            for (auto oneShare : b.second) {
              if (oneShare.IsScalar())
                spec.filesShare.push_back({AsString(oneShare), AsString(oneShare)});
              else if (oneShare.IsSequence() && oneShare.size() == 2) {
                spec.filesShare.push_back({AsString(oneShare[0]), AsString(oneShare[1])});
              } else {
                ERR("elements of the files/share list have to be scalars or lists of size two (fromFile, toFile)")
              }
            }
          }
        }
      }
    } else if (isKey(k, "options")) {
      if (listOrScalar(k.second, spec.options, "options")) {
        // options are a list (simplified format): set details to options that support them
        auto itNet = spec.options.find("net");
        if (itNet != spec.options.end())
          itNet->second.reset(Spec::NetOptDetails::createDefault()); // default "net" option details
        auto itTor = spec.options.find("tor");
        if (itTor != spec.options.end()) {
          itTor->second.reset(Spec::TorOptDetails::createDefault()); // default "tor" option details
          spec.options["net"].reset(new Spec::NetOptDetails); // blank "net" option details
          spec.optionNetWr()->outboundWan = true; // only WAN, DNS isn't needed for Tor
        }
      } else if (k.second.IsMap()) {
        // options are a map: they are in the extended format, parse them in a custom fashion, one by one
        std::set<std::string> opts;
        for (auto lo : k.second) {
          auto soptName = AsString(lo.first);
          if (!lo.second.IsMap() && !lo.second.IsNull())
            ERR("options/" << soptName << " value must be a map or empty when options are in the extended format")
          opts.insert(soptName);
        }
        for (auto &soptName : allOptionsLst)
          if (opts.find(soptName) != opts.end()) {
            const auto &soptVal = k.second[soptName];
            auto &optVal = spec.options[soptName];
            if (soptName == "net") {
              if (soptVal.IsMap()) {
                optVal.reset(new Spec::NetOptDetails); // blank "net" option details
                auto optNetDetails = static_cast<Spec::NetOptDetails*>(optVal.get());
                for (auto netOpt : soptVal) {
                  if (AsString(netOpt.first) == "outbound") {
                    std::set<std::string> outboundSet;
                    listOrScalarOnly(netOpt.second, outboundSet, "net/outbound");
                    for (auto &v : outboundSet)
                      if (v == "all") {
                        if (outboundSet.size() > 1)
                          ERR("net/outbound contains other elements besides 'all'")
                        optNetDetails->outboundWan = true;
                        optNetDetails->outboundLan = true;
                        optNetDetails->outboundHost = true;
                        optNetDetails->outboundDns = true;
                      } else if (v == "none") {
                        if (outboundSet.size() > 1)
                          ERR("net/outbound contains other elements besides 'none'")
                      } else if (v == "wan") {
                        optNetDetails->outboundWan = true;
                      } else if (v == "lan") {
                        optNetDetails->outboundLan = true;
                      } else if (v == "host") {
                        optNetDetails->outboundHost = true;
                      } else if (v == "dns") {
                        optNetDetails->outboundDns = true;
                      } else {
                        ERR("net/outbound contains the unknown element '" << v << "'")
                      }
                  } else if (AsString(netOpt.first) == "inbound-tcp") {
                    if (listOrScalar(netOpt.second, optNetDetails->inboundPortsTcp, "options")) {
                    } else if (netOpt.second.IsMap()) {
                      for (auto portsPair : netOpt.second)
                        optNetDetails->inboundPortsTcp.push_back({parsePortRange(portsPair.first.as<std::string>()), parsePortRange(portsPair.second.as<std::string>())});
                    } else {
                      ERR("options/net/inbound-tcp value must be an array, a scalar or a map")
                    }
                  } else if (AsString(netOpt.first) == "inbound-udp") {
                    if (listOrScalar(netOpt.second, optNetDetails->inboundPortsUdp, "options")) {
                    } else if (netOpt.second.IsMap()) {
                      for (auto portsPair : netOpt.second)
                        optNetDetails->inboundPortsUdp.push_back({parsePortRange(portsPair.first.as<std::string>()), parsePortRange(portsPair.second.as<std::string>())});
                    } else {
                      ERR("options/net/inbound-udp value must be an array, a scalar or a map")
                    }
                  } else
                    ERR("the invalid value options/net/" << netOpt.first << " supplied")
                }
              } else
                optVal.reset(Spec::NetOptDetails::createDefault()); // default "net" option details
            } else if (soptName == "tor") { // ASSUME that the "tor" option is after the "net" option
              optVal.reset(new Spec::TorOptDetails); // blank "tor" option details
              if (soptVal.IsMap()) {
                auto optTorDetails = static_cast<Spec::TorOptDetails*>(optVal.get());
                for (auto torOpt : soptVal) {
                  if (AsString(torOpt.first) == "control-port") {
                    if (!YAML::convert<bool>::decode(torOpt.second, optTorDetails->controlPort))
                      ERR("options/tor/control-port can't be converted to boolean: " << torOpt.second.as<std::string>())
                  } else
                    ERR("the invalid value options/tor/" << torOpt.first << " supplied")
                }
              }
              // always set options/net/wan for tor
              if (!spec.optionExists("net"))
                spec.options["net"].reset(new Spec::NetOptDetails); // blank "net" option details
              spec.optionNetWr()->outboundWan = true; // only WAN, DNS isn't needed for Tor
            } else {
              if (!soptVal.IsNull())
                ERR("options/* values must be empty when options are in the extended format")
            }
        }
      } else {
        ERR("options are not scalar, list or map")
      }
    } else if (isKey(k, "scripts")) {
      if (!k.second.IsMap())
        ERR("scripts must be a map")
        for (auto secScripts : k.second) {
          const auto section = AsString(secScripts.first);
          if (spec.scripts.find(section) != spec.scripts.end())
            ERR("duplicate 'scripts/" << section << "'")
          spec.scripts[AsString(secScripts.first)] = parseScriptsSection(section, secScripts.second);
        }
    } else {
      ERR("unknown top-level element '" << k.first << "' in spec")
    }
  }

  return spec;
}

