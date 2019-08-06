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
  // escape in order to be single-quoted, and run by /bin/sh via system(3)
  // XXX not sure if this is a correct escaping procedure, because it isn't clear how system(3) interprets the string
  std::ostringstream ss;
  for (auto chr : script)
    switch (chr) {
    case '\'':
      ss << "'\\''";
      break;
    case '$':
    case '#':
    case '"':
      ss << "\\";
    default:
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
    for (auto &script : it->second)
      fnRunner(STR("/bin/sh -c '" << escape(script.second) << "'"));
}

}
