
#include "spec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml-cpp/yaml.h>
#include <rang.hpp>

#include <string>
#include <set>
#include <iostream>

#define ERR(msg...) \
  { \
    std::cerr << rang::fg::red << "spec parser: " << msg << rang::style::reset << std::endl; \
    exit(1); \
  }

static std::set<std::string> allOptions = {"x11", "net", "ssl-certs"};

Spec parseSpec(const std::string &fname) {

  Spec spec;

  // helper functions
  auto isKey = [](auto &k, const char *s) {
    return k.first.template as<std::string>() == s;
  };
  auto scalar = [](auto &node, std::string &out, const char *name) {
    if (node.IsScalar()) {
      out = node.template as<std::string>();
    } else {
      ERR("unsupported " << name << " object type " << node.Type())
    }
  };
  auto listOrScalar = [](auto &node, std::vector<std::string> &out, const char *name) {
    if (node.IsSequence()) {
      for (auto r : node)
        out.push_back(r.template as<std::string>());
    } else if (node.IsScalar()) {
      out.push_back(node.template as<std::string>());
    } else {
      ERR("unsupported " << name << " object type " << node.Type())
    }
  };

  // parse the spec in the yaml format
  YAML::Node top = YAML::LoadFile(fname);

  // top-level tags
  for (auto k : top) {
    if (isKey(k, "base")) {
      for (auto b : k.second) {
        if (isKey(b, "keep")) {
          listOrScalar(b.second, spec.baseKeep, "base/keep");
        } else if (isKey(b, "remove")) {
          listOrScalar(b.second, spec.baseRemove, "base/remove");
        } else {
          ERR("unknown element base/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "pkg")) {
      for (auto b : k.second) {
        if (isKey(b, "install")) {
          listOrScalar(b.second, spec.pkgInstall, "pkg/install");
        } else if (isKey(b, "add")) {
          listOrScalar(b.second, spec.pkgAdd, "pkg/add");
          std::cerr << "pkg/add tag is currently broken" << std::endl;
          abort();
        } else {
          ERR("unknown element pkg/" << b.first << " in spec")
        }
      }
    } else if (isKey(k, "run")) {
      for (auto b : k.second) {
        if (isKey(b, "executable")) {
          scalar(b.second, spec.runExecutable, "run/executable");
        } else if (isKey(b, "service")) {
          listOrScalar(b.second, spec.runServices, "run/service");
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
                spec.dirsShare.push_back({oneShare.template as<std::string>(), oneShare.template as<std::string>()});
              else if (oneShare.IsSequence() && oneShare.size() == 2) {
                spec.dirsShare.push_back({oneShare[0].template as<std::string>(), oneShare[1].template as<std::string>()});
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
      for (auto o : k.second)
        spec.options.insert(o.template as<std::string>());
    } else {
      ERR("unknown top-level element '" << k.first << "' in spec")
    }
  }

  return spec;
}

// preprocess function processes some options, etc. for simplicity of use both by users and by out 'create' module
Spec Spec::preprocess() const {
  Spec spec = *this;

  // expand the ssl-certs option
  if (spec.optionExists("ssl-certs")) {
    spec.options.erase("ssl-certs");
    spec.pkgInstall.push_back("ca_root_nss");
  }

  return spec;
}

void Spec::validate() const {
  auto isFullPath = [](const std::string &path) {
    return path[0] == '/';
  };
  if (!runExecutable.empty()) {
    if (!isFullPath(runExecutable))
      ERR("the executable path has to be a full path, executable=" << runExecutable)
  }
  for (auto &dirShare : dirsShare)
    if (!isFullPath(dirShare.first) || !isFullPath(dirShare.second))
      ERR("the shared directory paths have to be a full paths, share=" << dirShare.first << "->" << dirShare.second)
  for (auto &o : options)
    if (allOptions.find(o) == allOptions.end())
      ERR("the unknown option '" << o << "' is supplied")
}

