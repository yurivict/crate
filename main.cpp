
#include "args.h"
#include "spec.h"
#include "util.h"
#include "misc.h"
#include "commands.h"

#include <rang.hpp>

#include <unistd.h>

#include <iostream>

int main(int argc, char** argv) {

  //
  // can only run as a privileged user because we need to run chroot(8) and need to create jails
  //
  if (geteuid() != 0) {
    std::cerr << rang::fg::red << "crate has to run as root user" << rang::style::reset << std::endl;
    return 1;
  }

  //
  // adjust uid, make it equal to euid
  //
  Util::ckSyscallError(::setuid(::geteuid()), "setuid", "geteuid()");

  //
  // create the jails directory if it doesn't yet exist
  //
  createJailsDirectoryIfNeeded();

  //
  // parse the arguments
  //
  unsigned numArgsProcessed = 0;
  Args args = parseArguments(argc, argv, numArgsProcessed);
  args.validate();

  //
  // run the requested command
  //
  bool succ = false;
  int returnCode = 0;

  switch (args.cmd) {
  case CmdCreate: {
    auto spec = parseSpec(args.createSpec);
    spec.validate();
    succ = createCrate(args, spec.preprocess());
    break;
  } case CmdRun: {
    succ = runCrate(args, argc - numArgsProcessed, argv + numArgsProcessed, returnCode);
    break;
  } case CmdNone: {
    break; // impossible
  }}

  return succ ? (returnCode <= 255 ? returnCode : 255) : 1; // not sure why sometimes returnCode=255
}
