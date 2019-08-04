// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#pragma once

#include <string>
#include <map>
#include <functional>

namespace Scripts {

typedef std::function<void(const std::string &)> FnRunner;

void section(const char *sec, const std::map<std::string, std::map<std::string, std::string>> &scripts, FnRunner fnRunner);

}
