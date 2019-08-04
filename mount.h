// Copyright (C) 2019 by Yuri Victorovich. All rights reserved.

#pragma once

//
// Mount: allows to mount/unmount directories, and it auto-unmounts them from its destructor on error
//

#include <string>

class Mount {
  bool mounted = false;
  const char *fstype;
  std::string fspath;
  std::string target;

public:
  Mount(const char *newFstype, const std::string &newFspath, const std::string &newTarget);
  ~Mount();

  void mount();
  void unmount(bool doThrow = true); // unmount is normally called individually, or as part of a destructor when exception has orrurred
};
