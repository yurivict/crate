#pragma once


#define STR(msg...) \
  [=]() { \
    std::ostringstream ss; \
    ss << msg; \
    return ss.str(); \
  }()
