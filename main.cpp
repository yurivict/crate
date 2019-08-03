
#include "args.h"
#include "spec.h"
#include "util.h"
#include "err.h"
#include "misc.h"
#include "commands.h"

#include <rang.hpp>

#include <unistd.h>

#include <iostream>

static int mainGuarded(int argc, char** argv) {

  //
  // can only run as a privileged user because we need to run chroot(8) and need to create jails
  //
  if (::geteuid() != 0) {
    std::cerr << rang::fg::red << "crate has to run as a regular user setuid to root"
                               << " (you ran it just as a regular user with UID=" << ::geteuid() << ")"
                               << rang::style::reset << std::endl;
    return 1;
  }
  if (::getuid() == 0) {
    std::cerr << rang::fg::red << "crate has to run as a regular user setuid to root"
                               << " (you ran it just as root, this isn't yet supported)"
                               << rang::style::reset << std::endl;
    return 1;
  }

  //
  // Can't run in jail because we need to create jails ourselves
  //
  if (Util::getSysctlInt("security.jail.jailed") != 0) {
    std::cerr << rang::fg::red << "crate can not run in jail" << rang::style::reset << std::endl;
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
    createCacheDirectoryIfNeeded();
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

int main(int argc, char** argv) {
  try {
    return mainGuarded(argc, argv);
  } catch (const Exception &e) {
    std::cerr << rang::fg::red << e.what() << rang::style::reset << std::endl;
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "FIXME(EXCEPTION std::exception): " << rang::fg::red << e.what() << rang::style::reset << std::endl;
    return 1;
  } catch (...) {
    std::cerr << rang::fg::red << "XXX UNKNOWN EXCEPTION IS CAUGHT" << rang::style::reset << std::endl;
    return 1;
  }
}
