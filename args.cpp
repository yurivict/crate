
#include "args.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <iostream>

//
// internals
//

static bool strEq(const char *s1, const char *s2) {
  return strcmp(s1, s2) == 0;
}

static void usage() {
  std::cout << "usage: crate [-h|--help] COMMAND [...command arguments...]" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, --help                 show this help screen" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << "  create                     creates a container (run 'crate create -h' for details)" << std::endl;
  std::cout << "  run                        runs the containerzed application (run 'crate run -h' for details)" << std::endl;
  std::cout << "" << std::endl;
}

static void usageCreate() {
  std::cout << "usage: crate create [-s <spec-file>|--spec <spec-file>] [-o <output-create-file>|--output <output-create-file>]" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -s, --spec <spec-file>             crate specification (required)" << std::endl;
  std::cout << "  -o, --output <output-create-file>  output crate file" << std::endl;
  std::cout << "  -h, --help                         show this help screen" << std::endl;
  std::cout << "" << std::endl;
}

static void usageRun() {
  std::cout << "usage: crate run [-h|--help] <create-file>" << std::endl;
  std::cout << "" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -h, --help                         show this help screen" << std::endl;
  std::cout << "" << std::endl;
}

static void err(const char *msg) {
  fprintf(stderr, "failed to parse arguments: %s\n", msg);
  std::cout << "" << std::endl;
  usage();
  exit(1);
}

static void err(const char *fmt, const char *arg) {
  fprintf(stderr, "failed to parse arguments: ");
  fprintf(stderr, fmt, arg);
  fprintf(stderr, "\n");
  std::cout << "" << std::endl;
  usage();
  exit(1);
}

static const char isShort(const char* arg) {
  if (arg[0] == '-' && (isalpha(arg[1]) || isdigit(arg[1])) && arg[2] == 0)
    return arg[1];
  return 0;
}

static const char* isLong(const char* arg) {
  if (arg[0] == '-' && arg[1] == '-') {
    for (int i = 2; arg[i]; i++)
      if (!islower(arg[i]) || !isdigit(arg[i]))
        return nullptr;
    return arg + 2;
  }

  return nullptr;
}

static Cmd isCommand(const char* arg) {
  if (strEq(arg, "create"))
    return CmdCreate;
  if (strEq(arg, "run"))
    return CmdRun;

  return CmdNone;
}

static const char* getArgParam(int aidx, int argc, char** argv) {
  if (aidx >= argc)
    err("argument parameter expected but no more arguments were supplied");
  if (argv[aidx][0] == '-')
    err("argument parameter can't begin from the hyphen");
  return argv[aidx];
}

//
// interface
//

Args parseArguments(int argc, char** argv) {
  Args args;

  enum Loc {LocBeforeCmd, LocAfterCmd};
  Loc loc = LocBeforeCmd;
  for (unsigned a = 1; a < argc; a++) {
    switch (loc) {
    case LocBeforeCmd:
      if (auto argShort = isShort(argv[a])) {
        switch (argShort) {
        case 'h':
          usage();
          exit(0);
        default:
          err("unsupported short option '%s'", argv[a]);
        }
      } else if (auto argLong = isLong(argv[a])) {
        std::cout << "argLong=" << argLong << std::endl;
        if (strEq(argLong, "help")) {
          usage();
          exit(0);
        } else {
          err("unsupported long option '%s'", argv[a]);
        }
      } else if (auto cmd = isCommand(argv[a])) {
        args.cmd = cmd;
        loc = LocAfterCmd;
        break;
      } else {
        err("unknown argument '%s'", argv[a]);
      }

    case LocAfterCmd:
      switch (args.cmd) {
      case CmdNone:
        // impossible
        break;
      case CmdCreate:
        if (auto argShort = isShort(argv[a])) {
          switch (argShort) {
          case 'h':
            usageCreate();
            exit(0);
          case 's':
            args.createSpec = getArgParam(++a, argc, argv);
            break;
          case 'o':
            args.createOutput = getArgParam(++a, argc, argv);
            break;
          default:
            err("unsupported short option '%s'", argv[a]);
          }
        } else if (auto argLong = isLong(argv[a])) {
          std::cout << "argLong=" << argLong << std::endl;
          if (strEq(argLong, "help")) {
            usageCreate();
            exit(0);
          } else if (strEq(argLong, "spec")) {
            args.createSpec = getArgParam(++a, argc, argv);
            break;
          } else if (strEq(argLong, "output")) {
            args.createOutput = getArgParam(++a, argc, argv);
            break;
          } else {
            err("unsupported long option '%s'", argv[a]);
          }
        } else {
          err("unknown argument '%s'", argv[a]);
        }
        break;
      case CmdRun:
        if (auto argShort = isShort(argv[a])) {
          switch (argShort) {
          case 'h':
            usageRun();
            exit(0);
          default:
            err("unsupported short option '%s'", argv[a]);
          }
        } else if (auto argLong = isLong(argv[a])) {
          std::cout << "argLong=" << argLong << std::endl;
          if (strEq(argLong, "help")) {
            usage();
            exit(0);
          } else {
            err("unsupported long option '%s'", argv[a]);
          }
        } else {
          err("unknown argument '%s'", argv[a]);
        }
      }
    }
  }

  return args;
}

