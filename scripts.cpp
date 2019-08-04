// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "scripts.h"
#include "util.h"

#include <rang.hpp>

#include <string>
#include <map>
#include <iostream>
#include <ostream>

namespace Scripts {

//
// helpers
//

static std::string escape(const std::string &script) {
  std::ostringstream ss;
  for (auto chr : script) {
    switch (chr) {
    case '"':
    case '\\':
      ss << '\\';
    }
    ss << chr;
  }
  return ss.str();
}

//
// interface
//

void section(const char *sec, const std::map<std::string, std::map<std::string, std::string>> &scripts, FnRunner fnRunner) {
  auto it = scripts.find(sec);
  if (it != scripts.end())
    for (auto &script : it->second) {
      std::cout << rang::fg::cyan << "@run-script#" << sec << "#" << script.first << "#begin" << rang::style::reset << std::endl;
      fnRunner(STR("/bin/sh -c \"" << escape(script.second) << "\""));
      std::cout << rang::fg::cyan << "@run-script#" << sec << "#" << script.first << "#end" << rang::style::reset << std::endl;
    }
}

}
