
#include "spec.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml-cpp/yaml.h>
#include <rang.hpp>

#include <string>
#include <set>
#include <map>
#include <iostream>
#include <sstream>

#include "lst-all-script-sections.h" // generated from create.cpp and run.cpp by the Makefile

#define ERR(msg...) \
  { \
    std::cerr << rang::fg::red << "spec parser: " << msg << rang::style::reset << std::endl; \
    exit(1); \
  }

// various sets
static std::set<std::string> allOptions = {"x11", "net", "ssl-certs", "video", "gl", "dbg-ktrace"};

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

  static const char *errMsg = "scripts should be scalars, arrays of scalars, arrays of arrays of scalars, or maps of scalars or of arrays of scalars";

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

//
// interface
//

Spec parseSpec(const std::string &fname) {

  Spec spec;

  // helper functions
  auto isKey = [](auto &k, const char *s) {
    return AsString(k.first) == s;
  };
  auto scalar = [](auto &node, std::string &out, const char *name) {
    if (node.IsScalar()) {
      out = AsString(node);
    } else {
      ERR("unsupported " << name << " object type " << node.Type())
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
      ERR("unsupported " << opath << " object type " << node.Type())
  };

  // parse the spec in the yaml format
  YAML::Node top = YAML::LoadFile(fname);

  // top-level tags
  for (auto k : top) {
    if (isKey(k, "base")) {
      for (auto b : k.second) {
        if (isKey(b, "keep")) {
          listOrScalarOnly(b.second, spec.baseKeep, "base/keep");
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
        if (isKey(b, "executable")) {
          scalar(b.second, spec.runExecutable, "run/executable");
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
    } else if (isKey(k, "options")) {
      if (!listOrScalar(k.second, spec.options, "pkg/options"))
        ERR("options are not scalar or list")
    } else if (isKey(k, "scripts")) {
      if (!k.second.IsMap())
        ERR("scripts should be a map")
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

// preprocess function processes some options, etc. for simplicity of use both by users and by out 'create' module
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

  // should be something to run
  if (runExecutable.empty() && runServices.empty())
    ERR("crate has to have either the executable to run, some services to run, or both, it can't have nothing to do")

  // should be no conflicting package local overrides
  if (!pkgLocalOverride.empty()) {
    std::map<std::string, std::string> pkgs;
    for (auto &lo : pkgLocalOverride) {
      if (pkgs.find(lo.first) != pkgs.end())
        ERR("duplicate local override packages: " << lo.first << "->" << pkgs[lo.first] << " and " << lo.first << "->" << lo.second)
      pkgs[lo.first] = lo.second;
    }
  }

  // executable must have full path
  if (!runExecutable.empty()) {
    if (!isFullPath(runExecutable))
      ERR("the executable path has to be a full path, executable=" << runExecutable)
  }

  // shared directories must be full paths
  for (auto &dirShare : dirsShare)
    if (!isFullPath(dirShare.first) || !isFullPath(dirShare.second))
      ERR("the shared directory paths have to be a full paths, share=" << dirShare.first << "->" << dirShare.second)

  // options must be from the supported set
  for (auto &o : options)
    if (allOptions.find(o) == allOptions.end())
      ERR("the unknown option '" << o << "' was supplied")

  // script sections must be from the supported set
  for (auto &s : scripts)
    if (s.first.empty() || allScriptSections.find(s.first) == allScriptSections.end())
      ERR("the unknown script section '" << s.first << "' was supplied")
}

