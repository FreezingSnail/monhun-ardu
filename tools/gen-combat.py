#!/usr/bin/env python3
"""Compile creature combat JSON into the packed FX blob + generated headers.

    data/skeletons.json + data/creatures/*.json
        -> fxdata/tables/combat.bin          (packed blob; build intermediate)
        -> src/generated/combat_data.hpp     (host plain structs + arrays)
        -> src/generated/combat_meta.hpp     (VERSION, SIZE, per-record offsets)
        -> src/generated/combat_expect.hpp   (record sizes, spot values, sha256)

3-hitzone model (build/zones-design.md): the body is implicit (creature HP and
creature w/h, wins ties); a creature may declare a `head` and/or an `appendage`
zone. Zones carry a u8 pool, dmgMul, bodyShare, breakTypes, staggerOnHit and a
single broken record (brokenDmgMul / brokenFlags / unlockMask); there are no
stage tables, element tables or predicate tables. Validation stays strict:
unknown/missing keys, integer-only quantized fields (floats and bools rejected),
ranges, phys enum membership, windows inside [0, active], guard ordering,
resolvable local refs, unique local ids and u8/u16 section limits.

Blob layout (little-endian, explicit u8/u16, no padding, fixed section order):

    header     32 B  magic u16, version u8, flags u8, 10x u16 counts + 4x u16 reserved
    creature   29 B  skeletonIdx, profileIdx, headZone, appendZone,
                     firstAttack, attackCount, firstPattern, patternCount,
                     w, h, spd, collide(ox i8, oy i8, w, h),
                     hp u16, spawnX u16, spawnY u16, flags, sheet,
                     brokenW, brokenH, enrage(hpPct, spdMul, faceHold, cue)
    profile    24 B  engageDist, keepDist, attackDist, circleNum, circleDen,
                     retreatNum, retreatDen, staggerMax, staggerDecay,
                     zoneFlags (bit0 head, bit1 appendage), faceHold u8 tick
                     countdown, turnRate u8 DIR8 steps per facing refresh,
                     cdBase u16, cdJitter u16, spawnT u16,
                     spawnCd u16, stunRecoverT u16, staggerRecoverT u16
    skeleton    2 B  firstAnchor, anchorCount
    zone       12 B  box(ox i8, oy i8, w, h), hp, dmgMul, bodyShare, breakTypes,
                     staggerOnHit, brokenDmgMul, brokenFlags (bit0 hurtOff,
                     bit1 cue), unlockMask (bit per global attack idx)
    anchor      2 B  ox i8, oy i8
    attack     27 B  moveType, moveSpeedF, moveDx i8, moveDy i8, facing, phys,
                     elem, onHitEffect, onHitPush i8, onHitStun, stagger, cue,
                     wallStun, firstWindow, windowCount, windup u16, active u16,
                     recover u16, dmg u16, tell (0 dot / 1 line / 2 arc /
                     3 ring / 4 zone, feel.5), artSheet (1-based
                     data/art_sheets.json index, 0 = no attack overlay),
                     artFrame (2-facing pose frame base), artMode (0 normal /
                     1 locked-spin whole-body sheet, bih.4)
    window     10 B  t0 u16, t1 u16, box(ox i8, oy i8, w, h), dmgMul, flags
    pattern     3 B  firstStep, stepCount, guardIdx
    guard       9 B  minDist, maxDist, hpLo, hpHi, playerFlags, cooldown,
                     chance, zonesBroken (bitmask), facing (0 any / 1 behind /
                     2 front)
    step        4 B  kind (0 ATK / 1 WAIT), ref (attackIdx or ticks), after,
                     chance
    art        10 B  sheetIdx (1-based into data/art_sheets.json, 0 = none ->
                     legacy per-kind draw), anchorY i8, stride, idle0,
                     idleCount, windup, attack, recover, flash, dead

Usage:
    python3 tools/gen-combat.py [--root DIR] [--dump]

    --root DIR  pipeline root holding data/ and src/generated (default: repo)
    --dump      validate + list the compiled model on stdout; writes nothing
"""

import argparse
import hashlib
import json
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKELETONS_REL = "data/skeletons.json"
CREATURES_REL = "data/creatures"
ITEMS_REL = "data/items.json"
ART_SHEETS_REL = "data/art_sheets.json"
BLOB_REL = "fxdata/tables/combat.bin"
DATA_HPP_REL = "src/generated/combat_data.hpp"
META_HPP_REL = "src/generated/combat_meta.hpp"
EXPECT_HPP_REL = "src/generated/combat_expect.hpp"

MAGIC = 0x4D43
VERSION = 1
FLAGS = 0
HEADER_SIZE = 32
MAX_ID = 31
ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")

PHYS = {"SLASH": 0x01, "BLUNT": 0x02, "SHOT": 0x04}
ELEMS = {"NONE": 0, "FIRE": 1, "WATER": 2, "ICE": 3, "THUNDER": 4}
MOVE_TYPES = {"none": 0, "lunge": 1, "charge": 2, "hop": 3}
FACINGS = {"track": 0, "lock-at-windup": 1, "lock-away": 2}
# Per-attack telegraph shape (feel.5): the render draws the windup tell from the
# cached window. 0 (dot) is the shipped 2x2 default; the others outline/point at
# the covered area. Values mirror src/core/combat.hpp enum Tell.
TELLS = {"dot": 0, "line": 1, "arc": 2, "ring": 3, "zone": 4}
# Per-attack whole-body art mode (bih.4): mode 0 draws the attack record's
# 2-facing art sheet at artFrame (or the authored tell slot during windup); mode
# 1 draws the locked-spin whole-body sheet (fxtailspin) centred on the body.
ATTACK_MODES = {"normal": 0, "spin": 1}
# Guard facing clause: player position relative to the beast's facing vector.
GUARD_FACINGS = {"behind": 1, "front": 2}
ON_HIT_EFFECTS = {"none": 0, "trip": 1, "stun": 2}
CUES = {"none": 0, "windup": 1, "part_break": 2}
STEP_ATK = 0
STEP_WAIT = 1

# Carve table (bead monhun-ardu-prg.3): a per-creature drop list packed into the
# creature record as CARVE_SLOTS fixed 3-byte entries (item index u8, count
# 1..3, chance 0..100). count 0 marks an empty slot. Item ids resolve against
# data/items.json (tools/gen-items.py); the runtime roll is deterministic
# (combatChancePasses, no RNG state), so the blob carries no extra state.
CARVE_SLOTS = 4
CARVE_SIZE = 3
CARVE_ITEM_OFF = 0
CARVE_COUNT_OFF = 1
CARVE_CHANCE_OFF = 2
CARVE_COUNT_MIN = 1
CARVE_COUNT_MAX = 3

# Fixed 3-hitzone model (build/zones-design.md): body is implicit, these are
# the two optional per-creature records. Bit order is the zone flag / broken
# bit contract shared with src/core/combat.hpp.
ZONE_NAMES = ("head", "appendage")
ZONE_HEAD = 0x01
ZONE_APPENDAGE = 0x02
COMBAT_NO_ZONE = 0xFF

SIZES = {
    "CREATURE": 29 + CARVE_SIZE * CARVE_SLOTS,
    "PROFILE": 24,
    "SKELETON": 2,
    "ZONE": 13,
    "ANCHOR": 2,
    "ATTACK": 27,
    "WINDOW": 10,
    "PATTERN": 3,
    "GUARD": 9,
    "STEP": 4,
    "CARVE": CARVE_SIZE,
    # Epic monhun-ardu-bih: the per-creature art descriptor appended after every
    # other section (ART_COUNT == CREATURES_COUNT). The sheet field is a 1-based
    # index into data/art_sheets.json; render resolves it to a cart address via
    # the post-pack src/generated/art_sheets.hpp table.
    "ART": 10,
}
SECTION_RECORD = {
    "CREATURES": "CREATURE",
    "PROFILES": "PROFILE",
    "SKELETONS": "SKELETON",
    "ZONES": "ZONE",
    "ANCHORS": "ANCHOR",
    "ATTACKS": "ATTACK",
    "WINDOWS": "WINDOW",
    "PATTERNS": "PATTERN",
    "GUARDS": "GUARD",
    "STEPS": "STEP",
    "ART": "ART",
}
SECTION_ORDER = list(SECTION_RECORD)
# 11 section counts now (the ART count takes the first reserved u16), 3 reserved.
RESERVED_COUNTS = 3

_MISSING = object()


class Errors:
    """Collects user-facing validation errors; never raises on schema issues."""

    def __init__(self):
        self.items = []

    def add(self, ctx, message):
        self.items.append("%s: %s" % (ctx, message))


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def check_keys(errors, ctx, obj, required, optional=()):
    if not isinstance(obj, dict):
        errors.add(ctx, "expected an object")
        return False
    for key in sorted(obj):
        if key not in required and key not in optional:
            errors.add(ctx, "unknown key '%s'" % key)
    for key in required:
        if key not in obj:
            errors.add(ctx, "missing key '%s'" % key)
    return True


def read_int(errors, ctx, obj, key, lo, hi, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not is_int(value):
        errors.add(ctx, "%s: expected an integer, got %r" % (key, value))
        return None
    if not lo <= value <= hi:
        errors.add(ctx, "%s: out of range %d..%d: %d" % (key, lo, hi, value))
        return None
    return value


def read_bool(errors, ctx, obj, key, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not isinstance(value, bool):
        errors.add(ctx, "%s: expected true/false, got %r" % (key, value))
        return None
    return 1 if value else 0


def read_enum(errors, ctx, obj, key, table, default=_MISSING):
    if not isinstance(obj, dict) or key not in obj:
        if default is _MISSING:
            return None
        return default
    value = obj[key]
    if not isinstance(value, str) or value not in table:
        errors.add(ctx, "%s: unknown value %r (want one of %s)" % (key, value, ", ".join(sorted(table))))
        return None
    return table[value]


def read_id(errors, ctx, obj, key, seen=None):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, str):
        errors.add(ctx, "%s: expected a string id, got %r" % (key, value))
        return None
    if not ID_RE.match(value):
        errors.add(ctx, "%s: id '%s' must match [a-z][a-z0-9_]*" % (key, value))
        return None
    if len(value) > MAX_ID:
        errors.add(ctx, "%s: id '%s' is longer than %d characters" % (key, value, MAX_ID))
        return None
    if seen is not None:
        if value in seen:
            errors.add(ctx, "duplicate local id '%s'" % value)
        seen.add(value)
    return value


def read_attack_id_list(errors, ctx, obj, key, known):
    if not isinstance(obj, dict) or key not in obj:
        return []
    value = obj[key]
    if not isinstance(value, list):
        errors.add(ctx, "%s: expected an array of attack ids" % key)
        return []
    refs = []
    for i, item in enumerate(value):
        if not isinstance(item, str) or item not in known:
            errors.add(ctx, "%s[%d]: unknown attack id %r" % (key, i, item))
            continue
        if item in refs:
            errors.add(ctx, "%s[%d]: duplicate attack id '%s'" % (key, i, item))
            continue
        refs.append(item)
    return refs


def normalize_box(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"ox", "oy", "w", "h"})
    return {
        "ox": read_int(errors, ctx, obj, "ox", -128, 127),
        "oy": read_int(errors, ctx, obj, "oy", -128, 127),
        "w": read_int(errors, ctx, obj, "w", 1, 255),
        "h": read_int(errors, ctx, obj, "h", 1, 255),
    }


def normalize_break_types(errors, ctx, value):
    if not isinstance(value, list):
        errors.add(ctx, "breakTypes: expected an array of phys names")
        return 0
    mask = 0
    seen = set()
    for i, item in enumerate(value):
        if not isinstance(item, str) or item not in PHYS:
            errors.add(ctx, "breakTypes[%d]: unknown phys %r (want SLASH, BLUNT or SHOT)" % (i, item))
            continue
        if item in seen:
            errors.add(ctx, "breakTypes[%d]: duplicate phys '%s'" % (i, item))
            continue
        seen.add(item)
        mask |= PHYS[item]
    return mask


