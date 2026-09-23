.PHONY :  full build mini dev gen gen-check size size-line debug hooks format test test-tools testvm testvm-debug fxtest fxtest-headless fxtest-headless-preflight fxtest-build fxtest-run base-sheet

# Common compiler flags
CXX_FLAGS = -std=c++17 -I/src -w -O0 -g3
# Host suites must exercise every carved path, so force the prg.8/prg.11 trims
# back on for the host build (shipping defaults MH_STAGE3/MH_ROLL_ALT/
# MH_B_BRANCH_BUFFER/MH_PUSH_MOVE to 0; test_parity carves them off and the host
# suite is the coverage for those branches).
TEST_FLAGS = -DTEST -DMH_STAGE3=1 -DMH_ROLL_ALT=1 -DMH_B_BRANCH_BUFFER=1 -DMH_PUSH_MOVE=1
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

# Shipping size flags (monhun-ardu checkpoint review, section 10): -mrelax is
# link-time instruction relaxation, -mcall-prologues shares function
# prologue/epilogue code. Measured -620 B total (29400 -> 28780), perf rMx
# +24 us (5064 vs 7407 budget), parity 660/660, perf suite 5/5.
#
# -DMH_NO_USB (bead monhun-ardu-42n.8): selects the sketch's own USB-free
# main() (monhun-ardu.ino) instead of the core's main.cpp, dropping
# USBDevice/CDC/PluggableUSB from the shipping link. Applied to the shipping
# compile only (build/mini/size/debug inherit these flags). The Ardens fxtest
# build (fxtest-build) is a separate arduino-cli invocation with stock flags, so
# its sketches keep the core main and captureserial still works.
SIZE_FLAGS = --build-property compiler.cpp.extra_flags="-mcall-prologues -mrelax -DMH_NO_USB" \
    --build-property compiler.c.extra_flags="-mrelax" \
    --build-property compiler.c.elf.extra_flags="-mrelax"

build:
	arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug  --output-dir dist $(SIZE_FLAGS)

mini:
	arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-mini" --optimize-for-debug  --output-dir dist $(SIZE_FLAGS)

# Dev feel build (hbk.1): shipping flags + -DMH_DEV=1 -- unlimited crafting,
# fresh 9999-zenny/99-item defaults, EEPROM never touched. Not for releases.
dev:
	arduino-cli compile --fqbn "arduboy-homemade:avr:arduboy-fx" --optimize-for-debug --output-dir dist \
	    --build-property compiler.cpp.extra_flags="-mcall-prologues -mrelax -DMH_NO_USB -DMH_DEV=1" \
	    --build-property compiler.c.extra_flags="-mrelax" \
	    --build-property compiler.c.elf.extra_flags="-mrelax"
	@elf=dist/monhun-ardu.ino.elf; \
	$(AVR_SIZE) -A "$$elf" | awk '$$1==".text"{t=$$2} $$1==".data"{d=$$2} $$1==".bss"{b=$$2} END {flash=t+d; ram=d+b; printf "dev size: flash=%d/%d (%d free)  ram=%d/2560\n", flash, 29696, 29696-flash, ram}'

gen:
	./tools/gen.sh

# Staleness + determinism gate: snapshot every generated artifact, re-run the
# single generation entry (tools/gen.sh), and fail if any artifact changed.
# gen.sh itself records fxdata/manifest.json after validating the
# image<->fxdata declaration mapping, so a stale manifest fails here too.
gen-check:
	@python3 tools/fxdata_manifest.py --snapshot build/gen-check.snapshot.json
	@$(MAKE) --no-print-directory gen
	@python3 tools/fxdata_manifest.py --verify-snapshot build/gen-check.snapshot.json && rm -f build/gen-check.snapshot.json
	@cmp -s fxdata/fxdata.h src/fxdata.h || { echo "gen-check: FAIL header copy drift (fxdata/fxdata.h != src/fxdata.h); run make gen" >&2; exit 1; }

