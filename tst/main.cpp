// Host test runner entry point. Compiles to build/tests/host via `make test`.
#include "test.hpp"
#include "fp_test.hpp"
#include "input_test.hpp"
#include "player_test.hpp"
#include "monster_test.hpp"
#include "shells_test.hpp"
#include "world_test.hpp"

int main() {
  TestRunner runner;
  FpSuite(runner);
  InputSuite(runner);
  PlayerSuite(runner);
  MonsterSuite(runner);
  ShellSuite(runner);
  WorldSuite(runner);
  runner.printSummary();
  return runner.fail() ? 1 : 0;
}