def normalize_collide(errors, ctx, obj):
    """Optional per-creature collide box (epic monhun-ardu-nch): the rect
    syncMonsterTarget/pushApart use for body collision, so a tall wader can
    expose only its legs while its body region passes over the player. Absent
    means the body box (stats w/h at the origin), the shipped default."""
    check_keys(errors, ctx, obj, {"ox", "oy", "w", "h"})
    return {
        "ox": read_int(errors, ctx, obj, "ox", -128, 127),
        "oy": read_int(errors, ctx, obj, "oy", -128, 127),
        "w": read_int(errors, ctx, obj, "w", 1, 255),
        "h": read_int(errors, ctx, obj, "h", 1, 255),
    }


def normalize_broken_body(errors, ctx, obj):
    """Optional stats.brokenBody box (static props): the target rect shrinks to
    this size when the prop's breakable zone breaks. Absent = rect unchanged."""
    check_keys(errors, ctx, obj, {"w", "h"})
    return {
        "w": read_int(errors, ctx, obj, "w", 1, 255),
        "h": read_int(errors, ctx, obj, "h", 1, 255),
    }


def normalize_enrage(errors, ctx, obj):
    """Optional stats.enrage phase record (feel.6): one-shot escalation when the
    creature's HP percent crosses hpPct. hpPct 0 disables it (the shipped
    default); spdMul is the speed percent applied once with a floor of 1,
    faceHold replaces profile.faceHold, cue names the existing audio cue stored
    for a future enrage cue path."""
    check_keys(errors, ctx, obj, {"hpPct", "spdMul", "faceHold"}, {"cue"})
    return {
        "hpPct": read_int(errors, ctx, obj, "hpPct", 0, 100),
        "spdMul": read_int(errors, ctx, obj, "spdMul", 0, 255),
        "faceHold": read_int(errors, ctx, obj, "faceHold", 0, 255),
        "cue": read_enum(errors, ctx, obj, "cue", CUES, default=0),
    }


def normalize_zone(errors, ctx, obj, attack_ids):
    # Static props omit the pool (hp 0 = never drains/breaks); bodyShare
    # defaults to 100 and hurtOn to true. The record itself has no intact
    # hurtOn field (only the broken override), so those keys are validation
    # only, kept for authoring clarity.
    check_keys(errors, ctx, obj, {"box", "dmgMul"}, {"hp", "bodyShare", "breakTypes", "hurtOn", "staggerOnHit", "broken"})
    dmg_mul = read_int(errors, ctx, obj, "dmgMul", 0, 255)
    broken = obj.get("broken")
    broken_dmg = dmg_mul if dmg_mul is not None else 100
    broken_hurt_off = 0
    broken_cue = 0
    broken_disable = []
    if broken is not None:
        check_keys(errors, ctx + ".broken", broken, set(), {"dmgMul", "hurtOn", "cue", "disableAttacks"})
        broken_dmg = read_int(errors, ctx + ".broken", broken, "dmgMul", 0, 255, default=broken_dmg if broken_dmg is not None else 100)
        hurt = read_bool(errors, ctx + ".broken", broken, "hurtOn", default=1)
        broken_hurt_off = 0 if hurt else 1
        broken_cue = read_enum(errors, ctx + ".broken", broken, "cue", CUES, default=0) or 0
        broken_disable = read_attack_id_list(errors, ctx + ".broken", broken, "disableAttacks", attack_ids)
    return {
        "box": normalize_box(errors, ctx + ".box", obj.get("box")),
        "dmgMul": dmg_mul,
        "hp": read_int(errors, ctx, obj, "hp", 0, 255, default=0),
        "bodyShare": read_int(errors, ctx, obj, "bodyShare", 0, 255, default=100),
        "breakTypes": normalize_break_types(errors, ctx, obj.get("breakTypes", [])),
        "hurtOn": read_bool(errors, ctx, obj, "hurtOn", default=1),
        "staggerOnHit": read_int(errors, ctx, obj, "staggerOnHit", 0, 255, default=0),
        "brokenDmgMul": broken_dmg if broken_dmg is not None else 100,
        "brokenHurtOff": broken_hurt_off,
        "brokenCue": broken_cue,
        "brokenDisable": broken_disable,
    }


def normalize_attack_art(errors, ctx, obj, sheet_names):
    """Optional per-attack whole-body art (bih.4).

    `sheet` names a data/art_sheets.json entry and packs as its 1-based index
    (0 = no attack overlay -> the generic base body draws); `frame` is the
    2-facing pose base (already doubled: 0/2/4); `mode` selects the draw (normal
    2-facing sheet or the locked-spin whole-body sheet). All optional: an attack
    without an `art` block packs an all-zero triple (legacy generic body)."""
    if obj is None:
        return dict(ZERO_ATTACK_ART)
    check_keys(errors, ctx, obj, set(), {"sheet", "frame", "mode"})
    name = obj.get("sheet")
    index = 0
    if name is None:
        pass
    elif not isinstance(name, str):
        errors.add(ctx, "sheet: expected a sheet name string")
    elif sheet_names is None:
        errors.add(ART_SHEETS_REL, "missing art sheet file (attack art.sheet resolves against it)")
    elif name not in sheet_names:
        errors.add(ctx, "sheet: unknown art sheet %r" % name)
    else:
        index = sheet_names.index(name) + 1
    frame = read_int(errors, ctx, obj, "frame", 0, 255, default=0)
    mode = read_enum(errors, ctx, obj, "mode", ATTACK_MODES, default=0)
    return {"sheet": index, "frame": frame if frame is not None else 0, "mode": mode if mode is not None else 0}


def normalize_attack(errors, ctx, obj, sheet_names=None):
    check_keys(errors, ctx, obj, {"id", "windup", "active", "recover", "dmg", "phys", "elem", "move", "facing", "windows"}, {"onHit", "stagger", "cue", "wallStun", "tell", "art"})
    active = read_int(errors, ctx, obj, "active", 0, 65535)
    raw_windows = obj.get("windows")
    if not isinstance(raw_windows, list):
        errors.add(ctx, "windows: expected an array")
        raw_windows = []
    windows = []
    for i, win in enumerate(raw_windows):
        c = "%s.windows[%d]" % (ctx, i)
        check_keys(errors, c, win, {"t0", "t1", "box", "dmgMul"})
        t0 = read_int(errors, c, win, "t0", 0, 65535)
        t1 = read_int(errors, c, win, "t1", 0, 65535)
        if t0 is not None and t1 is not None:
            if t0 > t1:
                errors.add(c, "window is inverted: t0 %d > t1 %d" % (t0, t1))
            if active is not None and t1 > active:
                errors.add(c, "window ends outside the active phase: t1 %d > active %d" % (t1, active))
        windows.append({
            "t0": t0 if t0 is not None else 0,
            "t1": t1 if t1 is not None else 0,
            "box": normalize_box(errors, c + ".box", win.get("box")),
            "dmgMul": read_int(errors, c, win, "dmgMul", 0, 255),
        })
    move = obj.get("move")
    check_keys(errors, ctx + ".move", move, {"type"}, {"speedF", "dx", "dy"})
    move_type = read_enum(errors, ctx + ".move", move, "type", MOVE_TYPES)
    move_speed = 0
    move_dx = 0
    move_dy = 0
    if move_type in (MOVE_TYPES["lunge"], MOVE_TYPES["charge"]):
        if "speedF" not in move:
            errors.add(ctx + ".move", "speedF: required for move type '%s'" % move.get("type"))
        else:
            move_speed = read_int(errors, ctx + ".move", move, "speedF", 0, 255)
        for key in ("dx", "dy"):
            if key in move:
                errors.add(ctx + ".move", "%s: not allowed for move type '%s'" % (key, move.get("type")))
    elif move_type == MOVE_TYPES["hop"]:
        for key in ("dx", "dy"):
            if key not in move:
                errors.add(ctx + ".move", "%s: required for move type 'hop'" % key)
        move_dx = read_int(errors, ctx + ".move", move, "dx", -128, 127, default=0)
        move_dy = read_int(errors, ctx + ".move", move, "dy", -128, 127, default=0)
        if "speedF" in move:
            errors.add(ctx + ".move", "speedF: not allowed for move type 'hop'")
    elif move_type == MOVE_TYPES["none"]:
        for key in ("speedF", "dx", "dy"):
            if key in move:
                errors.add(ctx + ".move", "%s: not allowed for move type 'none'" % key)
    on_hit = obj.get("onHit")
    if on_hit is not None:
        check_keys(errors, ctx + ".onHit", on_hit, set(), {"effect", "push", "stun"})
    else:
        on_hit = {}
    return {
        "id": read_id(errors, ctx, obj, "id"),
        "windup": read_int(errors, ctx, obj, "windup", 0, 65535),
        "active": active if active is not None else 0,
        "recover": read_int(errors, ctx, obj, "recover", 0, 65535),
        "dmg": read_int(errors, ctx, obj, "dmg", 0, 65535),
        "phys": read_enum(errors, ctx, obj, "phys", PHYS),
        "elem": read_enum(errors, ctx, obj, "elem", ELEMS),
        "moveType": move_type if move_type is not None else 0,
        "moveSpeedF": move_speed if move_speed is not None else 0,
        "moveDx": move_dx if move_dx is not None else 0,
        "moveDy": move_dy if move_dy is not None else 0,
        "facing": read_enum(errors, ctx, obj, "facing", FACINGS),
        "onHitEffect": read_enum(errors, ctx + ".onHit", on_hit, "effect", ON_HIT_EFFECTS, default=0),
        "onHitPush": read_int(errors, ctx + ".onHit", on_hit, "push", -128, 127, default=0),
        "onHitStun": read_int(errors, ctx + ".onHit", on_hit, "stun", 0, 255, default=0),
        "stagger": read_int(errors, ctx, obj, "stagger", 0, 255, default=0),
        "cue": read_enum(errors, ctx, obj, "cue", CUES, default=0),
        # wallStun: ticks the beast self-stuns when a moving attack's clamp
        # reaches a room bound (feel.4). 0 (default) ships the branch inert.
        "wallStun": read_int(errors, ctx, obj, "wallStun", 0, 255, default=0),
        # tell: windup telegraph shape (feel.5); "dot" (0) is the shipped default.
        "tell": read_enum(errors, ctx, obj, "tell", TELLS, default=0),
        "windows": windows,
        "art": normalize_attack_art(errors, ctx + ".art", obj.get("art"), sheet_names),
    }


def normalize_guard(errors, ctx, obj):
    guard = {"minDist": 0, "maxDist": 255, "hpLo": 0, "hpHi": 100, "playerFlags": 0, "cooldown": 0, "chance": 100, "zonesBroken": [], "facing": 0}
    if obj is None:
        return guard
    check_keys(errors, ctx, obj, set(), {"minDist", "maxDist", "hpBand", "zonesBroken", "player", "cooldown", "chance", "facing"})
    guard["minDist"] = read_int(errors, ctx, obj, "minDist", 0, 255, default=0)
    guard["maxDist"] = read_int(errors, ctx, obj, "maxDist", 0, 255, default=255)
    if guard["minDist"] is not None and guard["maxDist"] is not None and guard["minDist"] > guard["maxDist"]:
        errors.add(ctx, "guard is inverted: minDist %d > maxDist %d" % (guard["minDist"], guard["maxDist"]))
    if "hpBand" in obj:
        band = obj["hpBand"]
        if not isinstance(band, list) or len(band) != 2:
            errors.add(ctx, "hpBand: expected [lo, hi]")
        else:
            lo = read_int(errors, ctx + ".hpBand", {"lo": band[0]}, "lo", 0, 100)
            hi = read_int(errors, ctx + ".hpBand", {"hi": band[1]}, "hi", 0, 100)
            if lo is not None and hi is not None:
                if lo > hi:
                    errors.add(ctx, "hpBand is inverted: lo %d > hi %d" % (lo, hi))
                guard["hpLo"] = lo
                guard["hpHi"] = hi
    if "zonesBroken" in obj:
        zones = obj["zonesBroken"]
        if not isinstance(zones, list):
            errors.add(ctx, "zonesBroken: expected an array of zone names")
        else:
            for i, name in enumerate(zones):
                if not isinstance(name, str) or name not in ZONE_NAMES:
                    errors.add(ctx, "zonesBroken[%d]: unknown zone %r (want head or appendage)" % (i, name))
                    continue
                if name in guard["zonesBroken"]:
                    errors.add(ctx, "zonesBroken[%d]: duplicate zone '%s'" % (i, name))
                    continue
                guard["zonesBroken"].append(name)
    player = obj.get("player")
    if player is not None:
        check_keys(errors, ctx + ".player", player, set(), {"attacking"})
        if "attacking" in player:
            attacking = read_bool(errors, ctx + ".player", player, "attacking")
            if attacking:
                guard["playerFlags"] |= 0x01
    guard["cooldown"] = read_int(errors, ctx, obj, "cooldown", 0, 255, default=0)
    guard["chance"] = read_int(errors, ctx, obj, "chance", 0, 100, default=100)
    guard["facing"] = read_enum(errors, ctx, obj, "facing", GUARD_FACINGS, default=0)
    return guard


