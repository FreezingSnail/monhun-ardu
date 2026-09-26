# monhun-ardu-5gp — GEAR slot label overwrite

Status: DONE (orchestrator inline; full gate green).

## Bug (owner report)

On GEAR, the live candidate name was drawn at the same lane as the baked slot
label. The font glyphs are plus-mask (transparent gaps), so the baked ink showed
through and a shorter name ("GN T1" over "WEAPON") read as the label written
over itself.

## Fix

A slot row erases the baked slot label over its full baked width
(`hudBlk(SCREEN_LABEL_X, y, labelLen * 4, 8, 0)`) before drawing the candidate
name; a slot with nothing owned keeps the baked label (no erase). Pinned by
`test_screens`: with the gun root equipped ("GN T1") the name ink sits in the
label lane and the baked tail cell (x 30..33) is clear; the head row keeps its
baked label when nothing is crafted.

## Trims (the erase costs 36 B; the reserve was 36 free)

- `screenMapPage` spells out the quest-hint read instead of calling
  `screenMapQuestRoom` (the helper stays for the device suites; LTO drops the
  unused copy).
- `stepWorldBody` drops the pre-`syncMonsterTarget` call: the target was already
  synced at the end of the previous tick and by `updateActiveTarget` on arrival,
  and the monster only moves inside `updateMonster` (which syncs itself), so
  `updatePlayer` still reads a current rect.

Net: shipping **29672/29696 (24 free)**, ram 1920 — the fix lands at +12 B over
the pre-fix baseline; the `dap` trim wave (measured leads) restores the reserve.

## Test-side hardening (third stack-ceiling hang this wave)

`test_screens` runs at the stack ceiling: the added draw-path code hung its
pixel section (no serial). Its 44 B SaveBlocks + text buffer moved to file
scope (`static SaveBlock save, act, eep, out, ps, qs2, gear, skillSave,
moveSave, fsave; static char ctext[...]`), giving the suite ~250 B of stack
back (globals 1809 -> 2059, margin 501 B). This is the durable fix for the
class of hang, not a one-off.

## Verification

- `make gen-check` OK; host 6973/0; tools 411 OK; 19/19 device suites PASS;
  `make size` 29672/29696 (24 free); `make dev-hitboxes` and `make dev` build.
