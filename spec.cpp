#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml-cpp/yaml.h>
#include <rang.hpp>

#include "spec.h"

#include <string>
#include <iostream>

#define ERR(msg...) \
  { \
    std::cerr << rang::fg::red << "spec parser: " << msg << rang::style::reset << std::endl; \
    exit(1); \
  }

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

  // parse the spec in yaml format
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
    } else if (isKey(k, "options")) {
      for (auto o : k.second)
        spec.options.insert(o.template as<std::string>());
    } else {
      ERR("unknown top-level element '" << k.first << "' in spec")
    }
  }

  return spec;
}

void Spec::validate() const {
  if (!runExecutable.empty()) {
    if (runExecutable[0] != '/')
      ERR("the executable path has to begin with '/', executable=" << runExecutable)
  }
  for (auto &o : options)
    if (o != "x11")
      ERR("the unknown option '" << o << "' is supplied")
}