# Whole-image size report (LTO makes per-symbol math useless; measure the ELF):
# .text/.data/.bss, flash/RAM headroom, and the compile-time data facts that
# gate the optional combat machinery (monhun-ardu-ljj.* pattern).
AVR_SIZE ?= $(firstword $(wildcard $(HOME)/Library/Arduino15/packages/arduino/tools/avr-gcc/*/bin/avr-size) avr-size)
size: build
	@elf=dist/monhun-ardu.ino.elf; \
	$(AVR_SIZE) -A "$$elf" | awk ' \
	    $$1==".text"{t=$$2} $$1==".data"{d=$$2} $$1==".bss"{b=$$2} \
	    END { flash=t+d; ram=d+b; \
	          printf "size: .text=%d .data=%d .bss=%d\n", t, d, b; \
	          printf "size: flash=%d/%d (%d free)  ram=%d/2560\n", flash, 29696, 29696-flash, ram }'
	@printf 'size: data facts: '; grep -E '^constexpr bool ' src/generated/combat_meta.hpp | sed 's/constexpr bool //; s/ = /:/; s/;//' | tr '\n' ' '; echo

# Script/checkpoint friendly: only the flash/RAM headroom line (same ELF as
# `size`). Use after each layer in a bead to catch a budget blow-up early
# (AGENTS.md "Dev-cycle speed rules").
size-line: build
	@elf=dist/monhun-ardu.ino.elf; \
	$(AVR_SIZE) -A "$$elf" | awk ' \
	    $$1==".text"{t=$$2} $$1==".data"{d=$$2} $$1==".bss"{b=$$2} \
	    END { flash=t+d; ram=d+b; \
	          printf "size: flash=%d/%d (%d free)  ram=%d/2560\n", flash, 29696, 29696-flash, ram }'

# Install the repo git hooks (.githooks/pre-commit runs clang-format on staged
# C/C++ files and restages them). Idempotent; run once per clone.
hooks:
	git config core.hooksPath .githooks
	chmod +x .githooks/pre-commit
	@echo "git hooks installed (core.hooksPath=.githooks)"

# Format all tracked C-family sources, skipping vendored and generated files.
FORMAT_SKIP = ^(src/external|src/generated|Arduboy-Python-Utilities)/|^(src/fxdata\.h|fxdata/fxdata\.h|tst/fxdatatest/parity_fixtures\.hpp)$$
format:
	@git ls-files '*.hpp' '*.cpp' '*.h' '*.ino' | grep -vE '$(FORMAT_SKIP)' | xargs clang-format -i --style=file
	@echo "clang-format: done"

# Launch the Ardens debugger GUI with the shipping build + FX image.
# Uses the ELF (DWARF debug info: source view, symbols, globals), not the hex.
# Pause/continue F5, reset F8, settings O. Override ARDENS to use another build.
debug: build
	@test -f "dist/monhun-ardu.ino.elf" || { echo "debug: ELF missing at dist/monhun-ardu.ino.elf; run make build" >&2; exit 1; }
	@test -f "$(FXDATA_BIN)" || { echo "debug: FX data image missing at $(FXDATA_BIN); run make gen" >&2; exit 1; }
	"$(ARDENS)" display=ssd1306 fxport=d1 file=dist/monhun-ardu.ino.elf file=$(FXDATA_BIN)

test:
	$(call run_test,,$(TEST_FLAGS),$(TEST_SOURCES))

# Python tooling tests (unittest suite co-located under tools/tests/).
base-sheet:
	python3 tools/gen-base-sheet.py

art-dump:
	python3 tools/gen-art.py --dims build/fxdump.json --dump

test-tools:
	python3 -m unittest discover -s tools/tests -p 'test_*.py' -v

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
# Iterate one suite: `make fxtest-headless FXTEST_ONLY=test_combat` (full gate
# stays the default; the filter is for the inner dev loop).
# test_parity is the legacy mock-derived diagnostics image (AGENTS.md: not a
# gate, fixtures are frozen and not maintained). It is excluded from the default
# run; `FXTEST_ONLY=test_parity` still builds/runs it on demand.
FXTEST_ONLY   ?=
FXTEST_RUN    = $(if $(strip $(FXTEST_ONLY)),$(filter $(FXTEST_ONLY),$(FXTEST_NAMES)),$(filter-out test_parity,$(FXTEST_NAMES)))

fxtest: fxtest-headless

fxtest-headless:
	@if [ -z "$(ARDENS)" ] || [ ! -x "$(ARDENS)" ]; then \
		echo "fxtest-headless: SKIPPED (Ardens unavailable at $(ARDENS); set ARDENS=/path/to/Ardens to run serial device tests)"; \
	else \
		$(MAKE) --no-print-directory fxtest-headless-preflight fxtest-build-parallel && \
		$(MAKE) --no-print-directory fxtest-run; \
	fi

fxtest-headless-preflight:
	@test -x "$(ARDENS)" || { echo "fxtest-headless: Ardens executable not found at $(ARDENS); set ARDENS=/path/to/Ardens" >&2; exit 1; }
	@test -f "$(FXDATA_BIN)" || { echo "fxtest-headless: FX data image missing at $(FXDATA_BIN); run make gen or set FXDATA_BIN=/path/to/fxdata.bin" >&2; exit 1; }
	@strings "$(ARDENS)" | grep -Fxq captureserial || { echo "fxtest-headless: BLOCKED (Ardens at $(ARDENS) lacks captureserial)" >&2; exit 2; }

# Device test images compile with the shipping size flags (see SIZE_FLAGS) so
# their flash budget matches the real build. MH_NO_USB is deliberately NOT set:
# the Ardens harness captures USB serial output.
FXTEST_SIZE_FLAGS = --build-property compiler.cpp.extra_flags="-mcall-prologues -mrelax" \
    --build-property compiler.c.extra_flags="-mrelax" \
    --build-property compiler.c.elf.extra_flags="-mrelax"

# Stage + compile the selected suites. Each suite is its own make target, so
# `fxtest-build-parallel` compiles them with `make -j$(FXTEST_JOBS)` (private
# stage dirs, no shared state); the Ardens run phase stays serial (one GUI/
# serial instance). A failed compile fails its target and the sub-make.
FXTEST_JOBS ?= 4

fxtest-build: $(addprefix fxtest-build-,$(FXTEST_RUN))

fxtest-build-parallel:
	@$(MAKE) --no-print-directory -j$(FXTEST_JOBS) fxtest-build

fxtest-build-%:
	@stage="$(FXTEST_BUILD_DIR)/$*"; \
	rm -rf "$$stage"; \
	mkdir -p "$$stage"; \
	cp -R src "$$stage/src"; \
	cp "tst/fxdatatest/$*.ino" "$$stage/"; \
	cp tst/fxdatatest/*.hpp "$$stage/"; \
	cp -R tst/fxdatatest/harness "$$stage/harness"; \
	echo "build: $*"; \
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" \
	    --optimize-for-debug --output-dir "$$stage/output" \
	    $(FXTEST_SIZE_FLAGS) \
	    "$$stage/$*.ino"

fxtest-run:
	@failed=0; \
	for name in $(FXTEST_RUN); do \
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