def normalize_pattern(errors, ctx, obj, attack_ids):
    check_keys(errors, ctx, obj, {"id", "steps"}, {"guard"})
    raw_steps = obj.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        errors.add(ctx, "steps: expected a non-empty array")
        raw_steps = []
    steps = []
    for i, step in enumerate(raw_steps):
        c = "%s.steps[%d]" % (ctx, i)
        if not isinstance(step, dict):
            errors.add(c, "expected an object")
            continue
        if "atk" in step:
            check_keys(errors, c, step, {"atk"}, {"after", "chance"})
            atk = step.get("atk")
            if not isinstance(atk, str) or atk not in attack_ids:
                errors.add(c, "atk: unknown attack id %r" % atk)
            steps.append({
                "kind": STEP_ATK,
                "ref": atk if isinstance(atk, str) and atk in attack_ids else None,
                "after": read_int(errors, c, step, "after", 0, 255, default=0),
                "chance": read_int(errors, c, step, "chance", 0, 100, default=100),
            })
        elif "wait" in step:
            check_keys(errors, c, step, {"wait"}, {"after"})
            if "chance" in step:
                errors.add(c, "chance: not allowed on a WAIT step")
            steps.append({
                "kind": STEP_WAIT,
                "ref": read_int(errors, c, step, "wait", 1, 255),
                "after": read_int(errors, c, step, "after", 0, 255, default=0),
                "chance": 100,
            })
        else:
            errors.add(c, "expected either an 'atk' or a 'wait' step")
    return {
        "id": read_id(errors, ctx, obj, "id"),
        "guard": normalize_guard(errors, ctx + ".guard", obj.get("guard")),
        "steps": steps,
    }


def load_json(errors, path):
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        errors.add(path, "cannot read: %s" % exc)
    except ValueError as exc:
        errors.add(path, "invalid JSON: %s" % exc)
    return None


def load_item_ids(errors, root):
    """Ordered item ids from data/items.json (source order == item index).

    Only needed when a creature authors a `carve` table; a tree without the item
    file compiles as long as no carve block is present, so lightweight schema
    fixtures do not have to ship the full item table (prg.2)."""
    path = os.path.join(root, ITEMS_REL)
    if not os.path.isfile(path):
        return None
    doc = load_json(errors, path)
    if doc is None:
        return None
    check_keys(errors, ITEMS_REL, doc, {"version", "items"})
    read_int(errors, ITEMS_REL, doc, "version", 1, 1)
    raw_items = doc.get("items")
    if not isinstance(raw_items, list) or not raw_items:
        errors.add(ITEMS_REL, "items: expected a non-empty array")
        return None
    ids = []
    seen = set()
    for i, obj in enumerate(raw_items):
        ids.append(read_id(errors, "%s: items[%d]" % (ITEMS_REL, i), obj, "id", seen))
    return None if errors.items else ids


def load_art_sheets(errors, root):
    """Ordered art sheet names from data/art_sheets.json (epic monhun-ardu-bih).

    A creature's `art.sheet` resolves to a 1-based index in this list (the ART
    record stores the index, never a cart address; the address table is emitted
    post-pack into src/generated/art_sheets.hpp). Only needed when a creature
    authors an `art` block, so lightweight schema fixtures stay valid without
    the file."""
    path = os.path.join(root, ART_SHEETS_REL)
    if not os.path.isfile(path):
        return None
    doc = load_json(errors, path)
    if doc is None:
        return None
    check_keys(errors, ART_SHEETS_REL, doc, {"version", "sheets"})
    read_int(errors, ART_SHEETS_REL, doc, "version", 1, 1)
    raw_sheets = doc.get("sheets")
    if not isinstance(raw_sheets, list) or not raw_sheets:
        errors.add(ART_SHEETS_REL, "sheets: expected a non-empty array")
        return None
    names = []
    seen = set()
    for i, name in enumerate(raw_sheets):
        ctx = "%s: sheets[%d]" % (ART_SHEETS_REL, i)
        if not isinstance(name, str) or not ID_RE.match(name):
            errors.add(ctx, "expected a sheet name matching [a-z][a-z0-9_]*")
            continue
        if name in seen:
            errors.add(ctx, "duplicate art sheet '%s'" % name)
        seen.add(name)
        names.append(name)
    return None if errors.items else names


ART_FIELDS = ("sheet", "anchorY", "stride", "idle0", "idleCount", "windup", "attack", "recover", "flash", "dead")


def normalize_art(errors, ctx, obj, sheet_names):
    """Optional per-creature art descriptor (epic monhun-ardu-bih).

    `sheet` names a data/art_sheets.json entry and packs as its 1-based index.
    Every frame slot is required so a future creature cannot silently inherit a
    half-authored record; the field order is the packed ART record order."""
    check_keys(errors, ctx, obj, set(ART_FIELDS))
    name = obj.get("sheet")
    if name is None:
        errors.add(ctx, "sheet: required")
        index = 0
    elif not isinstance(name, str):
        errors.add(ctx, "sheet: expected a sheet name string")
        index = 0
    elif sheet_names is None:
        errors.add(ART_SHEETS_REL, "missing art sheet file (art.sheet resolves against it)")
        index = 0
    elif name not in sheet_names:
        errors.add(ctx, "sheet: unknown art sheet %r" % name)
        index = 0
    else:
        index = sheet_names.index(name) + 1
    return {
        "sheet": index,
        "anchorY": read_int(errors, ctx, obj, "anchorY", -128, 127),
        "stride": read_int(errors, ctx, obj, "stride", 0, 255),
        "idle0": read_int(errors, ctx, obj, "idle0", 0, 255),
        "idleCount": read_int(errors, ctx, obj, "idleCount", 0, 255),
        "windup": read_int(errors, ctx, obj, "windup", 0, 255),
        "attack": read_int(errors, ctx, obj, "attack", 0, 255),
        "recover": read_int(errors, ctx, obj, "recover", 0, 255),
        "flash": read_int(errors, ctx, obj, "flash", 0, 255),
        "dead": read_int(errors, ctx, obj, "dead", 0, 255),
    }


ZERO_ART = {"sheet": 0, "anchorY": 0, "stride": 0, "idle0": 0, "idleCount": 0,
            "windup": 0, "attack": 0, "recover": 0, "flash": 0, "dead": 0}

# Per-attack art triple (bih.4): sheet index 0 = no attack overlay -> the
# generic base body draws; frame base 0; normal (non-spin) mode.
ZERO_ATTACK_ART = {"sheet": 0, "frame": 0, "mode": 0}


def normalize_carve(errors, ctx, raw, item_ids):
    """Optional per-creature carve table (prg.3): [{item, count, chance}, ...].

    Item names resolve to data/items.json indices; count is 1..3; chance is the
    deterministic per-entry drop percent. At most CARVE_SLOTS entries; the packer
    pads the fixed record with count-0 empty slots."""
    if raw is None:
        return []
    if not isinstance(raw, list):
        errors.add(ctx, "carve: expected an array")
        return []
    if len(raw) > CARVE_SLOTS:
        errors.add(ctx, "size limit: %d carve entries exceed the %d slot cap" % (len(raw), CARVE_SLOTS))
    if item_ids is None:
        errors.add(ITEMS_REL, "missing item file (carve item ids resolve against it)")
        return []
    entries = []
    seen = set()
    for i, entry in enumerate(raw):
        c = "%s[%d]" % (ctx, i)
        check_keys(errors, c, entry, {"item", "count", "chance"})
        item = entry.get("item") if isinstance(entry, dict) else None
        if not isinstance(item, str) or item not in item_ids:
            errors.add(c, "item: unknown item id %r" % item)
            index = 0
        else:
            index = item_ids.index(item)
            if item in seen:
                errors.add(c, "duplicate item '%s'" % item)
            seen.add(item)
        count = read_int(errors, c, entry, "count", CARVE_COUNT_MIN, CARVE_COUNT_MAX)
        chance = read_int(errors, c, entry, "chance", 0, 100)
        entries.append({
            "item": index,
            "count": count if count is not None else 0,
            "chance": chance if chance is not None else 0,
        })
    return entries


PROFILE_REQUIRED = {"engageDist", "keepDist", "attackDist", "circleNum", "circleDen", "retreatNum", "retreatDen", "cdBase", "cdJitter", "spawnT", "spawnCd", "stunRecoverT"}
PROFILE_OPTIONAL = {"staggerMax", "staggerDecay", "staggerRecoverT", "faceHold", "turnRate"}


def zero_profile():
    """Static props carry no behaviour: an inert all-zero profile (denominators
    stay 1 so a stray divide never faults). The generator still emits a profile
    record per creature, so the profiles section stays 1:1 with creatures."""
    return {
        "engageDist": 0, "keepDist": 0, "attackDist": 0,
        "circleNum": 0, "circleDen": 1, "retreatNum": 0, "retreatDen": 1,
        "cdBase": 0, "cdJitter": 0, "spawnT": 0, "spawnCd": 0, "stunRecoverT": 0,
        "staggerMax": 0, "staggerDecay": 0, "staggerRecoverT": 0, "faceHold": 0, "turnRate": 0,
    }


def normalize_profile(errors, ctx, obj):
    check_keys(errors, ctx, obj, PROFILE_REQUIRED, PROFILE_OPTIONAL)
    return {
        "engageDist": read_int(errors, ctx, obj, "engageDist", 0, 255),
        "keepDist": read_int(errors, ctx, obj, "keepDist", 0, 255),
        "attackDist": read_int(errors, ctx, obj, "attackDist", 0, 255),
        "circleNum": read_int(errors, ctx, obj, "circleNum", 0, 255),
        "circleDen": read_int(errors, ctx, obj, "circleDen", 1, 255),
        "retreatNum": read_int(errors, ctx, obj, "retreatNum", 0, 255),
        "retreatDen": read_int(errors, ctx, obj, "retreatDen", 1, 255),
        "cdBase": read_int(errors, ctx, obj, "cdBase", 0, 65535),
        "cdJitter": read_int(errors, ctx, obj, "cdJitter", 0, 65535),
        "spawnT": read_int(errors, ctx, obj, "spawnT", 0, 65535),
        "spawnCd": read_int(errors, ctx, obj, "spawnCd", 0, 65535),
        "stunRecoverT": read_int(errors, ctx, obj, "stunRecoverT", 0, 65535),
        "staggerMax": read_int(errors, ctx, obj, "staggerMax", 0, 255, default=0),
        "staggerDecay": read_int(errors, ctx, obj, "staggerDecay", 0, 255, default=0),
        "staggerRecoverT": read_int(errors, ctx, obj, "staggerRecoverT", 0, 255, default=0),
        # nch.4: optional turn-commitment cadence. 0 = recompute facing every tick
        # (shipped default); >0 = refresh facing only every `faceHold` ticks.
        "faceHold": read_int(errors, ctx, obj, "faceHold", 0, 255, default=0),
        # feel.14: optional turn-rate limit. 0 = snap the refreshed facing straight
        # onto the player delta (shipped default); 1..8 = step the cached DIR8
        # facing toward it by at most this many 45-degree steps per refresh.
        "turnRate": read_int(errors, ctx, obj, "turnRate", 0, 8, default=0),
    }


