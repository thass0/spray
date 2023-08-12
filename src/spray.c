/* 🐛🐛🐛 Spray: an ergonomic debugger for x86_64 Linux. 🐛🐛🐛 */

#include "debugger.h"

#define SET_ARGS_ONCE
#include "args.h"

int setup_args(int argc, char **argv) {
  Args args = {0};

  if (parse_args(argc, argv, &args)) {
    print_help_message(prog_name_arg(argc, argv));
    return -1;
  } else {
    set_args(&args);
    return 0;
  }
}

int main(int argc, char **argv) {
  if (setup_args(argc, argv) == -1) {
    return -1;
  }

  Debugger debugger;

  if (setup_debugger(get_args()->file, get_args()->args, &debugger) == -1) {
    return -1;
  }

  run_debugger(debugger);

  return 0;
}
