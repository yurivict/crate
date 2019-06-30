#pragma once

#include <string>

namespace CrateFile {
  void create(const std::string &jailPath, const std::string &crateFilePath);
  void extract(const std::string &jailPath, const std::string &crateFilePath);
};
