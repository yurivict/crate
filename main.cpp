#include <iostream>

#include "args.h"
#include "commands.h"

int main(int argc, char** argv) {

  Args args = parseArguments(argc, argv);
  std::cout << "--parsed arguments--" << std::endl;

  bool succ = false;

  switch (args.cmd) {
  case CmdCreate:
    succ = createCrate(args);
    break;
  case CmdRun:
    //succ = runCrate(args);
    break;
  case CmdNone:
    break; // impossible
  }
}
