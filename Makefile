.PHONY :  full build mini gen test fxtest fxtest-headless fxtest-headless-preflight fxtest-build fxtest-run

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

# Ardens device-serial harness, mirroring ~/code/CreatureGathererFX Makefile
# targets: stage each sketch under build/fxtest/<name>, compile with
# arduino-cli, boot the hex + FX image in Ardens, captureserial the output,
# normalize CRLF, and require a final bare `P`/`F` marker.
ARDENS        ?= $(HOME)/code/Ardens/build/Ardens.app/Contents/MacOS/Ardens
ARDUINO_CLI   ?= arduino-cli
FQBN          ?= arduboy-homemade:avr:arduboy-fx
FXTEST_MS     ?= 3000
FXDATA_BIN    ?= fxdata/fxdata.bin
FXTEST_BUILD_DIR ?= build/fxtest
FXTEST_INOS   = $(wildcard tst/fxdatatest/test_*.ino)
FXTEST_NAMES  = $(basename $(notdir $(FXTEST_INOS)))

fxtest: fxtest-headless

fxtest-headless:
	@if [ -z "$(ARDENS)" ] || [ ! -x "$(ARDENS)" ]; then \
		echo "fxtest-headless: SKIPPED (Ardens unavailable at $(ARDENS); set ARDENS=/path/to/Ardens to run serial device tests)"; \
	else \
		$(MAKE) --no-print-directory fxtest-headless-preflight fxtest-build fxtest-run; \
	fi

fxtest-headless-preflight:
	@test -x "$(ARDENS)" || { echo "fxtest-headless: Ardens executable not found at $(ARDENS); set ARDENS=/path/to/Ardens" >&2; exit 1; }
	@test -f "$(FXDATA_BIN)" || { echo "fxtest-headless: FX data image missing at $(FXDATA_BIN); run make gen or set FXDATA_BIN=/path/to/fxdata.bin" >&2; exit 1; }
	@strings "$(ARDENS)" | grep -Fxq captureserial || { echo "fxtest-headless: BLOCKED (Ardens at $(ARDENS) lacks captureserial)" >&2; exit 2; }

fxtest-build:
	@set -e; \
	for ino in $(FXTEST_NAMES); do \
		stage="$(FXTEST_BUILD_DIR)/$$ino"; \
		rm -rf "$$stage"; \
		mkdir -p "$$stage"; \
		cp -R src "$$stage/src"; \
		cp "tst/fxdatatest/$$ino.ino" "$$stage/"; \
		cp tst/fxdatatest/*.hpp "$$stage/"; \
		cp -R tst/fxdatatest/harness "$$stage/harness"; \
		echo $$ino; \
		$(ARDUINO_CLI) compile --fqbn "$(FQBN)" \
		    --optimize-for-debug --output-dir "$$stage/output" \
		    "$$stage/$$ino.ino"; \
	done

fxtest-run:
	@failed=0; \
	for name in $(FXTEST_NAMES); do \
		stage="$(FXTEST_BUILD_DIR)/$$name"; \
		echo "=== $$name ==="; \
		cp -f "$(FXDATA_BIN)" "$$stage/fxdata.bin"; \
		if out="$$($(ARDENS) captureserial=$(FXTEST_MS) fxport=d1 display=ssd1306 file=$$stage/output/$$name.ino.hex file=$$stage/fxdata.bin 2>&1)"; then \
			runner_status=0; \
		else \
			runner_status=$$?; \
		fi; \
		printf '%s\n' "$$out"; \
		normalized_out="$$(printf '%s\n' "$$out" | tr -d '\r')"; \
		if [ -z "$$out" ]; then \
			echo "$$name: FAIL (no serial: crash, hang, or ROM not loaded)"; \
			failed=1; \
		elif printf '%s\n' "$$normalized_out" | grep -qx 'F'; then \
			echo "$$name: FAIL"; \
			failed=1; \
		elif [ "$$runner_status" -ne 0 ]; then \
			echo "$$name: FAIL (Ardens exited $$runner_status)"; \
			failed=1; \
		elif printf '%s\n' "$$normalized_out" | grep -qx 'P'; then \
			echo "$$name: PASS"; \
		else \
			echo "$$name: FAIL (missing P/F marker; capture may be truncated, raise FXTEST_MS)"; \
			failed=1; \
		fi; \
	done; \
	test "$$failed" -eq 0
