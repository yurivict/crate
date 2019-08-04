// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#include "err.h"
#include "util.h"


Exception::Exception(const std::string &loc, const std::string &msg)
: xmsg(STR(loc << ": " << msg))
{ }

const char* Exception::what() const throw() {
  return xmsg.c_str();
}