def compile_model(errors, root):
    skeletons_path = os.path.join(root, SKELETONS_REL)
    if not os.path.isfile(skeletons_path):
        errors.add(SKELETONS_REL, "missing skeleton file")
        return None
    doc = load_json(errors, skeletons_path)
    if doc is None:
        return None
    check_keys(errors, SKELETONS_REL, doc, {"version", "skeletons"})
    read_int(errors, SKELETONS_REL, doc, "version", 1, 1)
    raw_skeletons = doc.get("skeletons")
    if not isinstance(raw_skeletons, list) or not raw_skeletons:
        errors.add(SKELETONS_REL, "skeletons: expected a non-empty array")
        return None
    skeletons = []
    skeleton_ids = set()
    for i, obj in enumerate(raw_skeletons):
        ctx = "data/skeletons.json: skeletons[%d]" % i
        check_keys(errors, ctx, obj, {"id", "anchors"})
        sid = read_id(errors, ctx, obj, "id", skeleton_ids)
        raw_anchors = obj.get("anchors")
        if not isinstance(raw_anchors, list):
            errors.add(ctx, "anchors: expected an array")
            raw_anchors = []
        anchors = []
        anchor_ids = set()
        for j, anchor in enumerate(raw_anchors):
            ac = "%s.anchors[%d]" % (ctx, j)
            check_keys(errors, ac, anchor, {"id", "ox", "oy"})
            anchors.append({
                "id": read_id(errors, ac, anchor, "id", anchor_ids),
                "ox": read_int(errors, ac, anchor, "ox", -128, 127),
                "oy": read_int(errors, ac, anchor, "oy", -128, 127),
            })
        if sid is not None:
            skeletons.append({"id": sid, "anchors": anchors})
    if errors.items:
        return None
    skeletons.sort(key=lambda skeleton: skeleton["id"])

    creatures_dir = os.path.join(root, CREATURES_REL)
    if not os.path.isdir(creatures_dir):
        errors.add(CREATURES_REL, "missing creature directory")
        return None
    creature_files = sorted(name for name in os.listdir(creatures_dir) if name.endswith(".json"))
    if not creature_files:
        errors.add(CREATURES_REL, "no creature JSON files found")
        return None
    skeletons_by_id = {skeleton["id"]: skeleton for skeleton in skeletons}
    item_ids = load_item_ids(errors, root)
    art_sheet_names = load_art_sheets(errors, root)
    creatures = []
    creature_ids = set()
    for name in creature_files:
        path = os.path.join(creatures_dir, name)
        ctx = "%s/%s" % (CREATURES_REL, name)
        obj = load_json(errors, path)
        if obj is None:
            continue
        check_keys(errors, ctx, obj, {"id", "skeleton", "stats"},
                   {"profile", "attacks", "patterns", "zones", "collide", "static", "sheet", "carve", "art"})
        is_static = bool(read_bool(errors, ctx, obj, "static", default=0))
        sheet_id = read_int(errors, ctx, obj, "sheet", 0, 255, default=0)
        cid = read_id(errors, ctx, obj, "id")
        if cid is not None:
            if cid != os.path.splitext(name)[0]:
                errors.add(ctx, "id '%s' does not match file name '%s'" % (cid, name))
            if cid in creature_ids:
                errors.add(ctx, "duplicate creature id '%s'" % cid)
            creature_ids.add(cid)
        skeleton_id = obj.get("skeleton")
        skeleton = skeletons_by_id.get(skeleton_id) if isinstance(skeleton_id, str) else None
        if skeleton is None:
            errors.add(ctx, "skeleton: unknown skeleton id %r" % skeleton_id)
            skeleton = skeletons[0]
        stats = obj.get("stats")
        check_keys(errors, ctx + ".stats", stats, {"w", "h", "hp", "spd", "spawnX", "spawnY"}, {"brokenBody", "enrage"})
        collide = normalize_collide(errors, ctx + ".collide", obj["collide"]) if "collide" in obj else None
        broken_body = normalize_broken_body(errors, ctx + ".stats.brokenBody", stats["brokenBody"]) if "brokenBody" in stats else None
        enrage = normalize_enrage(errors, ctx + ".stats.enrage", stats["enrage"]) if "enrage" in stats else {"hpPct": 0, "spdMul": 0, "faceHold": 0, "cue": 0}
        if "profile" in obj:
            profile = normalize_profile(errors, ctx + ".profile", obj.get("profile"))
        elif is_static:
            profile = zero_profile()
        else:
            errors.add(ctx, "profile: required for a non-static creature")
            profile = zero_profile()
        raw_attacks = obj.get("attacks")
        if raw_attacks is None:
            if not is_static:
                errors.add(ctx, "attacks: required for a non-static creature")
            raw_attacks = []
        if not isinstance(raw_attacks, list):
            errors.add(ctx, "attacks: expected an array")
            raw_attacks = []
        elif not raw_attacks and not is_static:
            errors.add(ctx, "attacks: expected a non-empty array")
        attacks = []
        attack_ids = set()
        for i, attack in enumerate(raw_attacks):
            ac = "%s.attacks[%d]" % (ctx, i)
            attack = normalize_attack(errors, ac, attack, art_sheet_names)
            if attack["id"] is not None:
                if attack["id"] in attack_ids:
                    errors.add(ac, "duplicate local id '%s'" % attack["id"])
                attack_ids.add(attack["id"])
            attacks.append(attack)
        raw_zones = obj.get("zones")
        zones = {}
        if raw_zones is not None:
            if not isinstance(raw_zones, dict):
                errors.add(ctx, "zones: expected an object")
            else:
                for name_key in sorted(raw_zones):
                    if name_key not in ZONE_NAMES:
                        errors.add(ctx, "zones: unknown zone '%s' (want head or appendage)" % name_key)
                        continue
                    zc = "%s.zones.%s" % (ctx, name_key)
                    zones[name_key] = normalize_zone(errors, zc, raw_zones[name_key], attack_ids)
        raw_patterns = obj.get("patterns")
        if raw_patterns is None:
            if not is_static:
                errors.add(ctx, "patterns: required for a non-static creature")
            raw_patterns = []
        if not isinstance(raw_patterns, list):
            errors.add(ctx, "patterns: expected an array")
            raw_patterns = []
        elif not raw_patterns and not is_static:
            errors.add(ctx, "patterns: expected a non-empty array")
        patterns = []
        pattern_ids = set()
        for i, pattern in enumerate(raw_patterns):
            pc = "%s.patterns[%d]" % (ctx, i)
            pattern = normalize_pattern(errors, pc, pattern, attack_ids)
            if pattern["id"] is not None:
                if pattern["id"] in pattern_ids:
                    errors.add(pc, "duplicate local id '%s'" % pattern["id"])
                pattern_ids.add(pattern["id"])
            patterns.append(pattern)
        carve = normalize_carve(errors, ctx + ".carve", obj.get("carve"), item_ids)
        art = normalize_art(errors, ctx + ".art", obj.get("art"), art_sheet_names) if "art" in obj else dict(ZERO_ART)
        creatures.append({
            "id": cid,
            "skeleton": skeleton,
            "static": 1 if is_static else 0,
            "sheet": sheet_id,
            "stats": {
                "w": read_int(errors, ctx + ".stats", stats, "w", 1, 255),
                "h": read_int(errors, ctx + ".stats", stats, "h", 1, 255),
                "hp": read_int(errors, ctx + ".stats", stats, "hp", 0, 65535),
                "spd": read_int(errors, ctx + ".stats", stats, "spd", 0, 255),
                "spawnX": read_int(errors, ctx + ".stats", stats, "spawnX", 0, 65535),
                "spawnY": read_int(errors, ctx + ".stats", stats, "spawnY", 0, 65535),
            },
            "brokenBody": broken_body,
            "enrage": enrage,
            "collide": collide,
            "profile": profile,
            "attacks": attacks,
            "zones": zones,
            "patterns": patterns,
            "carve": carve,
            "art": art,
        })
    if errors.items:
        return None
    creatures.sort(key=lambda creature: creature["id"])
    return {"skeletons": skeletons, "creatures": creatures}


def u8(value):
    return bytes([value & 0xFF])


def i8(value):
    return bytes([value & 0xFF])


def u16(value):
    return bytes([value & 0xFF, (value >> 8) & 0xFF])


def zone_flag(name):
    return ZONE_HEAD if name == "head" else ZONE_APPENDAGE


def build_layout(model):
    """Derive every global index, offset-independent, in deterministic order."""
    creatures = model["creatures"]
    skeletons = model["skeletons"]
    layout = {"creatures": [], "skeletons": [], "zones": [], "anchors": [],
              "attacks": [], "windows": [], "patterns": [], "guards": [], "steps": []}

    # Fixed zone order per creature: head then appendage (only present records).
    zone_keys = [(c["id"], name) for c in creatures for name in ZONE_NAMES if name in c["zones"]]
    zone_index = {key: i for i, key in enumerate(zone_keys)}

    attack_keys = [(c["id"], a["id"]) for c in creatures for a in c["attacks"]]
    attack_index = {key: i for i, key in enumerate(attack_keys)}
    window_keys = [(c["id"], a["id"], i) for c in creatures for a in c["attacks"] for i in range(len(a["windows"]))]
    window_index = {key: i for i, key in enumerate(window_keys)}
    pattern_keys = [(c["id"], p["id"]) for c in creatures for p in c["patterns"]]
    pattern_index = {key: i for i, key in enumerate(pattern_keys)}
    step_keys = [(c["id"], p["id"], i) for c in creatures for p in c["patterns"] for i in range(len(p["steps"]))]
    step_index = {key: i for i, key in enumerate(step_keys)}

    anchor_index = 0
    for skeleton in skeletons:
        layout["skeletons"].append({"skeleton": skeleton, "first_anchor": anchor_index})
        for anchor in skeleton["anchors"]:
            layout["anchors"].append({"skeleton": skeleton, "anchor": anchor})
            anchor_index += 1

    for creature in creatures:
        cid = creature["id"]
        head_idx = zone_index.get((cid, "head"), COMBAT_NO_ZONE)
        append_idx = zone_index.get((cid, "appendage"), COMBAT_NO_ZONE)
        layout["creatures"].append({
            "creature": creature,
            "head_zone": head_idx,
            "append_zone": append_idx,
            "first_attack": attack_index[(cid, creature["attacks"][0]["id"])] if creature["attacks"] else 0,
            "first_pattern": pattern_index[(cid, creature["patterns"][0]["id"])] if creature["patterns"] else 0,
        })
        for name in ZONE_NAMES:
            if name in creature["zones"]:
                layout["zones"].append({"key": (cid, name), "creature": creature, "name": name,
                                        "zone": creature["zones"][name],
                                        "unlock": zone_unlock_mask(creature, creature["zones"][name], attack_index)})
        for attack in creature["attacks"]:
            layout["attacks"].append({
                "creature": creature,
                "attack": attack,
                "first_window": window_index[(cid, attack["id"], 0)] if attack["windows"] else 0,
            })
            for i, window in enumerate(attack["windows"]):
                layout["windows"].append({"creature": creature, "attack": attack, "window": window, "index": i})
        for pattern in creature["patterns"]:
            mask = 0
            for name in pattern["guard"]["zonesBroken"]:
                mask |= zone_flag(name)
            layout["patterns"].append({
                "creature": creature,
                "pattern": pattern,
                "first_step": step_index[(cid, pattern["id"], 0)] if pattern["steps"] else 0,
                "guard_idx": pattern_index[(cid, pattern["id"])],
            })
            layout["guards"].append({"creature": creature, "pattern": pattern, "guard": pattern["guard"], "zones_mask": mask})
            for i, step in enumerate(pattern["steps"]):
                layout["steps"].append({"creature": creature, "pattern": pattern, "step": step, "index": i})

    indices = {
        "zone": zone_index,
        "attack": attack_index,
        "window": window_index,
        "pattern": pattern_index,
        "step": step_index,
    }
    return layout, indices


