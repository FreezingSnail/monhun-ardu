# monhun-ardu-42n.8 — shipping: drop the USB stack via a custom main

Status: DONE.

## What changed

- `monhun-ardu.ino`: appended a guarded USB-free entry point behind
  `#if defined(MH_NO_USB)`: weak `initVariant()` + `int __attribute__((OS_main))
  main(void)` that calls `init(); initVariant(); setup(); for (;;) loop();` with
  no `serialEventRun()`. Placed after `setup()`/`loop()` so Arduino/Arduboy
  prototype generation does not clash (compiles clean; no prototype errors).
- `Makefile`: `-DMH_NO_USB` added to `compiler.cpp.extra_flags` in
  `SIZE_FLAGS` (shipping only; `build`/`mini`/`size`/`debug` inherit it).
  Comment updated to document the split: shipping USB-free, `fxtest-build`
  (separate arduino-cli invocation, stock flags) keeps the core main so
  `captureserial` still works.
- `README.md`: device-layer note (USB-free shipping main, consequence: no USB
  serial device while the game runs; upload via Cathy3K bootloader; fxtest keeps
  USB), status-snapshot + flash/RAM-history numbers refreshed.

## Size before/after

| | flash | RAM (.data+.bss) | free flash |
|---|---|---|---|
| before (HEAD 36e9242) | **27900 / 29696** | 1882 | 1796 |
| after (MH_NO_USB) | **25234 / 29696** | 1742 | **4462** |

Reclaimed: **-2666 B flash**, **-140 B RAM** (.text 27822->25194, .data 78->40,
.bss 1804->1702). Well above the ~1-2 KB target.

## Symbol evidence (`avr-nm -C dist/monhun-ardu.ino.elf`)

Shipping ELF, USB/CDC/Serial/PluggableUSB pattern count = **0 matches**.

```
== dist (shipping, MH_NO_USB) USB/CDC/Serial matches ==
(none)
== main symbol ==
000034da T main
```

Baseline (pre-change, same HEAD) had 17 matches, including `_cdcInterface`,
`PluggableUSB()`, `serialEventRun`, `Serial_::read/write/...`,
`vtable for Serial_`, `Serial`, `USB_SendControl(unsigned char, void const*, int)`,
`SendInterfaces()`, `SendControl()`, `Recv()`. All gone after the change.

`make mini` also compiles with `-DMH_NO_USB` (same 25234/1742; mini ELF
overwritten in `dist` only transiently, final `dist` rebuilt as the fx shipping
build).

## fxtest unaffected

`fxtest-build` compiles `tst/fxdatatest/test_*.ino` with its own arduino-cli
invocation and stock flags (no `SIZE_FLAGS`), so it keeps the core main and USB
CDC. Full `make fxtest-headless` green with serial capture intact (all suites
end with a bare `P` marker, which requires working serial):

```
test_audio PASS, test_boot PASSED=4, test_combat PASSED=233,
test_data PASSED=221, test_hub PASSED=57, test_hud PASSED=17,
test_menu_art PASSED=81, test_menu PASSED=74, test_monster_art PASSED=31,
test_parity PASSED=660, test_perf PASSED=5 (pUs=6373 pHz=156 lHz=52),
test_player_art PASSED=111, test_quests PASSED=50, test_screens PASSED=78,
test_smith PASSED=66  — all PASS
```

## Boot smoke (Ardens, profiledump path supported)

`build/profiler_after.txt` format matched; Ardens binary advertises
`profiledump=`. Ran:

```sh
"$ARDENS" headless=4000 display=ssd1306 fxport=d1 \
    profiledump=build/profiler-42n8_nousb.txt \
    file=dist/monhun-ardu.ino.elf file=fxdata/fxdata.bin
```

Result: **exit 0**, dump written with `cycles=35396006`,
`cycles_with_sleep=64000005`, `cpu_active_pct=55.3`, 38 hotspot rows including
`main` (10.47%, 0x34da-0x6076), `ArduboyG ...::paint` (15.70%),
`SpritesU::drawPlusMaskFX` (3.78%), `mh::blkClamp`, `mh::hudBar` — game executes
normally. **0** USB/CDC/Serial entries in the dump. Empty serial output is
expected for this shipping ELF (no USB); the profiledump confirms execution, so
the `captureserial` fallback was not needed.

## Gates

- `make build` (fx) clean: 25234 B.
- `make mini` clean: 25234 B.
- `make size`: flash=25234/29696 (4462 free) ram=1742/2560.
- `make gen-check`: `fxdata_manifest: PASS (67 generated artifacts unchanged)`.
- `make test`: Total Passed 4579 / Failed 0.
- `make test-tools`: 141 tests OK.
- `node --test mock/game.test.js`: 34/34 pass.
- parity regen `node tools/gen-parity-fixtures.js`: `tst/fxdatatest/parity_fixtures.hpp`
  empty diff.
- `make fxtest-headless`: all 15 suites PASS (serial capture intact).

## Risk callout

With no CDC the OS serial port disappears while the game runs. Uploading still
works via the Cathy3K bootloader window (`arduino-cli upload` / reset as usual);
noted in the README device-layer bullet.

No git commit/push. Files touched: `monhun-ardu.ino`, `Makefile`, `README.md`;
`output.md` this report.
