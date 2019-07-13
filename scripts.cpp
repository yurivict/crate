#include "scripts.h"
#include "util.h"

#include <string>
#include <map>
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
    for (auto &script : it->second)
      fnRunner(STR("/bin/sh -c \"" << escape(script.second) << "\""));
}

}