def zone_unlock_mask(creature, zone, attack_index):
    mask = 0
    for attack_id in zone["brokenDisable"]:
        mask |= 1 << attack_index[(creature["id"], attack_id)]
    return mask


def pack_model(errors, model):
    layout, index_maps = build_layout(model)
    zone_index = index_maps["zone"]
    attack_index = index_maps["attack"]
    window_index = index_maps["window"]
    pattern_index = index_maps["pattern"]
    step_index = index_maps["step"]

    counts = {
        "CREATURES": len(layout["creatures"]), "PROFILES": len(layout["creatures"]),
        "SKELETONS": len(layout["skeletons"]), "ZONES": len(layout["zones"]),
        "ANCHORS": len(layout["anchors"]),
        "ATTACKS": len(layout["attacks"]), "WINDOWS": len(layout["windows"]),
        "PATTERNS": len(layout["patterns"]), "GUARDS": len(layout["guards"]),
        "STEPS": len(layout["steps"]),
        "ART": len(layout["creatures"]),
    }
    for section, count in counts.items():
        if count > 255:
            errors.add("data", "size limit: %d %ss exceed the 255 record limit" % (count, section.lower().rstrip("s")))
    # unlockMask is a u16 bit per global attack index (lo/hi bytes in the packed
    # record), so the ABI limit is on the *index of a disabled attack*, not the
    # total attack count: a zone can disable global indices 0..15. The runtime
    # mirrors this (combatAttackDisabled returns false for attackIdx >= 16); the
    # per-zone `unlock > 0xFFFF` check below rejects the unrepresentable case.
    if errors.items:
        return None

    body = bytearray()
    offsets = {}
    indices = {}
    section_off = {}
    section_count = dict(counts)

    def record(name, data):
        assert len(data) == SIZES[name], (name, len(data))
        body.extend(data)

    def mark(name):
        return HEADER_SIZE + len(body)

    # creatures
    section_off["CREATURES"] = mark("creatures")
    for i, entry in enumerate(layout["creatures"]):
        creature = entry["creature"]
        cid = creature["id"]
        indices["CREATURE_%s" % cid.upper()] = i
        offsets["CREATURE_%s_OFF" % cid.upper()] = mark("creature")
        stats = creature["stats"]
        # collide box: authored per creature, or the body box default (stats w/h
        # at the origin) so collision is unchanged for creatures without one.
        collide = creature["collide"] or {"ox": 0, "oy": 0, "w": stats["w"], "h": stats["h"]}
        broken_body = creature["brokenBody"] or {"w": 0, "h": 0}
        enrage = creature["enrage"]
        carve_bytes = bytearray()
        for slot in range(CARVE_SLOTS):
            drop = creature["carve"][slot] if slot < len(creature["carve"]) else {"item": 0, "count": 0, "chance": 0}
            carve_bytes += u8(drop["item"]) + u8(drop["count"]) + u8(drop["chance"])
        record("CREATURE", b"".join([
            u8(model["skeletons"].index(creature["skeleton"])), u8(i),
            u8(entry["head_zone"]), u8(entry["append_zone"]),
            u8(entry["first_attack"]), u8(len(creature["attacks"])),
            u8(entry["first_pattern"]), u8(len(creature["patterns"])),
            u8(stats["w"]), u8(stats["h"]), u8(stats["spd"]),
            i8(collide["ox"]), i8(collide["oy"]), u8(collide["w"]), u8(collide["h"]),
            u16(stats["hp"]), u16(stats["spawnX"]), u16(stats["spawnY"]),
            u8(creature["static"]), u8(creature["sheet"]), u8(broken_body["w"]), u8(broken_body["h"]),
            u8(enrage["hpPct"]), u8(enrage["spdMul"]), u8(enrage["faceHold"]), u8(enrage["cue"] or 0),
            bytes(carve_bytes),
        ]))

    # profiles
    section_off["PROFILES"] = mark("profiles")
    for entry in layout["creatures"]:
        creature = entry["creature"]
        offsets["PROFILE_%s_OFF" % creature["id"].upper()] = mark("profile")
        profile = creature["profile"]
        zone_flags = 0
        for name in ZONE_NAMES:
            if name in creature["zones"]:
                zone_flags |= zone_flag(name)
        record("PROFILE", b"".join([
            u8(profile["engageDist"]), u8(profile["keepDist"]), u8(profile["attackDist"]),
            u8(profile["circleNum"]), u8(profile["circleDen"]),
            u8(profile["retreatNum"]), u8(profile["retreatDen"]),
            u8(profile["staggerMax"]), u8(profile["staggerDecay"]),
            u8(zone_flags), u8(profile["faceHold"]), u8(profile["turnRate"]),
            u16(profile["cdBase"]), u16(profile["cdJitter"]), u16(profile["spawnT"]),
            u16(profile["spawnCd"]), u16(profile["stunRecoverT"]), u16(profile["staggerRecoverT"]),
        ]))

    # skeletons
    section_off["SKELETONS"] = mark("skeletons")
    for i, entry in enumerate(layout["skeletons"]):
        skeleton = entry["skeleton"]
        indices["SKELETON_%s" % skeleton["id"].upper()] = i
        offsets["SKELETON_%s_OFF" % skeleton["id"].upper()] = mark("skeleton")
        record("SKELETON", b"".join([
            u8(entry["first_anchor"]), u8(len(skeleton["anchors"])),
        ]))

    # zones
    section_off["ZONES"] = mark("zones")
    for entry in layout["zones"]:
        key = entry["key"]
        zone = entry["zone"]
        owner_name = key[0].upper()
        zone_name = key[1].upper()
        indices["ZONE_%s_%s" % (owner_name, zone_name)] = zone_index[key]
        offsets["ZONE_%s_%s_OFF" % (owner_name, zone_name)] = mark("zone")
        box = zone["box"]
        broken_flags = (0x01 if zone["brokenHurtOff"] else 0) | (0x02 if zone["brokenCue"] else 0)
        unlock = zone_unlock_mask(entry["creature"], zone, attack_index)
        if unlock > 0xFFFF:
            errors.add("data", "unlockMask overflows u16 for %s %s (disable index >= 16)" % (key[0], key[1]))
            return None
        record("ZONE", b"".join([
            i8(box["ox"]), i8(box["oy"]), u8(box["w"]), u8(box["h"]),
            u8(zone["hp"]), u8(zone["dmgMul"]), u8(zone["bodyShare"]), u8(zone["breakTypes"] or 0),
            u8(zone["staggerOnHit"]), u8(zone["brokenDmgMul"]), u8(broken_flags), u16(unlock),
        ]))

    # anchors
    section_off["ANCHORS"] = mark("anchors")
    for entry in layout["anchors"]:
        skeleton = entry["skeleton"]
        anchor = entry["anchor"]
        offsets["ANCHOR_%s_%s_OFF" % (skeleton["id"].upper(), anchor["id"].upper())] = mark("anchor")
        record("ANCHOR", i8(anchor["ox"]) + i8(anchor["oy"]))

    # attacks
    section_off["ATTACKS"] = mark("attacks")
    for entry in layout["attacks"]:
        creature = entry["creature"]
        attack = entry["attack"]
        key = (creature["id"], attack["id"])
        name = "ATTACK_%s_%s" % (creature["id"].upper(), attack["id"].upper())
        indices[name] = attack_index[key]
        offsets[name + "_OFF"] = mark("attack")
        first_window = window_index[(key[0], key[1], 0)] if attack["windows"] else 0
        attack_art = attack["art"]
        record("ATTACK", b"".join([
            u8(attack["moveType"]), u8(attack["moveSpeedF"]), i8(attack["moveDx"]), i8(attack["moveDy"]),
            u8(attack["facing"]), u8(attack["phys"] or 0), u8(attack["elem"] or 0),
            u8(attack["onHitEffect"] or 0), i8(attack["onHitPush"] or 0), u8(attack["onHitStun"] or 0),
            u8(attack["stagger"]), u8(attack["cue"] or 0), u8(attack["wallStun"] or 0),
            u8(first_window), u8(len(attack["windows"])),
            u16(attack["windup"]), u16(attack["active"]), u16(attack["recover"]), u16(attack["dmg"]),
            u8(attack["tell"] or 0),
            u8(attack_art["sheet"]), u8(attack_art["frame"]), u8(attack_art["mode"]),
        ]))

    # windows
    section_off["WINDOWS"] = mark("windows")
    for entry in layout["windows"]:
        creature = entry["creature"]
        attack = entry["attack"]
        window = entry["window"]
        name = "WINDOW_%s_%s_%d" % (creature["id"].upper(), attack["id"].upper(), entry["index"])
        indices[name] = window_index[(creature["id"], attack["id"], entry["index"])]
        offsets[name + "_OFF"] = mark("window")
        box = window["box"]
        record("WINDOW", b"".join([
            u16(window["t0"]), u16(window["t1"]),
            i8(box["ox"]), i8(box["oy"]), u8(box["w"]), u8(box["h"]),
            u8(window["dmgMul"] or 0), u8(0),
        ]))

    # patterns
    section_off["PATTERNS"] = mark("patterns")
    for entry in layout["patterns"]:
        creature = entry["creature"]
        pattern = entry["pattern"]
        key = (creature["id"], pattern["id"])
        name = "PATTERN_%s_%s" % (creature["id"].upper(), pattern["id"].upper())
        indices[name] = pattern_index[key]
        offsets[name + "_OFF"] = mark("pattern")
        first_step = step_index[(key[0], key[1], 0)] if pattern["steps"] else 0
        record("PATTERN", u8(first_step) + u8(len(pattern["steps"])) + u8(pattern_index[key]))

    # guards
    section_off["GUARDS"] = mark("guards")
    for entry in layout["guards"]:
        creature = entry["creature"]
        pattern = entry["pattern"]
        guard = entry["guard"]
        name = "GUARD_%s_%s" % (creature["id"].upper(), pattern["id"].upper())
        indices[name] = pattern_index[(creature["id"], pattern["id"])]
        offsets[name + "_OFF"] = mark("guard")
        record("GUARD", b"".join([
            u8(guard["minDist"] if guard["minDist"] is not None else 0),
            u8(guard["maxDist"] if guard["maxDist"] is not None else 255),
            u8(guard["hpLo"]), u8(guard["hpHi"]), u8(guard["playerFlags"]),
            u8(guard["cooldown"] if guard["cooldown"] is not None else 0),
            u8(guard["chance"] if guard["chance"] is not None else 100),
            u8(entry["zones_mask"]),
            u8(guard["facing"]),
        ]))

    # steps
    section_off["STEPS"] = mark("steps")
    for entry in layout["steps"]:
        creature = entry["creature"]
        pattern = entry["pattern"]
        step = entry["step"]
        name = "STEP_%s_%s_%d" % (creature["id"].upper(), pattern["id"].upper(), entry["index"])
        indices[name] = step_index[(creature["id"], pattern["id"], entry["index"])]
        offsets[name + "_OFF"] = mark("step")
        ref = 0
        if step["kind"] == STEP_ATK:
            ref = attack_index[(creature["id"], step["ref"])]
        else:
            ref = step["ref"] if step["ref"] is not None else 0
        record("STEP", u8(step["kind"]) + u8(ref) + u8(step["after"] or 0) + u8(step["chance"] or 100))

    # art (epic monhun-ardu-bih): one 10 B descriptor per creature, appended
    # after every other section so all existing offsets stay stable. Index order
    # matches the CREATURES section (1:1), so the runtime reads ART_OFF + i*SIZE.
    section_off["ART"] = mark("art")
    for i, entry in enumerate(layout["creatures"]):
        creature = entry["creature"]
        art = creature["art"]
        cid = creature["id"]
        indices["ART_%s" % cid.upper()] = i
        offsets["ART_%s_OFF" % cid.upper()] = mark("art")
        record("ART", b"".join([
            u8(art["sheet"]), i8(art["anchorY"]), u8(art["stride"]), u8(art["idle0"]),
            u8(art["idleCount"]), u8(art["windup"]), u8(art["attack"]), u8(art["recover"]),
            u8(art["flash"]), u8(art["dead"]),
        ]))

    header = bytearray(u16(MAGIC) + u8(VERSION) + u8(FLAGS))
    for section in SECTION_ORDER:
        header.extend(u16(section_count[section]))
    for _ in range(RESERVED_COUNTS):
        header.extend(u16(0))
    assert len(header) == HEADER_SIZE
    blob = bytes(header) + bytes(body)
    if len(blob) >= 65536:
        errors.add("data", "size limit: blob is %d B, offsets are u16" % len(blob))
        return None
    for name in offsets:
        if offsets[name] >= 65536:
            errors.add("data", "size limit: offset %s does not fit in u16" % name)
            return None
    for name, value in indices.items():
        if value > 255:
            errors.add("data", "size limit: index %s does not fit in u8" % name)
            return None

    emitted_names = set()
    for name in sorted(set(offsets) | set(indices)):
        if name in emitted_names:
            errors.add("meta", "generated symbol collision: '%s'" % name)
            break
        emitted_names.add(name)
    if errors.items:
        return None
    return {"blob": blob, "section_off": section_off, "section_count": section_count,
            "offsets": offsets, "indices": indices, "layout": layout}


