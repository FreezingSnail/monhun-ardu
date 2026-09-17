#!/usr/bin/env python3
"""Compile creature combat JSON into the packed FX blob + generated headers.

    data/skeletons.json + data/creatures/*.json
        -> fxdata/tables/combat.bin          (packed blob; build intermediate)
        -> src/generated/combat_data.hpp     (host plain structs + arrays)
        -> src/generated/combat_meta.hpp     (VERSION, SIZE, per-record offsets)
        -> src/generated/combat_expect.hpp   (record sizes, spot values, sha256)

Schema and reference values live in docs/creature-framework.md (sections 3-8
and 11). Validation is strict: unknown/missing keys, integer-only quantized
fields (floats and bools rejected), ranges, phys/elem enum membership,
descending stage thresholds, windows inside [0, active], guard ordering,
resolvable local refs, unique local ids and u8/u16 section limits.

Blob layout (little-endian, explicit u8/u16, no padding, fixed section order):

    header     32 B  magic u16, version u8, flags u8, 14x u16 section counts
    creature   17 B  skeletonIdx, profileIdx, firstPart, partCount,
                     firstAttack, attackCount, firstPattern, patternCount,
                     w, h, spd, hp u16, spawnX u16, spawnY u16
    profile    22 B  engageDist, keepDist, attackDist, circleNum, circleDen,
                     retreatNum, retreatDen, staggerMax, staggerDecay,
                     partCount (effective), cdBase u16, cdJitter u16, spawnT u16,
                     spawnCd u16, stunRecoverT u16, staggerRecoverT u16
    skeleton    4 B  firstPart, partCount, firstAnchor, anchorCount
    part       18 B  box(ox i8, oy i8, w, h), dmgMul, bodyShare, breakTypes,
                     hurtOn, physSlash, physBlunt, physShot, firstStage,
                     stageCount, firstElem, elemCount, hp u16, flags
    stage      10 B  at, flags, dmgMulOverride, speedMul, stagger, cue,
                     firstDisable, disableCount, firstEnable, enableCount
    anchor      2 B  ox i8, oy i8
    elem        2 B  elem, mul
    ref         1 B  attackIdx
    attack     22 B  moveType, moveSpeedF, moveDx i8, moveDy i8, facing, phys,
                     elem, onHitEffect, onHitPush i8, onHitStun, stagger, cue,
                     firstWindow, windowCount, windup u16, active u16,
                     recover u16, dmg u16
    window     10 B  t0 u16, t1 u16, box(ox i8, oy i8, w, h), dmgMul, flags
    pattern     3 B  firstStep, stepCount, guardIdx
    guard       9 B  minDist, maxDist, hpLo, hpHi, playerFlags, cooldown,
                     chance, firstPartPred, partPredCount
    predicate   3 B  partIdx, op, stage
    step        4 B  kind (0 ATK / 1 WAIT), ref (attackIdx or ticks), after,
                     chance

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
FACINGS = {"track": 0, "lock-at-windup": 1}
ON_HIT_EFFECTS = {"none": 0, "trip": 1, "stun": 2}
CUES = {"none": 0, "windup": 1, "part_break": 2}
PRED_OPS = {">=": 0, "<=": 1, "==": 2}
STEP_ATK = 0
STEP_WAIT = 1

SIZES = {
    "CREATURE": 17,
    "PROFILE": 22,
    "SKELETON": 4,
    "PART": 18,
    "STAGE": 10,
    "ANCHOR": 2,
    "ELEM": 2,
    "REF": 1,
    "ATTACK": 22,
    "WINDOW": 10,
    "PATTERN": 3,
    "GUARD": 9,
    "PREDICATE": 3,
    "STEP": 4,
}
SECTION_RECORD = {
    "CREATURES": "CREATURE",
    "PROFILES": "PROFILE",
    "SKELETONS": "SKELETON",
    "PARTS": "PART",
    "STAGES": "STAGE",
    "ANCHORS": "ANCHOR",
    "ELEMS": "ELEM",
    "REFS": "REF",
    "ATTACKS": "ATTACK",
    "WINDOWS": "WINDOW",
    "PATTERNS": "PATTERN",
    "GUARDS": "GUARD",
    "PREDICATES": "PREDICATE",
    "STEPS": "STEP",
}
SECTION_ORDER = list(SECTION_RECORD)

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


def read_id_list(errors, ctx, obj, key, known):
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


def normalize_mul_map(errors, ctx, value, table, label):
    result = {}
    if value is None:
        return result
    if not isinstance(value, dict):
        errors.add(ctx, "%s: expected an object mapping names to percents" % label)
        return result
    for name in sorted(value):
        if name not in table:
            errors.add(ctx, "%s: unknown name %r (want one of %s)" % (label, name, ", ".join(sorted(table))))
            continue
        mul = value[name]
        if not is_int(mul):
            errors.add(ctx, "%s.%s: expected an integer, got %r" % (label, name, mul))
            continue
        if not 0 <= mul <= 255:
            errors.add(ctx, "%s.%s: out of range 0..255: %d" % (label, name, mul))
            continue
        result[table[name]] = mul
    return result


def normalize_stages(errors, ctx, value, attack_ids):
    if not isinstance(value, list):
        errors.add(ctx, "stages: expected an array")
        return []
    stages = []
    previous_at = None
    for i, obj in enumerate(value):
        c = "%s[%d]" % (ctx, i)
        check_keys(errors, c, obj, {"at"}, {"stagger", "speedMul", "cue", "dmgMulOverride", "hurtOn", "disableAttacks", "enableAttacks"})
        at = read_int(errors, c, obj, "at", 0, 100)
        if at is not None:
            if previous_at is not None and at >= previous_at:
                errors.add(c, "at: stage thresholds must be strictly descending (got %d after %d)" % (at, previous_at))
            previous_at = at
        hurt_on = None
        if "hurtOn" in obj:
            hurt_on = read_bool(errors, c, obj, "hurtOn")
        stage = {
            "at": at if at is not None else 0,
            "stagger": read_int(errors, c, obj, "stagger", 0, 255, default=0),
            "speedMul": read_int(errors, c, obj, "speedMul", 0, 255, default=0),
            "dmgMulOverride": read_int(errors, c, obj, "dmgMulOverride", 0, 255, default=100),
            "cue": read_enum(errors, c, obj, "cue", CUES, default=0),
            "disable": read_id_list(errors, c, obj, "disableAttacks", attack_ids),
            "enable": read_id_list(errors, c, obj, "enableAttacks", attack_ids),
            "hurtOn": hurt_on,
            "flags": (0x01 if hurt_on is not None else 0) | (0x02 if "dmgMulOverride" in obj else 0) | (0x04 if "speedMul" in obj else 0),
        }
        stages.append(stage)
    return stages


def normalize_part(errors, ctx, obj, attack_ids):
    check_keys(errors, ctx, obj, {"id", "box", "dmgMul", "hp", "bodyShare", "breakTypes", "hurtOn", "stages"}, {"physMul", "elemMul"})
    return {
        "id": read_id(errors, ctx, obj, "id"),
        "box": normalize_box(errors, ctx + ".box", obj.get("box")),
        "dmgMul": read_int(errors, ctx, obj, "dmgMul", 0, 255),
        "hp": read_int(errors, ctx, obj, "hp", 0, 65535),
        "bodyShare": read_int(errors, ctx, obj, "bodyShare", 0, 255),
        "breakTypes": normalize_break_types(errors, ctx, obj.get("breakTypes")),
        "hurtOn": read_bool(errors, ctx, obj, "hurtOn"),
        "stages": normalize_stages(errors, ctx + ".stages", obj.get("stages"), attack_ids),
        "phys": normalize_mul_map(errors, ctx, obj.get("physMul"), PHYS, "physMul"),
        "elems": normalize_mul_map(errors, ctx, obj.get("elemMul"), ELEMS, "elemMul"),
    }


def normalize_attack(errors, ctx, obj):
    check_keys(errors, ctx, obj, {"id", "windup", "active", "recover", "dmg", "phys", "elem", "move", "facing", "windows"}, {"onHit", "stagger", "cue"})
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
        "windows": windows,
    }


def normalize_guard(errors, ctx, obj, part_lookup):
    guard = {"minDist": 0, "maxDist": 255, "hpLo": 0, "hpHi": 100, "playerFlags": 0, "cooldown": 0, "chance": 100, "parts": []}
    if obj is None:
        return guard
    check_keys(errors, ctx, obj, set(), {"minDist", "maxDist", "hpBand", "parts", "player", "cooldown", "chance"})
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
    if "parts" in obj:
        parts = obj["parts"]
        if not isinstance(parts, dict):
            errors.add(ctx, "parts: expected an object of part-id predicates")
        else:
            for part_id in sorted(parts):
                value = parts[part_id]
                if not isinstance(part_id, str) or part_id not in part_lookup:
                    errors.add(ctx, "parts: unknown part id %r" % part_id)
                    continue
                if not isinstance(value, str):
                    errors.add(ctx, "parts.%s: expected a comparison string like '>=1'" % part_id)
                    continue
                match = re.match(r"^\s*(>=|<=|==)\s*(\d+)\s*$", value)
                if not match:
                    errors.add(ctx, "parts.%s: malformed comparison %r (want '>=N', '<=N' or '==N')" % (part_id, value))
                    continue
                stage = int(match.group(2))
                if stage > 255:
                    errors.add(ctx, "parts.%s: stage limit %d > 255" % (part_id, stage))
                    continue
                guard["parts"].append((part_lookup[part_id], PRED_OPS[match.group(1)], stage))
    player = obj.get("player")
    if player is not None:
        check_keys(errors, ctx + ".player", player, set(), {"attacking"})
        if "attacking" in player:
            attacking = read_bool(errors, ctx + ".player", player, "attacking")
            if attacking:
                guard["playerFlags"] |= 0x01
    guard["cooldown"] = read_int(errors, ctx, obj, "cooldown", 0, 255, default=0)
    guard["chance"] = read_int(errors, ctx, obj, "chance", 0, 100, default=100)
    return guard


def normalize_pattern(errors, ctx, obj, attack_ids, part_lookup):
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
        "guard": normalize_guard(errors, ctx + ".guard", obj.get("guard"), part_lookup),
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


PROFILE_REQUIRED = {"engageDist", "keepDist", "attackDist", "circleNum", "circleDen", "retreatNum", "retreatDen", "cdBase", "cdJitter", "spawnT", "spawnCd", "stunRecoverT"}
PROFILE_OPTIONAL = {"staggerMax", "staggerDecay", "staggerRecoverT"}


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
        check_keys(errors, ctx, obj, {"id", "parts", "anchors"})
        sid = read_id(errors, ctx, obj, "id", skeleton_ids)
        raw_parts = obj.get("parts")
        if not isinstance(raw_parts, list) or not raw_parts:
            errors.add(ctx, "parts: expected a non-empty array")
            raw_parts = []
        part_ids = set()
        parts = []
        for j, part in enumerate(raw_parts):
            pc = "%s.parts[%d]" % (ctx, j)
            part = normalize_part(errors, pc, part, set())
            if part["id"] is not None:
                if part["id"] in part_ids:
                    errors.add(pc, "duplicate local id '%s'" % part["id"])
                part_ids.add(part["id"])
            parts.append(part)
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
            skeletons.append({"id": sid, "parts": parts, "anchors": anchors, "partIds": part_ids})
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
    creatures = []
    creature_ids = set()
    for name in creature_files:
        path = os.path.join(creatures_dir, name)
        ctx = "%s/%s" % (CREATURES_REL, name)
        obj = load_json(errors, path)
        if obj is None:
            continue
        check_keys(errors, ctx, obj, {"id", "skeleton", "stats", "profile", "attacks", "patterns"}, {"parts"})
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
        check_keys(errors, ctx + ".stats", stats, {"w", "h", "hp", "spd", "spawnX", "spawnY"})
        profile = normalize_profile(errors, ctx + ".profile", obj.get("profile"))
        raw_attacks = obj.get("attacks")
        if not isinstance(raw_attacks, list) or not raw_attacks:
            errors.add(ctx, "attacks: expected a non-empty array")
            raw_attacks = []
        attacks = []
        attack_ids = set()
        for i, attack in enumerate(raw_attacks):
            ac = "%s.attacks[%d]" % (ctx, i)
            attack = normalize_attack(errors, ac, attack)
            if attack["id"] is not None:
                if attack["id"] in attack_ids:
                    errors.add(ac, "duplicate local id '%s'" % attack["id"])
                attack_ids.add(attack["id"])
            attacks.append(attack)
        raw_parts = obj.get("parts", [])
        if not isinstance(raw_parts, list):
            errors.add(ctx, "parts: expected an array")
            raw_parts = []
        overrides = []
        override_ids = set()
        for i, part in enumerate(raw_parts):
            pc = "%s.parts[%d]" % (ctx, i)
            part = normalize_part(errors, pc, part, attack_ids)
            if part["id"] is not None:
                if part["id"] in override_ids:
                    errors.add(pc, "duplicate local id '%s'" % part["id"])
                if part["id"] in skeleton["partIds"]:
                    errors.add(pc, "part override id '%s' collides with skeleton part" % part["id"])
                override_ids.add(part["id"])
            overrides.append(part)
        part_lookup = {}
        for part in skeleton["parts"]:
            if part["id"] is not None:
                part_lookup[part["id"]] = ("skeleton", skeleton["id"], part["id"])
        for part in overrides:
            if part["id"] is not None:
                part_lookup[part["id"]] = ("creature", cid, part["id"])
        raw_patterns = obj.get("patterns")
        if not isinstance(raw_patterns, list) or not raw_patterns:
            errors.add(ctx, "patterns: expected a non-empty array")
            raw_patterns = []
        patterns = []
        pattern_ids = set()
        for i, pattern in enumerate(raw_patterns):
            pc = "%s.patterns[%d]" % (ctx, i)
            pattern = normalize_pattern(errors, pc, pattern, attack_ids, part_lookup)
            if pattern["id"] is not None:
                if pattern["id"] in pattern_ids:
                    errors.add(pc, "duplicate local id '%s'" % pattern["id"])
                pattern_ids.add(pattern["id"])
            patterns.append(pattern)
        creatures.append({
            "id": cid,
            "skeleton": skeleton,
            "stats": {
                "w": read_int(errors, ctx + ".stats", stats, "w", 1, 255),
                "h": read_int(errors, ctx + ".stats", stats, "h", 1, 255),
                "hp": read_int(errors, ctx + ".stats", stats, "hp", 0, 65535),
                "spd": read_int(errors, ctx + ".stats", stats, "spd", 0, 255),
                "spawnX": read_int(errors, ctx + ".stats", stats, "spawnX", 0, 65535),
                "spawnY": read_int(errors, ctx + ".stats", stats, "spawnY", 0, 65535),
            },
            "profile": profile,
            "attacks": attacks,
            "parts": overrides,
            "patterns": patterns,
            "effectiveParts": len(skeleton["parts"]) + len(overrides),
        })
    if errors.items:
        return None
    creatures.sort(key=lambda creature: creature["id"])
    return {"skeletons": skeletons, "creatures": creatures}


def lookup_part(model, key):
    owner_kind, owner_id, part_id = key
    if owner_kind == "skeleton":
        owner = next(s for s in model["skeletons"] if s["id"] == owner_id)
    else:
        owner = next(c for c in model["creatures"] if c["id"] == owner_id)
    return next(p for p in owner["parts"] if p["id"] == part_id)


def part_phys(part, name):
    return part["phys"].get(PHYS[name], 100)


def u8(value):
    return bytes([value & 0xFF])


def i8(value):
    return bytes([value & 0xFF])


def u16(value):
    return bytes([value & 0xFF, (value >> 8) & 0xFF])


def build_layout(model):
    """Derive every global index, offset-independent, in deterministic order."""
    creatures = model["creatures"]
    skeletons = model["skeletons"]
    layout = {"creatures": [], "skeletons": [], "parts": [], "stages": [], "anchors": [],
              "elems": [], "refs": [], "attacks": [], "windows": [], "patterns": [],
              "guards": [], "predicates": [], "steps": []}

    # global part order: skeleton parts (skeletons sorted, source order), then
    # creature override parts (creatures sorted, source order).
    part_keys = []
    for skeleton in skeletons:
        for part in skeleton["parts"]:
            part_keys.append(("skeleton", skeleton["id"], part["id"]))
    for creature in creatures:
        for part in creature["parts"]:
            part_keys.append(("creature", creature["id"], part["id"]))
    part_index = {key: i for i, key in enumerate(part_keys)}

    attack_keys = [(c["id"], a["id"]) for c in creatures for a in c["attacks"]]
    attack_index = {key: i for i, key in enumerate(attack_keys)}
    window_keys = [(c["id"], a["id"], i) for c in creatures for a in c["attacks"] for i in range(len(a["windows"]))]
    window_index = {key: i for i, key in enumerate(window_keys)}
    pattern_keys = [(c["id"], p["id"]) for c in creatures for p in c["patterns"]]
    pattern_index = {key: i for i, key in enumerate(pattern_keys)}
    step_keys = [(c["id"], p["id"], i) for c in creatures for p in c["patterns"] for i in range(len(p["steps"]))]
    step_index = {key: i for i, key in enumerate(step_keys)}

    stage_index = 0
    elem_index = 0
    ref_index = 0
    predicate_index = 0
    for key in part_keys:
        part = lookup_part(model, key)
        layout["parts"].append({
            "key": key,
            "part": part,
            "first_stage": stage_index,
            "stage_count": len(part["stages"]),
            "first_elem": elem_index,
            "elem_count": len(part["elems"]),
        })
        for i, stage in enumerate(part["stages"]):
            owner_id = key[1]
            first_disable = ref_index
            first_enable = ref_index + len(stage["disable"])
            layout["stages"].append({
                "key": key,
                "index": i,
                "stage": stage,
                "first_disable": first_disable,
                "disable_count": len(stage["disable"]),
                "first_enable": first_enable,
                "enable_count": len(stage["enable"]),
            })
            for attack_id in stage["disable"] + stage["enable"]:
                if key[0] == "skeleton":
                    raise AssertionError("skeleton part stage cannot reference attacks")
                layout["refs"].append(attack_index[(owner_id, attack_id)])
            ref_index += len(stage["disable"]) + len(stage["enable"])
            stage_index += 1
        for elem in sorted(part["elems"]):
            layout["elems"].append((elem, part["elems"][elem]))
            elem_index += 1

    anchor_index = 0
    for skeleton in skeletons:
        skeleton_layout = {
            "skeleton": skeleton,
            "first_part": part_index[("skeleton", skeleton["id"], skeleton["parts"][0]["id"])] if skeleton["parts"] else 0,
            "first_anchor": anchor_index,
        }
        layout["skeletons"].append(skeleton_layout)
        for anchor in skeleton["anchors"]:
            layout["anchors"].append({"skeleton": skeleton, "anchor": anchor})
            anchor_index += 1

    for creature in creatures:
        cid = creature["id"]
        layout["creatures"].append({
            "creature": creature,
            "first_part": part_index[("creature", cid, creature["parts"][0]["id"])] if creature["parts"] else 0,
            "first_attack": attack_index[(cid, creature["attacks"][0]["id"])],
            "first_pattern": pattern_index[(cid, creature["patterns"][0]["id"])],
        })
        for attack in creature["attacks"]:
            layout["attacks"].append({
                "creature": creature,
                "attack": attack,
                "first_window": window_index[(cid, attack["id"], 0)] if attack["windows"] else 0,
            })
            for i, window in enumerate(attack["windows"]):
                layout["windows"].append({"creature": creature, "attack": attack, "window": window, "index": i})
        for pattern in creature["patterns"]:
            layout["patterns"].append({
                "creature": creature,
                "pattern": pattern,
                "first_step": step_index[(cid, pattern["id"], 0)] if pattern["steps"] else 0,
                "guard_idx": pattern_index[(cid, pattern["id"])],
            })
            layout["guards"].append({"creature": creature, "pattern": pattern, "guard": pattern["guard"], "first_pred": predicate_index})
            for i, step in enumerate(pattern["steps"]):
                layout["steps"].append({"creature": creature, "pattern": pattern, "step": step, "index": i})
            for part_key, op, stage in pattern["guard"]["parts"]:
                layout["predicates"].append((part_index[part_key], op, stage))
                predicate_index += 1

    indices = {
        "part": part_index,
        "attack": attack_index,
        "window": window_index,
        "pattern": pattern_index,
        "step": step_index,
    }
    return layout, indices


def pack_model(errors, model):
    layout, index_maps = build_layout(model)
    part_index = index_maps["part"]
    attack_index = index_maps["attack"]
    window_index = index_maps["window"]
    pattern_index = index_maps["pattern"]
    step_index = index_maps["step"]

    counts = {
        "CREATURES": len(layout["creatures"]), "PROFILES": len(layout["creatures"]),
        "SKELETONS": len(layout["skeletons"]), "PARTS": len(layout["parts"]),
        "STAGES": len(layout["stages"]), "ANCHORS": len(layout["anchors"]),
        "ELEMS": len(layout["elems"]), "REFS": len(layout["refs"]),
        "ATTACKS": len(layout["attacks"]), "WINDOWS": len(layout["windows"]),
        "PATTERNS": len(layout["patterns"]), "GUARDS": len(layout["guards"]),
        "PREDICATES": len(layout["predicates"]), "STEPS": len(layout["steps"]),
    }
    for section, count in counts.items():
        if count > 255:
            errors.add("data", "size limit: %d %ss exceed the 255 record limit" % (count, section.lower().rstrip("s")))
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
        record("CREATURE", b"".join([
            u8(model["skeletons"].index(creature["skeleton"])), u8(i),
            u8(entry["first_part"]), u8(len(creature["parts"])),
            u8(entry["first_attack"]), u8(len(creature["attacks"])),
            u8(entry["first_pattern"]), u8(len(creature["patterns"])),
            u8(stats["w"]), u8(stats["h"]), u8(stats["spd"]),
            u16(stats["hp"]), u16(stats["spawnX"]), u16(stats["spawnY"]),
        ]))

    # profiles
    section_off["PROFILES"] = mark("profiles")
    for entry in layout["creatures"]:
        creature = entry["creature"]
        offsets["PROFILE_%s_OFF" % creature["id"].upper()] = mark("profile")
        profile = creature["profile"]
        record("PROFILE", b"".join([
            u8(profile["engageDist"]), u8(profile["keepDist"]), u8(profile["attackDist"]),
            u8(profile["circleNum"]), u8(profile["circleDen"]),
            u8(profile["retreatNum"]), u8(profile["retreatDen"]),
            u8(profile["staggerMax"]), u8(profile["staggerDecay"]),
            u8(creature["effectiveParts"]),
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
            u8(entry["first_part"]), u8(len(skeleton["parts"])),
            u8(entry["first_anchor"]), u8(len(skeleton["anchors"])),
        ]))

    # parts
    section_off["PARTS"] = mark("parts")
    for entry in layout["parts"]:
        key = entry["key"]
        part = entry["part"]
        owner_name = key[1].upper()
        indices["PART_%s_%s" % (owner_name, key[2].upper())] = part_index[key]
        offsets["PART_%s_%s_OFF" % (owner_name, key[2].upper())] = mark("part")
        box = part["box"]
        record("PART", b"".join([
            i8(box["ox"]), i8(box["oy"]), u8(box["w"]), u8(box["h"]),
            u8(part["dmgMul"]), u8(part["bodyShare"]), u8(part["breakTypes"] or 0), u8(part["hurtOn"]),
            u8(part_phys(part, "SLASH")), u8(part_phys(part, "BLUNT")), u8(part_phys(part, "SHOT")),
            u8(entry["first_stage"]), u8(entry["stage_count"]),
            u8(entry["first_elem"]), u8(entry["elem_count"]),
            u16(part["hp"]), u8(0),
        ]))

    # stages
    section_off["STAGES"] = mark("stages")
    for entry in layout["stages"]:
        owner_name = entry["key"][1].upper()
        part_name = entry["key"][2].upper()
        stage = entry["stage"]
        offsets["STAGE_%s_%s_%d_OFF" % (owner_name, part_name, entry["index"])] = mark("stage")
        record("STAGE", b"".join([
            u8(stage["at"]), u8(stage["flags"]), u8(stage["dmgMulOverride"]), u8(stage["speedMul"]),
            u8(stage["stagger"]), u8(stage["cue"]),
            u8(entry["first_disable"]), u8(entry["disable_count"]),
            u8(entry["first_enable"]), u8(entry["enable_count"]),
        ]))

    # anchors
    section_off["ANCHORS"] = mark("anchors")
    for entry in layout["anchors"]:
        skeleton = entry["skeleton"]
        anchor = entry["anchor"]
        offsets["ANCHOR_%s_%s_OFF" % (skeleton["id"].upper(), anchor["id"].upper())] = mark("anchor")
        record("ANCHOR", i8(anchor["ox"]) + i8(anchor["oy"]))

    # elems
    section_off["ELEMS"] = mark("elems")
    for elem, mul in layout["elems"]:
        record("ELEM", u8(elem) + u8(mul))

    # refs
    section_off["REFS"] = mark("refs")
    for attack_ref in layout["refs"]:
        record("REF", u8(attack_ref))

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
        record("ATTACK", b"".join([
            u8(attack["moveType"]), u8(attack["moveSpeedF"]), i8(attack["moveDx"]), i8(attack["moveDy"]),
            u8(attack["facing"]), u8(attack["phys"] or 0), u8(attack["elem"] or 0),
            u8(attack["onHitEffect"] or 0), i8(attack["onHitPush"] or 0), u8(attack["onHitStun"] or 0),
            u8(attack["stagger"]), u8(attack["cue"] or 0),
            u8(first_window), u8(len(attack["windows"])),
            u16(attack["windup"]), u16(attack["active"]), u16(attack["recover"]), u16(attack["dmg"]),
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
            u8(entry["first_pred"]), u8(len(guard["parts"])),
        ]))

    # predicates
    section_off["PREDICATES"] = mark("predicates")
    for part_ref, op, stage in layout["predicates"]:
        record("PREDICATE", u8(part_ref) + u8(op) + u8(stage))

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

    header = bytearray(u16(MAGIC) + u8(VERSION) + u8(FLAGS))
    for section in SECTION_ORDER:
        header.extend(u16(section_count[section]))
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
    app("// Host-side plain structs and arrays (docs/creature-framework.md section 8).")
    app("// Field order matches the packed FX blob byte order; the host reads members")
    app("// directly, so host struct padding is irrelevant. The device reads the blob")
    app("// with the offsets in combat_meta.hpp instead.")
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
    app("    uint8_t partCount;")
    app("    uint16_t cdBase, cdJitter, spawnT, spawnCd, stunRecoverT, staggerRecoverT;")
    app("};")
    app("")
    app("struct Skeleton {")
    app("    uint8_t firstPart, partCount;")
    app("    uint8_t firstAnchor, anchorCount;")
    app("};")
    app("")
    app("struct Stage {")
    app("    uint8_t at, flags, dmgMulOverride, speedMul, stagger, cue;")
    app("    uint8_t firstDisable, disableCount, firstEnable, enableCount;")
    app("};")
    app("")
    app("struct Elem {")
    app("    uint8_t elem;")
    app("    uint8_t mul;")
    app("};")
    app("")
    app("struct Ref {")
    app("    uint8_t attackIdx;")
    app("};")
    app("")
    app("struct Part {")
    app("    Box box;")
    app("    uint8_t dmgMul, bodyShare, breakTypes, hurtOn;")
    app("    uint8_t physSlash, physBlunt, physShot;")
    app("    uint8_t firstStage, stageCount, firstElem, elemCount;")
    app("    uint16_t hp;")
    app("    uint8_t flags;")
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
    app("    uint8_t firstWindow, windowCount;")
    app("    uint16_t windup, active, recover, dmg;")
    app("};")
    app("")
    app("struct Pattern {")
    app("    uint8_t firstStep, stepCount, guardIdx;")
    app("};")
    app("")
    app("struct Guard {")
    app("    uint8_t minDist, maxDist, hpLo, hpHi, playerFlags, cooldown, chance;")
    app("    uint8_t firstPartPred, partPredCount;")
    app("};")
    app("")
    app("struct Predicate {")
    app("    uint8_t partIdx, op, stage;")
    app("};")
    app("")
    app("struct Step {")
    app("    uint8_t kind, ref, after, chance;")
    app("};")
    app("")
    app("struct Creature {")
    app("    uint8_t skeletonIdx, profileIdx;")
    app("    uint8_t firstPart, partCount;")
    app("    uint8_t firstAttack, attackCount;")
    app("    uint8_t firstPattern, patternCount;")
    app("    uint8_t w, h, spd;")
    app("    uint16_t hp, spawnX, spawnY;")
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
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    model["skeletons"].index(creature["skeleton"]), compiled["indices"]["CREATURE_%s" % creature["id"].upper()],
                    entry["first_part"], len(creature["parts"]), entry["first_attack"], len(creature["attacks"]),
                    entry["first_pattern"], len(creature["patterns"]),
                    stats["w"], stats["h"], stats["spd"], stats["hp"], stats["spawnX"], stats["spawnY"]))
        elif section == "PROFILES":
            for entry in layout["creatures"]:
                creature = entry["creature"]
                profile = creature["profile"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    profile["engageDist"], profile["keepDist"], profile["attackDist"],
                    profile["circleNum"], profile["circleDen"], profile["retreatNum"], profile["retreatDen"],
                    profile["staggerMax"], profile["staggerDecay"], creature["effectiveParts"],
                    profile["cdBase"], profile["cdJitter"], profile["spawnT"], profile["spawnCd"],
                    profile["stunRecoverT"], profile["staggerRecoverT"]))
        elif section == "SKELETONS":
            for entry in layout["skeletons"]:
                skeleton = entry["skeleton"]
                app("    {%d, %d, %d, %d}," % (entry["first_part"], len(skeleton["parts"]),
                                               entry["first_anchor"], len(skeleton["anchors"])))
        elif section == "PARTS":
            for entry in layout["parts"]:
                key = entry["key"]
                part = entry["part"]
                app("    {{%d, %d, %d, %d}, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    part["box"]["ox"], part["box"]["oy"], part["box"]["w"], part["box"]["h"],
                    part["dmgMul"], part["bodyShare"], part["breakTypes"] or 0, part["hurtOn"],
                    part_phys(part, "SLASH"), part_phys(part, "BLUNT"), part_phys(part, "SHOT"),
                    entry["first_stage"], entry["stage_count"], entry["first_elem"], entry["elem_count"],
                    part["hp"], 0))
        elif section == "STAGES":
            for entry in layout["stages"]:
                stage = entry["stage"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    stage["at"], stage["flags"], stage["dmgMulOverride"], stage["speedMul"],
                    stage["stagger"], stage["cue"], entry["first_disable"], entry["disable_count"],
                    entry["first_enable"], entry["enable_count"]))
        elif section == "ANCHORS":
            for entry in layout["anchors"]:
                app("    {%d, %d}," % (entry["anchor"]["ox"], entry["anchor"]["oy"]))
        elif section == "ELEMS":
            for elem, mul in layout["elems"]:
                app("    {%d, %d}," % (elem, mul))
        elif section == "REFS":
            for attack_ref in layout["refs"]:
                app("    {%d}," % attack_ref)
        elif section == "ATTACKS":
            for entry in layout["attacks"]:
                attack = entry["attack"]
                app("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                    attack["moveType"], attack["moveSpeedF"], attack["moveDx"], attack["moveDy"],
                    attack["facing"], attack["phys"] or 0, attack["elem"] or 0,
                    attack["onHitEffect"] or 0, attack["onHitPush"] or 0, attack["onHitStun"] or 0,
                    attack["stagger"], attack["cue"] or 0, entry["first_window"], len(attack["windows"]),
                    attack["windup"], attack["active"], attack["recover"], attack["dmg"]))
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
                    entry["first_pred"], len(guard["parts"])))
        elif section == "PREDICATES":
            for part_ref, op, stage in layout["predicates"]:
                app("    {%d, %d, %d}," % (part_ref, op, stage))
        elif section == "STEPS":
            for entry in layout["steps"]:
                step = entry["step"]
                ref = 0
                if step["kind"] == STEP_ATK:
                    ref = compiled["indices"].get("ATTACK_%s_%s" % (entry["creature"]["id"].upper(), step["ref"].upper()), 0)
                else:
                    ref = step["ref"] if step["ref"] is not None else 0
                app("    {%d, %d, %d, %d}," % (step["kind"], ref, step["after"] or 0, step["chance"] or 100))
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
    has_wait_steps = False
    has_step_after = False
    has_step_chance = False
    has_multi_step = False
    has_multi_window = False
    simple_guards = True
    for creature in model["creatures"]:
        for attack in creature["attacks"]:
            if len(attack["windows"]) > 1:
                has_multi_window = True
        for pattern in creature["patterns"]:
            guard = pattern["guard"]
            if guard["hpLo"] != 0 or guard["hpHi"] != 100 or guard["playerFlags"] != 0 \
                    or guard["cooldown"] != 0 or guard["chance"] != 100 or guard["parts"]:
                simple_guards = False
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
        "HAS_WAIT_STEPS": has_wait_steps,
        "HAS_STEP_AFTER": has_step_after,
        "HAS_STEP_CHANCE": has_step_chance,
        "HAS_MULTI_STEP": has_multi_step,
        "HAS_MULTI_WINDOW": has_multi_window,
        "HAS_SIMPLE_GUARDS": simple_guards,
    }


def emit_meta_header(model, compiled):
    lines = []
    app = lines.append
    app("#pragma once")
    app("// Generated by tools/gen-combat.py -- do not edit.")
    app("//")
    app("// Combat blob ABI: header (magic u16, version u8, flags u8, 14x u16 counts)")
    app("// then fixed-size record arrays, little-endian, explicit u8/u16, no padding.")
    app("// Offsets are absolute byte offsets into the mhCombat raw_t section")
    app("// (fxdata/fxdata.txt): on AVR the loader reads mhCombat + <offset>.")
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
    for record in sorted(SIZES):
        app("constexpr uint8_t %s_SIZE = %d;" % (record, SIZES[record]))
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
    for record in sorted(SIZES):
        app("constexpr uint8_t %s_SIZE = %d;" % (record, SIZES[record]))
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
        first_attack = creature["attacks"][0]
        app("constexpr uint16_t ATTACK_%s_%s_WINDUP = %d;" % (cid, first_attack["id"].upper(), first_attack["windup"]))
        app("constexpr uint16_t ATTACK_%s_%s_ACTIVE = %d;" % (cid, first_attack["id"].upper(), first_attack["active"]))
        app("constexpr uint16_t ATTACK_%s_%s_RECOVER = %d;" % (cid, first_attack["id"].upper(), first_attack["recover"]))
        app("constexpr uint16_t ATTACK_%s_%s_DMG = %d;" % (cid, first_attack["id"].upper(), first_attack["dmg"]))
        first_pattern = creature["patterns"][0]
        guard = first_pattern["guard"]
        app("constexpr uint8_t PATTERN_%s_%s_MIN_DIST = %d;" % (cid, first_pattern["id"].upper(), guard["minDist"]))
        app("constexpr uint8_t PATTERN_%s_%s_MAX_DIST = %d;" % (cid, first_pattern["id"].upper(), guard["maxDist"]))
        app("constexpr uint8_t PATTERN_%s_%s_CHANCE = %d;" % (cid, first_pattern["id"].upper(), guard["chance"]))
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
        print("creature %s (skeleton %s, stats w%d h%d hp%d spd%d, spawn %d,%d)" % (
            cid, creature["skeleton"]["id"], stats["w"], stats["h"], stats["hp"], stats["spd"], stats["spawnX"], stats["spawnY"]))
        for attack in creature["attacks"]:
            move = {0: "none", 1: "lunge", 2: "charge", 3: "hop"}[attack["moveType"]]
            detail = ""
            if attack["moveType"] in (1, 2):
                detail = "(%d)" % attack["moveSpeedF"]
            elif attack["moveType"] == 3:
                detail = "(%d,%d)" % (attack["moveDx"], attack["moveDy"])
            print("  attack %s: windup%d active%d recover%d dmg%d move %s%s windows %d" % (
                attack["id"], attack["windup"], attack["active"], attack["recover"], attack["dmg"], move, detail, len(attack["windows"])))
            for i, window in enumerate(attack["windows"]):
                box = window["box"]
                print("    window %d: t[%d,%d] box(%d,%d,%d,%d) dmgMul %d" % (
                    i, window["t0"], window["t1"], box["ox"], box["oy"], box["w"], box["h"], window["dmgMul"]))
        for pattern in creature["patterns"]:
            guard = pattern["guard"]
            print("  pattern %s: guard minDist%d maxDist%d hp[%d,%d] player0x%02X cd%d chance%d" % (
                pattern["id"], guard["minDist"], guard["maxDist"], guard["hpLo"], guard["hpHi"],
                guard["playerFlags"], guard["cooldown"], guard["chance"]))
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
    print("gen-combat: %d creatures, %d attacks, %d windows, %d patterns, %d steps, %d skeletons, %d parts, %d B, sha256 %s" % (
        compiled["section_count"]["CREATURES"], compiled["section_count"]["ATTACKS"],
        compiled["section_count"]["WINDOWS"], compiled["section_count"]["PATTERNS"],
        compiled["section_count"]["STEPS"], compiled["section_count"]["SKELETONS"],
        compiled["section_count"]["PARTS"], len(compiled["blob"]), digest))
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
