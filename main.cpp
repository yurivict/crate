#include <iostream>

#include "args.h"
#include "spec.h"
#include "commands.h"

int main(int argc, char** argv) {

  //
  // parse the arguments
  //
  Args args = parseArguments(argc, argv);
  args.validate();

  //
  // run the requested command
  //
  bool succ = false;

  switch (args.cmd) {
  case CmdCreate: {
    auto spec = parseSpec(args.createSpec);
    spec.validate();
    succ = createCrate(args, spec);
    break;
  } case CmdRun: {
    //succ = runCrate(args);
    break;
  } case CmdNone: {
    break; // impossible
  }}

  return succ ? 0 : 1;
}