def emit_data_header(model, compiled):
    layout = compiled["layout"]
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-combat.py -- do not edit.")
    app("//")
    app("// Host-side plain structs and arrays (build/zones-design.md). Field order")
    app("// matches the packed FX blob byte order; the host reads members directly,")
    app("// so host struct padding is irrelevant. The device reads the blob with the")
    app("// offsets in combat_meta.hpp instead.")
    app("")
    app("#include <array>")
    app("#include <stdint.h>")
    app("")
    app("namespace combat_data {")
    app("")
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(compiled["blob"]))
    app("")
    app("struct Box {")
    app("    int8_t ox;")
    app("    int8_t oy;")
    app("    uint8_t w;")
    app("    uint8_t h;")
    app("};")
    app("")
    app("struct Window {")
    app("    uint16_t t0;")
    app("    uint16_t t1;")
    app("    Box box;")
    app("    uint8_t dmgMul;")
    app("    uint8_t flags;")
    app("};")
    app("")
    app("struct Profile {")
    app("    uint8_t engageDist, keepDist, attackDist;")
    app("    uint8_t circleNum, circleDen, retreatNum, retreatDen;")
    app("    uint8_t staggerMax, staggerDecay;")
    app("    uint8_t zoneFlags;")
    app("    uint8_t faceHold;   // 0 = recompute facing every tick")
    app("    uint8_t turnRate;   // DIR8 steps per facing refresh (0 = snap, feel.14)")
    app("    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;")
    app("};")
    app("")
    app("struct Skeleton {")
    app("    uint8_t firstAnchor, anchorCount;")
    app("};")
    app("")
    app("struct Zone {")
    app("    Box box;")
    app("    uint8_t hp, dmgMul, bodyShare, breakTypes, staggerOnHit;")
    app("    uint8_t brokenDmgMul, brokenFlags, unlockMaskLo, unlockMaskHi;")
    app("};")
    app("")
    app("struct Anchor {")
    app("    int8_t ox;")
    app("    int8_t oy;")
    app("};")
    app("")
    app("struct Attack {")
    app("    uint8_t moveType, moveSpeedF;")
    app("    int8_t moveDx, moveDy;")
    app("    uint8_t facing, phys, elem, onHitEffect;")
    app("    int8_t onHitPush;")
    app("    uint8_t onHitStun, stagger, cue;")
    app("    uint8_t wallStun;   // ticks self-stunned on a room-bound hit (0 = inert)")
    app("    uint8_t firstWindow, windowCount;")
    app("    uint16_t windup, active, recover, dmg;")
    app("    uint8_t tell;   // windup telegraph shape (0 dot default, feel.5)")
    app("    uint8_t artSheet, artFrame, artMode;   // whole-body attack art (bih.4)")
    app("};")
    app("")
    app("struct Pattern {")
    app("    uint8_t firstStep, stepCount, guardIdx;")
    app("};")
    app("")
    app("struct Guard {")
    app("    uint8_t minDist, maxDist, hpLo, hpHi, playerFlags, cooldown, chance;")
    app("    uint8_t zonesBroken;")
    app("    uint8_t facing;   // 0 any, 1 behind, 2 front");
    app("};")
    app("")
    app("struct Step {")
    app("    uint8_t kind, ref, after, chance;")
    app("};")
    app("")
    app("struct Carve {")
    app("    uint8_t item, count, chance;   // count 0 = empty slot")
    app("};")
    app("")
    app("struct Art {")
    app("    uint8_t sheet;   // 1-based index into art_sheets.hpp; 0 = legacy per-kind draw")
    app("    int8_t anchorY;  // art draw offset from the body-box top")
    app("    uint8_t stride, idle0, idleCount, windup, attack, recover, flash, dead;")
    app("};")
    app("")
    app("struct Creature {")
    app("    uint8_t skeletonIdx, profileIdx;")
    app("    uint8_t headZone, appendZone;")
    app("    uint8_t firstAttack, attackCount;")
    app("    uint8_t firstPattern, patternCount;")
    app("    uint8_t w, h, spd;")
    app("    Box collide;   // body-collision rect (legs-only for the chicken)")
    app("    uint16_t hp, spawnX, spawnY;")
    app("    uint8_t flags;   // bit0: static prop (pole); no FSM/attacks")
    app("    uint8_t sheet;   // art sheet id (0 = default monster sheet)")
    app("    uint8_t brokenW, brokenH;   // target rect on break (0 = unchanged)")
    app("    uint8_t enrageHpPct, enrageSpdMul, enrageFaceHold, enrageCue;   // hpPct 0 = disabled")
    app("    Carve carve[%d];   // prg.3 drop table; count 0 slots are inert" % CARVE_SLOTS)
    app("};")
    app("")
    app("// Index constants (creatures sorted by id; attacks, windows, patterns and")
    app("// steps keep source order inside each creature).")
    for name in sorted(compiled["indices"]):
        app("constexpr uint8_t %s = %d;" % (name, compiled["indices"][name]))
    app("")
    for section in SECTION_ORDER:
        record = SECTION_RECORD[section]
        count = compiled["section_count"][section]
        app("inline constexpr std::array<%s, %d> %s = {{" % (record.capitalize(), count, section))
        if section == "CREATURES":
            for entry in layout["creatures"]:
                creature = entry["creature"]
                stats = creature["stats"]
                collide = creature["collide"] or {"ox": 0, "oy": 0, "w": stats["w"], "h": stats["h"]}
                broken_body = creature["brokenBody"] or {"w": 0, "h": 0}
                enrage = creature["enrage"]
                carve = creature["carve"]
                carve_init = ", ".join("{%d, %d, %d}" % (
                    carve[slot]["item"] if slot < len(carve) else 0,
                    carve[slot]["count"] if slot < len(carve) else 0,
                    carve[slot]["chance"] if slot < len(carve) else 0) for slot in range(CARVE_SLOTS))
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d, %d}, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, {%s}}," % (
                    model["skeletons"].index(creature["skeleton"]), compiled["indices"]["CREATURE_%s" % creature["id"].upper()],
                    entry["head_zone"], entry["append_zone"], entry["first_attack"], len(creature["attacks"]),
                    entry["first_pattern"], len(creature["patterns"]),
                    stats["w"], stats["h"], stats["spd"],
                    collide["ox"], collide["oy"], collide["w"], collide["h"],
                    stats["hp"], stats["spawnX"], stats["spawnY"],
                    creature["static"], creature["sheet"], broken_body["w"], broken_body["h"],
                    enrage["hpPct"], enrage["spdMul"], enrage["faceHold"], enrage["cue"] or 0,
                    carve_init))
        elif section == "PROFILES":
            for entry in layout["creatures"]:
                creature = entry["creature"]
                profile = creature["profile"]
                zone_flags = 0
                for name in ZONE_NAMES:
                    if name in creature["zones"]:
                        zone_flags |= zone_flag(name)
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    profile["engageDist"], profile["keepDist"], profile["attackDist"],
                    profile["circleNum"], profile["circleDen"], profile["retreatNum"], profile["retreatDen"],
                    profile["staggerMax"], profile["staggerDecay"], zone_flags, profile["faceHold"], profile["turnRate"],
                    profile["cdBase"], profile["cdJitter"], profile["spawnT"], profile["spawnCd"],
                    profile["stunRecoverT"], profile["staggerRecoverT"]))
        elif section == "SKELETONS":
            for entry in layout["skeletons"]:
                skeleton = entry["skeleton"]
                app("    {%d, %d}," % (entry["first_anchor"], len(skeleton["anchors"])))
        elif section == "ZONES":
            for entry in layout["zones"]:
                zone = entry["zone"]
                box = zone["box"]
                broken_flags = (0x01 if zone["brokenHurtOff"] else 0) | (0x02 if zone["brokenCue"] else 0)
                unlock = entry["unlock"]
                app("    {{%d, %d, %d, %d}, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    box["ox"], box["oy"], box["w"], box["h"], zone["hp"], zone["dmgMul"],
                    zone["bodyShare"], zone["breakTypes"] or 0, zone["staggerOnHit"],
                    zone["brokenDmgMul"], broken_flags, unlock & 0xFF, (unlock >> 8) & 0xFF))
        elif section == "ANCHORS":
            for entry in layout["anchors"]:
                app("    {%d, %d}," % (entry["anchor"]["ox"], entry["anchor"]["oy"]))
        elif section == "ATTACKS":
            for entry in layout["attacks"]:
                attack = entry["attack"]
                art = attack["art"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    attack["moveType"], attack["moveSpeedF"], attack["moveDx"], attack["moveDy"],
                    attack["facing"], attack["phys"] or 0, attack["elem"] or 0,
                    attack["onHitEffect"] or 0, attack["onHitPush"] or 0, attack["onHitStun"] or 0,
                    attack["stagger"], attack["cue"] or 0, attack["wallStun"] or 0,
                    entry["first_window"], len(attack["windows"]),
                    attack["windup"], attack["active"], attack["recover"], attack["dmg"], attack["tell"] or 0,
                    art["sheet"], art["frame"], art["mode"]))
        elif section == "WINDOWS":
            for entry in layout["windows"]:
                window = entry["window"]
                box = window["box"]
                app("    {%d, %d, {%d, %d, %d, %d}, %d, %d}," % (
                    window["t0"], window["t1"], box["ox"], box["oy"], box["w"], box["h"],
                    window["dmgMul"] or 0, 0))
        elif section == "PATTERNS":
            for entry in layout["patterns"]:
                app("    {%d, %d, %d}," % (entry["first_step"], len(entry["pattern"]["steps"]), entry["guard_idx"]))
        elif section == "GUARDS":
            for entry in layout["guards"]:
                guard = entry["guard"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    guard["minDist"] if guard["minDist"] is not None else 0,
                    guard["maxDist"] if guard["maxDist"] is not None else 255,
                    guard["hpLo"], guard["hpHi"], guard["playerFlags"],
                    guard["cooldown"] if guard["cooldown"] is not None else 0,
                    guard["chance"] if guard["chance"] is not None else 100,
                    entry["zones_mask"], guard["facing"]))
        elif section == "STEPS":
            for entry in layout["steps"]:
                step = entry["step"]
                ref = 0
                if step["kind"] == STEP_ATK:
                    ref = compiled["indices"].get("ATTACK_%s_%s" % (entry["creature"]["id"].upper(), step["ref"].upper()), 0)
                else:
                    ref = step["ref"] if step["ref"] is not None else 0
                app("    {%d, %d, %d, %d}," % (step["kind"], ref, step["after"] or 0, step["chance"] or 100))
        elif section == "ART":
            for entry in layout["creatures"]:
                art = entry["creature"]["art"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    art["sheet"], art["anchorY"], art["stride"], art["idle0"], art["idleCount"],
                    art["windup"], art["attack"], art["recover"], art["flash"], art["dead"]))
        app("}};")
        app("")
    app("}   // namespace combat_data")
    app("")
    return "\n".join(lines)


