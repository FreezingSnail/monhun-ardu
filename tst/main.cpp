// Host test runner entry point. Compiles to build/tests/host via `make test`.
#include "test.hpp"
#include "fp_test.hpp"

int main() {
  TestRunner runner;
  FpSuite(runner);
  runner.printSummary();
  return runner.fail() ? 1 : 0;
}