# monhun-ardu-imx — MAP screen (room graph + quest marker)

Status: DONE. Worker implementation + orchestrator trim pass; no worker commit.

## What landed

- `data/screens/map.json` + `images/screens/mh_screen_map_{0..4}_128x64.png`:
  SCREEN_MAP is a **panel** screen. Page 0 is the room graph (camp west, area
  centre, cavern north, ridge east) with the v1 cursor (camp) baked in; pages
  1..4 stamp a marker box on each room's panel corner. All five are committed
  art compiled by gen-screens (multi-page panel support + a page-count
  validation against `PAGE_MAX`).
- `src/screens.hpp`: `screenMapQuestRoom(save)` (room hint or `NONE`) and
  `screenMapPage(save)` (page = hint + 1; `NONE` wraps to page 0) pick the page;
  every other screen still pages by scroll. The MAP screen skips the list
  chrome. No live drawing: the marker/cursor are pixels in the cart.
- `tools/gen-quests.py` + `data/quests/*.json`: new optional `roomHint`
  validated against `data/map.json` room ids, emitted as `QUEST_ROOM_HINT`
  (PROGMEM, u8 room index, 0xFF = none) + `QUEST_ROOM_HINT_NONE`. Set:
  gather_ore -> cavern, slay_lunge/sweep -> area, crush_heavy -> ridge,
  train_pole -> area.
- `src/app_state.hpp`: `APP_NAV_MAP` + the hub MAP row route (pre-switch
  checks in `appScreenAccept`/`appNavApply` so the jump tables keep their size).
- `data/screens/hub.json`: MAP row after HUNT; `ACTION_OPEN_MAP`.
- Docs: `docs/ui-design.md` MAP section + page-table stride.

## Budget

The worker's first cut drew a live cursor+marker overlay: +106 B, 72 free
(under the ~150 reserve), and it also ran `test_screens` at the stack ceiling.
The baked-pages rework costs ~30 B net: **29572/29696 (124 free)**, ram 1920;
`test_hub` 1307 B globals, `test_screens` 1815 B (745 B stack margin).
The page-table slot widened from 13 B (4 addresses) to 16 B (5 addresses) with
a generator-side page-count error so a screen can never overflow its slot
again.

## Gotcha log (worth remembering)

- `test_screens` runs within ~780 B of frame at the stack ceiling: a rebuilt
  `app_state` route (switch-case instead of the pre-switch check) hung the
  suite with *no serial output*; restoring the checked form fixed it. Any
  imx-adjacent edit to that path needs a `test_screens` run.
- A single `make gen` pass after moving panel PNGs leaves a stale cart
  (gen-check catches it); always two passes when new FX symbols appear.

## Verification (orchestrator full gate)

- `make gen-check` OK; host 6960/0; tools 411 OK (6 pins updated for the new
  stride/page cap); 19/19 device suites PASS; `make size` 29572/29696
  (124 free); `ARDENS=/usr/bin/true make dev-hitboxes` builds.