def data_facts(model):
    """Compile-time capabilities of the compiled data (emitted as constexpr bools).

    The shipping interpreter keeps the full generic paths, but folds away the
    machinery the current data never reaches (plain `if` on a constexpr bool;
    gcc 7.3/gnu++11, no `if constexpr`). Each fact is true iff at least one
    shipped record uses the feature, so a future creature opts back in by
    adding data and re-running gen-combat.py.
    """
    has_stagger = any(c["profile"]["staggerMax"] > 0 for c in model["creatures"])
    has_hit_stagger = False
    has_wait_steps = False
    has_step_after = False
    has_step_chance = False
    has_multi_step = False
    has_multi_window = False
    simple_guards = True
    has_guard_hp = False
    has_guard_player = False
    has_guard_cooldown = False
    has_guard_chance = False
    has_guard_zones = False
    has_guard_facing = False
    has_zones = any(c["zones"] for c in model["creatures"])
    has_enrage = any(c["enrage"]["hpPct"] > 0 for c in model["creatures"])
    # prg.3: true once any creature authors a drop table. Emitted for the
    # data-fact ledger; the runtime carve interact folds on MH_CARVE (shipping
    # and host on, the legacy parity image off), not on this fact.
    has_carve = any(c["carve"] for c in model["creatures"])
    # feel.14: true once any creature authors a bounded turn rate. Emitted for
    # the data-fact ledger; the interpreter does not fold on it yet (the host
    # tests drive synthetic turnRate values before any kit authors one).
    has_turn_rate = any(c["profile"]["turnRate"] > 0 for c in model["creatures"])
    for creature in model["creatures"]:
        for attack in creature["attacks"]:
            if len(attack["windows"]) > 1:
                has_multi_window = True
            if attack["stagger"]:
                has_hit_stagger = True
        for pattern in creature["patterns"]:
            guard = pattern["guard"]
            if guard["hpLo"] != 0 or guard["hpHi"] != 100 or guard["playerFlags"] != 0 \
                    or guard["cooldown"] != 0 or guard["chance"] != 100 or guard["zonesBroken"] \
                    or guard["facing"] != 0:
                simple_guards = False
            if guard["hpLo"] != 0 or guard["hpHi"] != 100:
                has_guard_hp = True
            if guard["playerFlags"] != 0:
                has_guard_player = True
            if guard["cooldown"] != 0:
                has_guard_cooldown = True
            if guard["chance"] != 100:
                has_guard_chance = True
            if guard["zonesBroken"]:
                has_guard_zones = True
            if guard["facing"] != 0:
                has_guard_facing = True
            if len(pattern["steps"]) > 1:
                has_multi_step = True
            for step in pattern["steps"]:
                if step["kind"] == STEP_WAIT:
                    has_wait_steps = True
                if step["after"]:
                    has_step_after = True
                if step["kind"] == STEP_ATK and step["chance"] != 100:
                    has_step_chance = True
    return {
        "HAS_STAGGER": has_stagger,
        "HAS_HIT_STAGGER": has_hit_stagger,
        "HAS_WAIT_STEPS": has_wait_steps,
        "HAS_STEP_AFTER": has_step_after,
        "HAS_STEP_CHANCE": has_step_chance,
        "HAS_MULTI_STEP": has_multi_step,
        "HAS_MULTI_WINDOW": has_multi_window,
        "HAS_SIMPLE_GUARDS": simple_guards,
        "HAS_ZONES": has_zones,
        "HAS_ENRAGE": has_enrage,
        "HAS_GUARD_HP": has_guard_hp,
        "HAS_GUARD_PLAYER": has_guard_player,
        "HAS_GUARD_COOLDOWN": has_guard_cooldown,
        "HAS_GUARD_CHANCE": has_guard_chance,
        "HAS_GUARD_ZONES": has_guard_zones,
        "HAS_GUARD_FACING": has_guard_facing,
        "HAS_TURN_RATE": has_turn_rate,
        "HAS_CARVE": has_carve,
    }


def emit_meta_header(model, compiled):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-combat.py -- do not edit.")
    app("//")
    app("// Combat blob ABI: header (magic u16, version u8, flags u8, 11x u16 counts")
    app("// + 3x u16 reserved) then fixed-size record arrays, little-endian, explicit")
    app("// u8/u16, no padding. Offsets are absolute byte offsets into the mhCombat")
    app("// raw_t section (fxdata/fxdata.txt): on AVR the loader reads mhCombat + off.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace combat {")
    app("")
    app("constexpr uint16_t MAGIC = 0x%04X;" % MAGIC)
    app("constexpr uint8_t VERSION = %d;" % VERSION)
    app("constexpr uint8_t FLAGS = 0x%02X;" % FLAGS)
    app("constexpr uint16_t SIZE = %d;" % len(compiled["blob"]))
    app("constexpr uint16_t HEADER_SIZE = %d;" % HEADER_SIZE)
    app("")
    app("// Data facts: true when the shipped blob uses the feature. The interpreter")
    app("// still implements every path; a false fact lets the shipping build drop")
    app("// the unused machinery (constant-folded `if`, no C++17 `if constexpr`).")
    for name, value in sorted(data_facts(model).items()):
        app("constexpr bool %s = %s;" % (name, "true" if value else "false"))
    app("")
    for section in SECTION_ORDER:
        app("constexpr uint16_t %s_OFF = %d;" % (section, compiled["section_off"][section]))
        app("constexpr uint16_t %s_COUNT = %d;" % (section, compiled["section_count"][section]))
    app("")
    for record_name in sorted(SIZES):
        app("constexpr uint8_t %s_SIZE = %d;" % (record_name, SIZES[record_name]))
    app("")
    app("// Carve table (prg.3): a fixed %d-slot array inside the creature record," % CARVE_SLOTS)
    app("// each slot %d B (item index, count 1..3, chance 0..100; count 0 = empty)." % CARVE_SIZE)
    app("constexpr uint8_t CARVE_SLOTS = %d;" % CARVE_SLOTS)
    app("constexpr uint8_t CARVE_ITEM_OFF = %d;" % CARVE_ITEM_OFF)
    app("constexpr uint8_t CARVE_COUNT_OFF = %d;" % CARVE_COUNT_OFF)
    app("constexpr uint8_t CARVE_CHANCE_OFF = %d;" % CARVE_CHANCE_OFF)
    app("constexpr uint8_t CREATURE_CARVE_OFF = %d;" % (SIZES["CREATURE"] - CARVE_SIZE * CARVE_SLOTS))
    app("constexpr uint8_t CREATURE_CORE_SIZE = %d;   // creature bytes before the carve table" % (SIZES["CREATURE"] - CARVE_SIZE * CARVE_SLOTS))
    app("")
    app("// Per-record offsets and indices (creatures sorted by id; attacks, windows,")
    app("// patterns, guards and steps keep source order inside each creature).")
    for name in sorted(compiled["offsets"]):
        app("constexpr uint16_t %s = %d;" % (name, compiled["offsets"][name]))
    for name in sorted(compiled["indices"]):
        app("constexpr uint8_t %s = %d;" % (name, compiled["indices"][name]))
    app("")
    app("}   // namespace combat")
    app("")
    return "\n".join(lines)


