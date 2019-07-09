#pragma once

#include <string>

enum Command {CmdNone, CmdCreate, CmdRun};

class Args {
public:
  Args() : logProgress (false) { }

  Command cmd;

  // general params
  bool logProgress; // log progress

  // create parameters
  std::string createSpec;
  std::string createOutput;

  // run parameters
  std::string runCrateFile;

  void validate();
};

Args parseArguments(int argc, char** argv, unsigned &processed);
