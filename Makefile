.PHONY :  test  fxtest

# Common compiler flags
CXX_FLAGS = -std=c++17 -I/src -w -O0 -g3
TEST_FLAGS = -DTEST
DEBUG_FLAGS = -DDEBUG

# Common source files for main tests (host tests live in tst/, binary in build/)
TEST_SOURCES = tst/main.cpp

# Host test output binary (build/ is gitignored; never leave artifacts in tst/)
TEST_OUT = build/tests/host

# Function to run tests
define run_test
	mkdir -p $(dir $(TEST_OUT))
	g++ $(1) $(CXX_FLAGS) $(2) $(3) -o $(TEST_OUT) && ./$(TEST_OUT)
endef

full: gen build

build:
	arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist

mini:
	arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-mini" --optimize-for-debug  --output-dir dist

gen:
	./tools/gen.sh

test:
	$(call run_test,,$(TEST_FLAGS),$(TEST_SOURCES))

test-debug:
	$(call run_test,$(DEBUG_FLAGS),$(TEST_FLAGS),$(TEST_SOURCES))

testvm:
	$(call run_test,,$(TEST_FLAGS),$(TESTVM_SOURCES))

testvm-debug:
	$(call run_test,$(DEBUG_FLAGS),$(TEST_FLAGS),$(TESTVM_SOURCES))

FXDATA_BIN        = dist/fxdata.bin
INTEGRATION_TESTS = /Users/connorfranc/code/Ardens/build/integration_tests
FXTEST_INOS       = $(wildcard tst/fxdatatest/test_*.ino)
FXTEST_NAMES      = $(basename $(notdir $(FXTEST_INOS)))

.PHONY: fxtest fxtest-build fxtest-run

fxtest: fxtest-build fxtest-run

fxtest-build:
	for ino in $(FXTEST_NAMES); do \
		rm -rf tst/fxdatatest/$$ino && mkdir tst/fxdatatest/$$ino ; \
		cp -r src tst/fxdatatest/$$ino/src ; \
		cp tst/fxdatatest/$$ino.ino tst/fxdatatest/$$ino/ ; \
		cp tst/fxdatatest/*.hpp tst/fxdatatest/$$ino/ ; \
		echo $$ino ; \
		arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" \
		    --optimize-for-debug --output-dir tst/fxdatatest \
		    tst/fxdatatest/$$ino/$$ino.ino ; \
	done

fxtest-run:
	echo $(FXTEST_NAMES);
	echo "---";
	@for name in $(FXTEST_NAMES); do \
		echo $$name; \
		echo $(INTEGRATION_TESTS) --serial-test $$name tst/fxdatatest/$$name.ino.hex $(FXDATA_BIN); \
		$(INTEGRATION_TESTS) --serial-test $$name tst/fxdatatest/$$name.ino.hex $(FXDATA_BIN); \
	done
