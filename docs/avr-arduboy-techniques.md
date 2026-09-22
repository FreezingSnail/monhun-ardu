# Arduboy techniques (forum + community digest)

Reference notes gathered from community.arduboy.com and linked sources, for
monhun-ardu (ATmega32u4, ArduboyG `L4_Triplane`, FX cart). Numbers are the
values reported by the authors, not re-measured here. Forum post links use
`/t/<topic>/<post>`.

## 1. Grayscale / ArduboyG

Sources: [ArduboyG thread 10617](https://community.arduboy.com/t/10617),
[gray sprites from FX 12317](https://community.arduboy.com/t/12317),
[undocumented SSD1306 gray 11078](https://community.arduboy.com/t/11078).

- L4_Triplane renders 3 bit-planes per gray frame, order 0 -> 1 -> 2. Plane bit
  membership is the shade: `. . X` dark gray, `. X X` light gray, `X X X`
  white, `...` black.
- Refresh default 156 Hz (SH1106 120 Hz); gray frame rate = refresh / planes
  -> 52 Hz at 3 planes. Visible glitching reported below ~130-135 FPS.
- Per-plane budget ~7.4 ms; the buffer -> OLED transfer costs ~1.2-1.4 ms, so
  game logic + render must fit in ~5-6 ms. Exceeding the budget once shows a
  glitch.
- One 1024-byte `sBuffer` only; a double-buffered 2 bpp image would need 4 KB
  RAM and does not fit.
- Canonical loop keeps FX reads out of the OLED paint window:

  ```cpp
  FX::enableOLED();
  a.waitForNextPlane();
  FX::disableOLED();
  if (a.needsUpdate()) update();
  render();
  ```

- Banned/unsupported: `display()`, `nextFrame()`, `setFrameRate()`,
  `paintScreen()`, `INVERT`. Game state must only change when `needsUpdate()`
  is true (never mid-gray-frame).
- Update pacing: `setUpdateHz()`, `setUpdateEveryN(num, denom)` (fractional,
  e.g. 5/2 = every 2.5 planes).
- Slow calls: `fillRect`, `drawRect`, FX text (~60% CPU in one report).
- Gray sprites: converter splits a PNG into `shades-N` 1-bit planes; frame
  index is `frame*3 + currentPlane()`; `SPRITESU_PLUSMASK` for transparent,
  overwrite otherwise; sprite heights must be multiples of 8. Cost: about +100%
  unmasked / +50% masked per extra plane (FX transfer is the bottleneck).
- FX-C panel: `ABG_REFRESH_HZ 152`, `PRECHARGE 1`, `DISCHARGE 8`.
- Undocumented SSD1306 `0x9A,2` "gray mode": halves height to 128x32 and sums
  adjacent rows in the panel (3 levels, no CPU cost). Works on I2C modules,
  ignored on every tested Arduboy SPI unit -- not a product path.
- Grayscale bootloader was discussed and shelved: gray art is 4x larger,
  bootloader grows, smooth scrolling may break.

## 2. FX cart I/O and sprite performance

Sources: [FX library 7673](https://community.arduboy.com/t/7673),
[SpritesABC 12518](https://community.arduboy.com/t/12518),
[large maps from FX 12813](https://community.arduboy.com/t/12813),
[WorldMap 10316](https://community.arduboy.com/t/10316),
[flash access speed 6882](https://community.arduboy.com/t/6882).

- Addressing is 24-bit: `page << 8 | offset`. `seekData()` adds
  `programDataPage` internally, so compiled addresses stay chip-relative.
- `seekData()` starts a read; `readPendingUInt8/16/24/32` stream it;
  `readEnd()` deselects and returns the last byte. `readDataBytes(addr, buf,
  len)` is the seek + bulk-read + end form (memcpy-like).
- Endianness: cart data is big-endian, AVR is little-endian.
  `readPendingUInt16` swaps; raw `readDataBytes` does not.
- `readIndexedUInt*` reseeks per element (~100 cycles each) and takes a uint8
  index. For more than 256 elements use `seekDataArray(addr, index, offset,
  elementSize)` or `readDataArray`.
- SPI is shared with the display: disable FX before the sBuffer -> OLED write.
  `displayPrefetch` runs FX and OLED together, full-duplexing the next FX read
  during the display write, saving ~1.2 ms per frame.
- Chip speed: address setup ~4x18 cycles, then ~18 cycles/byte sequential
  (17 cycles/byte in the asm reader). A seek costs ~100 cycles. A grayscale
  frame budget is ~100k cycles.
- 16x16 unmasked sprite cycles: `SpritesU::drawOverwriteFX` 1206,
  `SpritesABC::drawFX` 993, `SpritesABC::drawSizedFX` 878,
  `SpritesABC::drawBasicFX` 826, `FX::drawBitmap` 2021-2244.
  Progmem cost: `drawBasicFX` 0x1004 B, `drawFX` 0x104 B, `drawSizedFX`
  0x58 B, `FX::drawBitmap` 0x508 B.
- SpritesABC modes: `MODE_OVERWRITE=0`, `MODE_PLUSMASK=1`, `MODE_SELFMASK=4`,
  `MODE_SELFMASK_ERASE=6`. Heights must be multiples of 8. It is worth its
  ~2x progmem only when CPU-bound (grayscale); `drawSizedFX` when dimensions
  are compile-time.
- Map streaming that measured well: `readDataBytes` straight into `sBuffer`
  (WorldMap reads one full row per scroll); a small per-row `uint16_t` buffer
  to avoid repeated seeks; chunk-into-RAM performed identically to per-tile
  indexed reads; consecutive chunk layout gained ~6%; one extra row for smooth
  vertical scroll cost 4.5% CPU. A RAM tile cache took one walk from
  <20 FPS back to 23-35 FPS.
- No ISR read-ahead: ISR entry/exit costs >= 20 cycles and the read dies at
  OLED deselect. `FX::drawBitmap` relies on cycle timing and polls SPSR once
  per row.
- Writes: 4 KiB sector erase, 256 B page program, bits only 1 -> 0.

## 3. AVR codegen and performance

Sources: [AVR optimization 11339](https://community.arduboy.com/t/11339),
[linker TCO 11956](https://community.arduboy.com/t/11956),
[profiling 9325](https://community.arduboy.com/t/9325),
[sprites masked vs unmasked 9813](https://community.arduboy.com/t/9813),
[byte reductions 3159](https://community.arduboy.com/t/3159),
[assembly 6139](https://community.arduboy.com/t/6139).

- No barrel shifter. `1 << a` compiles to a shift-by-one loop, roughly 10x
  slower than a LUT; GCC will not memoize it. Use `1 << (n & 7)` LUTs or
  multiply when the same shift repeats.
- No hardware divide. An 8-bit divide routine is ~56 cycles average; 16-bit is
  far worse. Three divisions per loop iteration consumed ~50% of all cycles in
  one raycaster. Precompute, invert to multiply, or use approximate methods.
- `do-while` is the cheapest loop shape; `for`/`while` pay an entry branch and
  the AVR's not-taken penalty per iteration (author hypothesis).
- Linker relaxation (`ld`) also performs tail-call optimization: an
  `rcall` -> `rjmp` rewrite happens even inside `asm volatile`, which broke a
  hand-timed 7-cycle delay (became 2 cycles). Any hand-timed asm under
  relaxation is suspect; verify with objdump.
- Benchmarks: wrap with `micros()`, and sink results through `volatile` or the
  compiler deletes the whole loop. Disassemble with `objdump -d -s` from the
  Arduino toolchain (`avr/bin/objdump`) or use godbolt with AVR GCC.
- Sprite routines: `drawPlusMask` measured ~8% faster than `drawExternalMask`
  (1000 draws). External mask is the worst case -- it needs four 16-bit
  pointers and the CPU only has X/Y/Z. `drawCompressed` is "really slow"
  because of variable shifts (`1<<i`, `<<yOffset`, `>>(8-yOffset)`); a
  `pgm_read_byte(&shiftOneLookup[yOffset & 7])` fixes the third.
- Inline asm: X = r27:r26, Y = r29:r28, Z = r31:r30. In `drawPlusMask`, X and
  Z are output operands and Y is push/pop preserved because all three pointer
  registers are in use. See the avr-gcc wiki "Register Layout" and avr-libc
  inline asm notes.
- LTO size surprises are real and reproducible (duplicating a
  `display(CLEAR_BUFFER)` call made the image 10 bytes smaller); globals cost
  more than locals (104 vs 84 B in the AVR4027 example). Measure whole images.

## 4. Flash size (ranked, measured by authors)

Sources: [saving flash 9504](https://community.arduboy.com/t/9504),
[cutting code size 12249](https://community.arduboy.com/t/12249),
[optimize help 12561](https://community.arduboy.com/t/12561),
[Moblynx compressor 2543](https://community.arduboy.com/t/2543),
[3-colour RLE 6435](https://community.arduboy.com/t/6435),
[Arduboy2 README](https://raw.githubusercontent.com/MLXXXp/Arduboy2/master/README.md),
[Squeezing the Arduboy (archive)](https://web.archive.org/web/20190920145355/http://www.mattgreer.org/articles/squeezing-the-arduboy-for-every-byte/).

1. `-DARDUBOY_NO_USB` drops the USB stack: ~3 KB flash. Cost: uploads need a
   manual reset (bootloader window ~8 s); provide `exitToBootloader()`.
2. Bootloader replacement: Cathy 3K +1 KB, Cathy 2K +2 KB, no bootloader
   (ICSP) +4 KB. Shrinks the shipping audience.
3. `boot()` instead of `begin()` and only the needed boot features; pick the
   `bootLogo*` variant matching the draw function the sketch already uses;
   `Arduboy2Base` instead of `Arduboy2` to drop text/Print; test both
   `Sprites` and `SpritesB` (either can win).
4. Custom font reusing the sketch's own `drawBitmap`: ~900 B vs `print()` plus
   a font table. A 40-char font is ~1 KB. 5 bits/char also allows three chars
   per 16-bit word.
5. Moblynx 1-bit image compressor: `drawCompressed` decompressor 500-1100 B,
   10-20% smaller than Cabi, ~50% faster, 128x64 in 20-30 ms; splash
   815 -> 663 B, masked sprite 234 -> 194 B.
6. Data layout: nibble-packed tiles halve map data; token RLE was 448 B gross,
   ~200 B net after the decompressor; entity x/y packed into one byte
   (`x/8 << 4 | y/4`) saved 100-200 B; unused top bits of an id byte reused
   for door index/chest contents; mirroring saved 1036 B; inversion cost 10 B
   of code and saved 352 B; smaller sprites.
7. `memcpy_P` with a struct and one loader per type, instead of one function
   per record. `pgm_read_byte_inc` (post-increment `lpm`) reads consecutive
   bytes in one instruction; `pgm_read_byte` cannot emit it.
8. Hoist repeated calls out of switch branches (~138 B measured; another
   contributor found ~500 B). Locals live in registers and beat globals.
9. Dither-aware 3-colour RLE lost to 1-bit Cabi on typical art (283 vs
   178/192 B); only worth it with a stateful 1-bit colour-change encoding.
- Bytecode VM / self-decompressing flash: not viable (AVR cannot execute from
  RAM; self-programming needs bootloader cooperation).

## 5. Fixed point, RNG, physics, architecture

Sources: [fixed point library 4129](https://community.arduboy.com/t/4129),
[float -> fixed 8633](https://community.arduboy.com/t/8633),
[fast fixed-point math 12569](https://community.arduboy.com/t/12569),
[Box2D-lite port 10790](https://community.arduboy.com/t/10790),
[better random 12753](https://community.arduboy.com/t/12753),
[on randomness 4919](https://community.arduboy.com/t/4919),
[optimizing/organization 7641](https://community.arduboy.com/t/7641),
[ABC 11673](https://community.arduboy.com/t/11673),
[FateHack 10682](https://community.arduboy.com/t/10682),
[unit testing 12615](https://community.arduboy.com/t/12615).

- Fixed point: prefer power-of-two fractions so scaling is a shift or a byte
  drop (`SQ7x8`, `SQ15.16`, `SQ23.8`). Libraries: Pharap
  `FixedPointsArduino` (templated `SFixed`/`UFixed`, float interop) and zed
  `AVR-fixpt-math` (fixed Q16, asm, validated trig/exp/recip).
- Multiply: multiply into 64-bit and drop the low 16 bits by byte pointer or
  union instead of an explicit `>> 16`. Guard `#ifdef __AVR__`; the union form
  compiled slightly larger. Interop libraries through
  `getInternal()`/`fromInternal()`.
- Pitfalls: Arduino's `round()` macro is wrong for negative fixed-point
  (floors instead of truncating) -- use `roundFixed`; multiply-then-divide
  loses precision. Brads (256 per turn) give implicit mod, bit-op quadrants
  and a 64-entry sin/cos table.
- RNG: `random()` is a ~300 B LCG. jsf8 is tiny/fast for general use;
  xorshift32 (or ABC's inline-asm xorshift 8,9,23) is best when a seeded
  sequence must reproduce. An 8-bit xorshift tops out at 2^16 period and is
  seed-sensitive; `%` introduces modulo bias. Seed from an unconnected ADC pin
  plus `micros()` or `TCNT0`.
- Physics: the Box2D-lite port ran with `SQ7x8` (hex 64.8 KB, RAM-bound, no
  overflow checks), and the follow-up was to replace libm with own trig/sqrt
  and switch to brads.
- Architecture (Pharap): use `constexpr` over macros, locals for loop
  counters, structs (`Point`, `Player`) over parallel arrays, per-type headers
  and files, warnings on. Avoid `x++` inside expressions.
- Testing: host-test pure logic by abstracting Arduboy calls, `constexpr` +
  `static_assert` for pure functions, on-device assert/Serial otherwise; a
  desktop mock for Arduboy/FX remains an open gap.
