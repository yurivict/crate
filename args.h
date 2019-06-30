#pragma once

#include <string>

enum Cmd {CmdNone, CmdCreate, CmdRun};

class Args {
public:
  Cmd cmd;

  // create parameters
  std::string createSpec;
  std::string createOutput;

  // run parameters
  //std::string createSpec;

  void validate();
};

Args parseArguments(int argc, char** argv);
