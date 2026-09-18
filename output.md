# monhun-ardu-603 — pack raw_t tables before sprite sections: DONE

Worker report. **No commit/push/`git add` performed.**

## Change

`fxdata/fxdata.txt` section order is the packer's only ordering mechanism:
`Arduboy-Python-Utilities/fxdata-build.py` (v1.15) has **no ordering directive**
— `include "..."` expands in place and `raw_t`/`image_t` blobs append as parsed
(see its read loop, lines 216-343). No vendored-tool patch needed.

Moved every `raw_t` runtime table (`mhWeaponDefs`, `mhMonsterAttacks`,
`mhMonsterDefs`, `mhSin65`, `mhCombat`, `mhEquip`) ahead of
`include "blocks|fonts|menu|equip/Sprites.txt"`, and documented the load-bearing
order in the file header (fxmem.hpp casts these uint24_t offsets to 16-bit fake
cart pointers → must stay < 64 KiB; SpritesU sheets use 24-bit seeks → free).
Updated the now-stale ordering comment in `tools/gen-art.py` (WHIRL_RING_FRAMES
stays 24; constraint removed, not rebaked).

Files: `fxdata/fxdata.txt`, `tools/gen-art.py`, regenerated set
(`fxdata/fxdata.{h,bin,data-data.bin,manifest.json}`, `fxdata/tables/equip.bin`,
`src/fxdata.h`, `src/generated/equip_meta.hpp`).

## Addresses (new; FX_DATA_BYTES unchanged 123917)

| symbol | new | old |
|---|---|---|
| mhWeaponDefs | 0x000000 | 0x00DEAA |
| mhMonsterAttacks | 0x00021C | 0x00E0C6 |
| mhMonsterDefs | 0x00023E | 0x00E0E8 |
| mhSin65 | 0x00026A | 0x00E114 |
| mhCombat | 0x0002AB | 0x00E155 |
| mhEquip | 0x000525 | 0x00E3CF |
| first sprite (`fxdeflect`) | 0x000891 | 0x000000 |
| last sprite (`mh_weapon_flail`) | 0x019C0B | 0x019C0B |

Table block = 0x000000-0x000891 (2193 B). **Headroom to the 64 KiB window =
0x10000 - 0x891 = 0xF76F = 63,343 B**, and sprite growth no longer moves the
tables at all (tables are pinned at the image base), so the ceiling is
structurally gone. `mhEquip` (the render-pass raw_t read) is at 0x000525.

## Verification

1. `make gen` x2 -> `make gen-check` **PASS**:
   `fxdata_manifest: PASS (51 generated artifacts unchanged)`;
   manifest `31 images, 40 inputs, 11 outputs`. `fxdata/fxdata.h == src/fxdata.h`.
   (First gen re-baked equip part-view offsets from the pre-reorder header; second
   pass converged; gen-check's own gen confirmed determinism.)
2. `make test`: **Total Passed: 3119 / Total Failed: 0**
   `make test-tools`: **Ran 81 tests ... OK**
3. `make fxtest-headless` full — all 10 suites PASS:
   test_assets 254/0, test_audio 14/0, test_boot 4/0, test_combat 195/0,
   test_data 221/0, test_hud 17/0, test_menu 59/0, test_parity 660/0,
   test_perf 5/0, test_player_art 111/0.
   Perf line: `B pUs=6614 pHz=151 lHz=50 lTk=984 rMx=5496 rAv=5136 ram=420`
   (pUs/rMx/rAv identical to the documented baseline `pUs=6614 rMx=5496 rAv=5136`;
   `ram` was 424 there, unrelated to this change).
   AVR `equip_meta.hpp` static_assert stale-blob guard passes (all SHEET_OFF_*
   re-baked; e.g. FXCHIP 7383, FXDEFLECT 2193, MH_BODY_BASE 82243).
4. `make build` + `make size`: **flash=27000/29696 (2696 free)**, RAM 2018/2560.
   Delta vs 27020 baseline = **-20 B** (no regression). `.text=26942 .data=58 .bss=1960`.
   data facts unchanged (`HAS_SIMPLE_GUARDS:true`, rest false).

## Deviations

- `tools/gen-art.py`: comment-only edit explaining the order change; no generated
  art changed (WHIRL_RING_FRAMES stays 24).
- Host build `output.md` itself remains the only tracked report artifact; not staged.
