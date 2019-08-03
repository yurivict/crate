#pragma once

#include <exception>
#include <string>

#include <rang.hpp>

#include "util.h"

class Exception : public std::exception {
  std::string xmsg;
public:
  Exception(const std::string &loc, const std::string &msg);

  const char* what() const throw();
};


#define ERR2(loc, msg...) \
  throw Exception(loc, STR(msg));

#define WARN(msg...) \
  std::cerr << rang::fg::yellow << msg << rang::style::reset << std::endl;