def emit_expect_header(model, compiled):
    lines = []
    app = lines.append
    digest = hashlib.sha256(compiled["blob"]).hexdigest()
    app("#pragma once")
    app("// Generated by tools/gen-combat.py -- do not edit.")
    app("//")
    app("// Byte-level expectations for the host pack-parity test and the Ardens")
    app("// loader test: record sizes, pinned spot values and the blob sha256.")
    app("")
    app("#include <stdint.h>")
    app("")
    app("namespace combat_expect {")
    app("")
    app("constexpr uint16_t BLOB_SIZE = %d;" % len(compiled["blob"]))
    for record_name in sorted(SIZES):
        app("constexpr uint8_t %s_SIZE = %d;" % (record_name, SIZES[record_name]))
    app("constexpr uint8_t CARVE_SLOTS = %d;" % CARVE_SLOTS)
    app("constexpr uint8_t CREATURE_CORE_SIZE = %d;" % (SIZES["CREATURE"] - CARVE_SIZE * CARVE_SLOTS))
    app("")
    for creature in model["creatures"]:
        cid = creature["id"].upper()
        stats = creature["stats"]
        app("constexpr uint16_t CREATURE_%s_HP = %d;" % (cid, stats["hp"]))
        app("constexpr uint8_t CREATURE_%s_SPD = %d;" % (cid, stats["spd"]))
        app("constexpr uint8_t CREATURE_%s_W = %d;" % (cid, stats["w"]))
        app("constexpr uint8_t CREATURE_%s_H = %d;" % (cid, stats["h"]))
        app("constexpr uint8_t CREATURE_%s_ATTACKS = %d;" % (cid, len(creature["attacks"])))
        app("constexpr uint8_t CREATURE_%s_PATTERNS = %d;" % (cid, len(creature["patterns"])))
        app("constexpr uint8_t CREATURE_%s_STATIC = %d;" % (cid, creature["static"]))
        app("constexpr uint8_t CREATURE_%s_SHEET = %d;" % (cid, creature["sheet"]))
        # Art descriptor pins (epic monhun-ardu-bih) only for creatures that
        # author one: every other creature packs an all-zero record (legacy draw)
        # and emitting ~9 dead constants each would bloat the device test image.
        art = creature["art"]
        if art["sheet"] != 0:
            app("constexpr uint8_t CREATURE_%s_ART_SHEET = %d;" % (cid, art["sheet"]))
            app("constexpr int8_t CREATURE_%s_ART_ANCHOR_Y = %d;" % (cid, art["anchorY"]))
            app("constexpr uint8_t CREATURE_%s_ART_STRIDE = %d;" % (cid, art["stride"]))
            app("constexpr uint8_t CREATURE_%s_ART_IDLE0 = %d;" % (cid, art["idle0"]))
            app("constexpr uint8_t CREATURE_%s_ART_IDLE_COUNT = %d;" % (cid, art["idleCount"]))
            app("constexpr uint8_t CREATURE_%s_ART_FLASH = %d;" % (cid, art["flash"]))
            app("constexpr uint8_t CREATURE_%s_ART_DEAD = %d;" % (cid, art["dead"]))
        broken_body = creature["brokenBody"] or {"w": 0, "h": 0}
        app("constexpr uint8_t CREATURE_%s_BROKEN_W = %d;" % (cid, broken_body["w"]))
        app("constexpr uint8_t CREATURE_%s_BROKEN_H = %d;" % (cid, broken_body["h"]))
        collide = creature["collide"] or {"ox": 0, "oy": 0, "w": stats["w"], "h": stats["h"]}
        app("constexpr int8_t CREATURE_%s_COLLIDE_OX = %d;" % (cid, collide["ox"]))
        app("constexpr int8_t CREATURE_%s_COLLIDE_OY = %d;" % (cid, collide["oy"]))
        app("constexpr uint8_t CREATURE_%s_COLLIDE_W = %d;" % (cid, collide["w"]))
        app("constexpr uint8_t CREATURE_%s_COLLIDE_H = %d;" % (cid, collide["h"]))
        # Carve table pins (prg.3): one ordinal + item/count/chance per authored
        # slot, so the host and device suites pin the packed drop table.
        carve = creature["carve"]
        app("constexpr uint8_t CREATURE_%s_CARVES = %d;" % (cid, len(carve)))
        for slot, drop in enumerate(carve):
            app("constexpr uint8_t CREATURE_%s_CARVE%d_ITEM = %d;" % (cid, slot, drop["item"]))
            app("constexpr uint8_t CREATURE_%s_CARVE%d_COUNT = %d;" % (cid, slot, drop["count"]))
            app("constexpr uint8_t CREATURE_%s_CARVE%d_CHANCE = %d;" % (cid, slot, drop["chance"]))
        # Enrage pins only for creatures that author the phase: 0 on every
        # shipped creature, so emitting four dead constants per creature only
        # bloats the device test image (feel.6 budget).
        enrage = creature["enrage"]
        if enrage["hpPct"] > 0:
            app("constexpr uint8_t CREATURE_%s_ENRAGE_HP_PCT = %d;" % (cid, enrage["hpPct"]))
            app("constexpr uint8_t CREATURE_%s_ENRAGE_SPD_MUL = %d;" % (cid, enrage["spdMul"]))
            app("constexpr uint8_t CREATURE_%s_ENRAGE_FACE_HOLD = %d;" % (cid, enrage["faceHold"]))
            app("constexpr uint8_t CREATURE_%s_ENRAGE_CUE = %d;" % (cid, enrage["cue"] or 0))
        app("constexpr uint8_t PROFILE_%s_FACE_HOLD = %d;" % (cid, creature["profile"]["faceHold"]))
        app("constexpr uint8_t PROFILE_%s_TURN_RATE = %d;" % (cid, creature["profile"]["turnRate"]))
        if creature["attacks"]:
            first_attack = creature["attacks"][0]
            app("constexpr uint16_t ATTACK_%s_%s_WINDUP = %d;" % (cid, first_attack["id"].upper(), first_attack["windup"]))
            app("constexpr uint16_t ATTACK_%s_%s_ACTIVE = %d;" % (cid, first_attack["id"].upper(), first_attack["active"]))
            app("constexpr uint16_t ATTACK_%s_%s_RECOVER = %d;" % (cid, first_attack["id"].upper(), first_attack["recover"]))
            app("constexpr uint16_t ATTACK_%s_%s_DMG = %d;" % (cid, first_attack["id"].upper(), first_attack["dmg"]))
            app("constexpr uint8_t ATTACK_%s_%s_WALLSTUN = %d;" % (cid, first_attack["id"].upper(), first_attack["wallStun"] or 0))
            app("constexpr uint8_t ATTACK_%s_%s_TELL = %d;" % (cid, first_attack["id"].upper(), first_attack["tell"] or 0))
        # Per-attack whole-body art pins (bih.4) only for attacks that author a
        # sheet: a sheet-less attack packs the zero triple (generic body draw)
        # and emitting dead constants would only bloat the device test image.
        for attack in creature["attacks"]:
            art = attack["art"]
            if art["sheet"] != 0:
                aid = attack["id"].upper()
                app("constexpr uint8_t ATTACK_%s_%s_ART_SHEET = %d;" % (cid, aid, art["sheet"]))
                app("constexpr uint8_t ATTACK_%s_%s_ART_FRAME = %d;" % (cid, aid, art["frame"]))
                app("constexpr uint8_t ATTACK_%s_%s_ART_MODE = %d;" % (cid, aid, art["mode"]))
        if creature["patterns"]:
            first_pattern = creature["patterns"][0]
            guard = first_pattern["guard"]
            app("constexpr uint8_t PATTERN_%s_%s_MIN_DIST = %d;" % (cid, first_pattern["id"].upper(), guard["minDist"]))
            app("constexpr uint8_t PATTERN_%s_%s_MAX_DIST = %d;" % (cid, first_pattern["id"].upper(), guard["maxDist"]))
            app("constexpr uint8_t PATTERN_%s_%s_CHANCE = %d;" % (cid, first_pattern["id"].upper(), guard["chance"]))
        for name in ZONE_NAMES:
            if name in creature["zones"]:
                zone = creature["zones"][name]
                app("constexpr uint8_t ZONE_%s_%s_HP = %d;" % (cid, name.upper(), zone["hp"]))
                app("constexpr uint8_t ZONE_%s_%s_DMG_MUL = %d;" % (cid, name.upper(), zone["dmgMul"]))
                app("constexpr uint8_t ZONE_%s_%s_BODY_SHARE = %d;" % (cid, name.upper(), zone["bodyShare"]))
    app("")
    app("// sha256 of fxdata/tables/combat.bin: %s" % digest)
    app("constexpr uint8_t BLOB_SHA256[32] = {")
    for offset in range(0, 32, 8):
        chunk = hashlib.sha256(compiled["blob"]).digest()[offset:offset + 8]
        app("    " + ", ".join("0x%02X" % byte for byte in chunk) + ",")
    app("};")
    app("")
    app("}   // namespace combat_expect")
    app("")
    return "\n".join(lines)


def dump_model(model, compiled):
    for creature in model["creatures"]:
        cid = creature["id"]
        stats = creature["stats"]
        zones = " ".join("%s D%d HP%d S%d ST%d" % (
            name, z["dmgMul"], z["hp"], z["bodyShare"], z["staggerOnHit"]) for name, z in sorted(creature["zones"].items()))
        collide = "body" if creature["collide"] is None else "box(%d,%d,%d,%d)" % (
            creature["collide"]["ox"], creature["collide"]["oy"], creature["collide"]["w"], creature["collide"]["h"])
        enrage = creature["enrage"]
        profile = creature["profile"]
        print("creature %s (skeleton %s, stats w%d h%d hp%d spd%d, spawn %d,%d, collide %s, enrage hpPct%d spdMul%d faceHold%d cue%d) zones %s" % (
            cid, creature["skeleton"]["id"], stats["w"], stats["h"], stats["hp"], stats["spd"], stats["spawnX"], stats["spawnY"], collide,
            enrage["hpPct"], enrage["spdMul"], enrage["faceHold"], enrage["cue"] or 0, zones or "-"))
        carve = " ".join("item%d x%d @%d%%" % (c["item"], c["count"], c["chance"]) for c in creature["carve"])
        print("  carve: %s" % (carve or "-"))
        art = creature["art"]
        if art["sheet"]:
            print("  art: sheet%d anchorY%d stride%d idle%d+%d windup%d attack%d recover%d flash%d dead%d" % (
                art["sheet"], art["anchorY"], art["stride"], art["idle0"], art["idleCount"],
                art["windup"], art["attack"], art["recover"], art["flash"], art["dead"]))
        print("  profile: engage%d keep%d attack%d circle%d/%d retreat%d/%d stagger%d/%d/%d faceHold%d turnRate%d cd%d+%d spawn%d/%d stun%d" % (
            profile["engageDist"], profile["keepDist"], profile["attackDist"],
            profile["circleNum"], profile["circleDen"], profile["retreatNum"], profile["retreatDen"],
            profile["staggerMax"], profile["staggerDecay"], profile["staggerRecoverT"],
            profile["faceHold"], profile["turnRate"],
            profile["cdBase"], profile["cdJitter"], profile["spawnT"], profile["spawnCd"], profile["stunRecoverT"]))
        for name in ZONE_NAMES:
            if name not in creature["zones"]:
                continue
            zone = creature["zones"][name]
            print("  zone %s: box(%d,%d,%d,%d) dmgMul %d hp %d share %d break 0x%02X stagger %d brokenOverride %d hurtOff %d disable %s" % (
                name, zone["box"]["ox"], zone["box"]["oy"], zone["box"]["w"], zone["box"]["h"],
                zone["dmgMul"], zone["hp"], zone["bodyShare"], zone["breakTypes"], zone["staggerOnHit"],
                zone["brokenDmgMul"], zone["brokenHurtOff"], ",".join(zone["brokenDisable"]) or "-"))
        for attack in creature["attacks"]:
            move = {0: "none", 1: "lunge", 2: "charge", 3: "hop"}[attack["moveType"]]
            tell = {0: "dot", 1: "line", 2: "arc", 3: "ring", 4: "zone"}.get(attack["tell"] or 0, "?")
            detail = ""
            if attack["moveType"] in (1, 2):
                detail = "(%d)" % attack["moveSpeedF"]
            elif attack["moveType"] == 3:
                detail = "(%d,%d)" % (attack["moveDx"], attack["moveDy"])
            print("  attack %s: windup%d active%d recover%d dmg%d move %s%s windows %d wallStun %d tell %s art(sheet%d frame%d mode%s)" % (
                attack["id"], attack["windup"], attack["active"], attack["recover"], attack["dmg"], move, detail, len(attack["windows"]), attack["wallStun"] or 0, tell,
                attack["art"]["sheet"], attack["art"]["frame"],
                {0: "normal", 1: "spin"}.get(attack["art"]["mode"], "?")))
            for i, window in enumerate(attack["windows"]):
                box = window["box"]
                print("    window %d: t[%d,%d] box(%d,%d,%d,%d) dmgMul %d" % (
                    i, window["t0"], window["t1"], box["ox"], box["oy"], box["w"], box["h"], window["dmgMul"]))
        for pattern in creature["patterns"]:
            guard = pattern["guard"]
            facing = {0: "any", 1: "behind", 2: "front"}.get(guard["facing"], "?")
            print("  pattern %s: guard minDist%d maxDist%d hp[%d,%d] player0x%02X cd%d chance%d zonesBroken %s facing %s" % (
                pattern["id"], guard["minDist"], guard["maxDist"], guard["hpLo"], guard["hpHi"],
                guard["playerFlags"], guard["cooldown"], guard["chance"], ",".join(guard["zonesBroken"]) or "-", facing))
            for i, step in enumerate(pattern["steps"]):
                if step["kind"] == STEP_ATK:
                    print("    step %d: ATK %s.%s after%d chance%d" % (i, cid, step["ref"], step["after"], step["chance"]))
                else:
                    print("    step %d: WAIT %d after%d" % (i, step["ref"], step["after"]))


def write_if_changed(path, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    if os.path.isfile(path):
        with open(path, "rb") as handle:
            if handle.read() == data:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
    return True


def run(root, dump):
    errors = Errors()
    model = compile_model(errors, root)
    compiled = pack_model(errors, model) if model is not None and not errors.items else None
    if errors.items:
        for item in errors.items:
            print("gen-combat: error: %s" % item, file=sys.stderr)
        print("gen-combat: FAIL (%d error%s)" % (len(errors.items), "" if len(errors.items) == 1 else "s"), file=sys.stderr)
        return 1
    if dump:
        dump_model(model, compiled)
        return 0
    changed = []
    if write_if_changed(os.path.join(root, BLOB_REL), compiled["blob"]):
        changed.append(BLOB_REL)
    if write_if_changed(os.path.join(root, DATA_HPP_REL), emit_data_header(model, compiled)):
        changed.append(DATA_HPP_REL)
    if write_if_changed(os.path.join(root, META_HPP_REL), emit_meta_header(model, compiled)):
        changed.append(META_HPP_REL)
    if write_if_changed(os.path.join(root, EXPECT_HPP_REL), emit_expect_header(model, compiled)):
        changed.append(EXPECT_HPP_REL)
    digest = hashlib.sha256(compiled["blob"]).hexdigest()
    print("gen-combat: %d creatures, %d attacks, %d windows, %d patterns, %d steps, %d skeletons, %d zones, %d B, sha256 %s" % (
        compiled["section_count"]["CREATURES"], compiled["section_count"]["ATTACKS"],
        compiled["section_count"]["WINDOWS"], compiled["section_count"]["PATTERNS"],
        compiled["section_count"]["STEPS"], compiled["section_count"]["SKELETONS"],
        compiled["section_count"]["ZONES"], len(compiled["blob"]), digest))
    for path in (BLOB_REL, DATA_HPP_REL, META_HPP_REL, EXPECT_HPP_REL):
        print("gen-combat: %s%s" % (path, "" if path in changed else " (unchanged)"))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=None, help="pipeline root holding data/ (default: repository root)")
    parser.add_argument("--dump", action="store_true", help="validate + list the compiled model; write nothing")
    args = parser.parse_args(argv)
    root = os.path.abspath(args.root) if args.root else REPO_ROOT
    return run(root, args.dump)


if __name__ == "__main__":
    sys.exit(main())
