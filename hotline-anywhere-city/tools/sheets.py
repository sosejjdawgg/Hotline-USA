"""Sheet schema, preflight and C++ generation for Hotline Anywhere City.

  python tools/sheets.py preflight   # list unfilled cells, broken references, unverified rows
  python tools/sheets.py gen         # preflight, then write src/gen/sheets.h (refuses if not clean)
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEETS = os.path.join(ROOT, "sheets")

# column -> type. "dev_label" columns are for people reading the sheet and are never compiled.
SCHEMA = {
    "hooks":     {"id": "ident", "kind": ["hook", "call", "slot"], "addr": "hex", "conv": "str", "prologue": "hexbytes", "purpose": "str", "evidence": "evidence"},
    "masks":     {"index": "int", "dev_label": "str", "name_id": "int", "perk_id": "int", "icon_frame": "int", "perk": "ref:perks.id", "param": "int", "gta2_effect": "str"},
    "perks":     {"id": "ident", "system": "str", "how": "str", "evidence": "evidence"},
    "damage":    {"site": "site", "kind": "ident", "base": "int", "npc_rule": ["knockdown", "execution", "lethal"], "player_rule": ["lethal", "shieldable"], "evidence": "evidence"},
    "scoring":   {"event": "ident", "points": "int", "label_id": "int", "cash": "int", "notes": "str"},
    "weapons":   {"id": "int", "dev_label": "str", "hm_role": ["gun", "melee", "thrown"], "npc_weight": "int", "npc_ammo": "int", "kill_event": "ref:scoring.event"},
    "npcs":      {"rule": "ident", "occupations": "intlist", "chance_pct": "int", "aggressive_pct": "int", "evidence": "evidence", "notes": "str"},
    "music":     {"key": "ident", "file": "file", "context": ["car_radio", "rampage", "job", "mask_menu", "stage_clear", "phonecall"], "loop": "int"},
    "controls":  {"action": "ident", "keyboard": "str", "mouse": "str", "gamepad": "str", "gta2_bits": "str", "scope": ["on_foot", "in_car", "both", "menu"]},
    "jobs":      {"id": "ident", "name_id": "int", "map": ["wil", "ste", "bil"], "anchor": ["pavement"], "min_from_spawn": "int", "min_separation": "int", "enemies": "int", "enemy_remap": "int", "weapons": "reflist:weapons.id", "track": "ref:music.key", "evidence": "evidence"},
    "hm_assets": {"use": "ident", "source": ["player_atlas", "hm_folder", "localization"], "anim": "int", "frames": "int", "evidence": "evidence"},
}

HM_TEXT_IDS = range(0, 1902)  # ids present in HM's English table (checked against the player's copy at runtime)


def load():
    return {name: json.load(open(os.path.join(SHEETS, name + ".json"), encoding="utf-8")) for name in SCHEMA}


def preflight(data):
    errors, unverified = [], []
    keys = {}
    for name, rows in data.items():
        for col, typ in SCHEMA[name].items():
            if typ == "ident" or col in ("id", "key", "event", "index"):
                keys[f"{name}.{col}"] = {str(r.get(col)) for r in rows}
    for name, rows in data.items():
        cols = SCHEMA[name]
        for i, row in enumerate(rows):
            tag = f"{name}[{row.get(next(iter(cols)), i)}]"
            for extra in set(row) - set(cols):
                errors.append(f"{tag}: column '{extra}' is not in the schema")
            for col, typ in cols.items():
                v = row.get(col, None)
                if v is None or (isinstance(v, str) and v.strip() == ""):
                    errors.append(f"{tag}.{col}: unfilled")
                    continue
                s = str(v)
                if isinstance(typ, list) and s not in typ:
                    errors.append(f"{tag}.{col}: '{s}' not one of {typ}")
                elif typ == "int" and not re.fullmatch(r"-?\d+", s):
                    errors.append(f"{tag}.{col}: '{s}' is not an integer")
                elif typ == "intlist" and not re.fullmatch(r"-?\d+(,-?\d+)*", s):
                    errors.append(f"{tag}.{col}: '{s}' is not an integer list")
                elif typ == "num" and not re.fullmatch(r"-?\d+(\.\d+)?", s):
                    errors.append(f"{tag}.{col}: '{s}' is not a number")
                elif typ == "hex" and not re.fullmatch(r"0x[0-9A-Fa-f]+", s):
                    errors.append(f"{tag}.{col}: '{s}' is not hex")
                elif typ == "hexbytes" and not (s == "-" and row.get("kind") == "slot") and not re.fullmatch(r"([0-9A-F]{2})+", s):
                    errors.append(f"{tag}.{col}: '{s}' is not a byte string")
                elif typ == "site" and not (s == "other" or re.fullmatch(r"0x[0-9A-Fa-f]+", s)):
                    errors.append(f"{tag}.{col}: '{s}' is not a call site")
                elif typ == "ident" and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s):
                    errors.append(f"{tag}.{col}: '{s}' is not an identifier")
                elif typ == "file" and not re.fullmatch(r"[A-Za-z0-9_]+\.ogg", s):
                    errors.append(f"{tag}.{col}: '{s}' is not an .ogg name")
                elif isinstance(typ, str) and typ.startswith("ref:") and s not in keys.get(typ[4:], set()):
                    errors.append(f"{tag}.{col}: '{s}' does not resolve in {typ[4:]}")
                elif isinstance(typ, str) and typ.startswith("reflist:"):
                    for part in s.split(","):
                        if part.strip() not in keys.get(typ[8:], set()):
                            errors.append(f"{tag}.{col}: '{part}' does not resolve in {typ[8:]}")
                elif typ == "evidence" and s.startswith("lab-pending"):
                    unverified.append(f"{tag}: {row.get('how') or row.get('purpose') or row.get('notes') or ''}".strip())
            for col in ("name_id", "perk_id", "label_id"):
                if col in row and str(row[col]).isdigit() and int(row[col]) not in HM_TEXT_IDS:
                    errors.append(f"{tag}.{col}: {row[col]} outside HM's text table")
    # every perk must be used by a mask, every mask index unique and contiguous
    used = {m["perk"] for m in data["masks"]}
    for p in data["perks"]:
        if p["id"] not in used:
            errors.append(f"perks[{p['id']}]: no mask uses it")
    idx = sorted(int(m["index"]) for m in data["masks"])
    if idx != list(range(len(idx))):
        errors.append("masks.index: not 0..n-1")
    return errors, unverified


def cstr(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def gen(data):
    out = ["// Generated by tools/sheets.py from sheets/*.json. Do not edit; change the sheet and regenerate.", "#pragma once", "#include <stdint.h>", ""]
    def enum(name, values):
        out.append(f"enum class {name} : int {{ " + ", ".join(values) + " };")
    enum("Perk", [p["id"] for p in data["perks"]])
    enum("DmgKind", sorted({d["kind"] for d in data["damage"]}))
    enum("NpcRule", ["knockdown", "execution", "lethal"])
    enum("PlayerRule", ["lethal", "shieldable"])
    enum("ScoreEvent", [s["event"] for s in data["scoring"]])
    enum("HmRole", ["gun", "melee", "thrown"])
    enum("MusicCtx", ["car_radio", "rampage", "job", "mask_menu", "stage_clear", "phonecall"])
    enum("Asset", [a["use"] for a in data["hm_assets"]])
    out.append("")
    out.append("struct HookRow { const char* id; uint32_t addr; const char* prologue; };")
    out.append("static const HookRow kHooks[] = {")
    for h in data["hooks"]:
        out.append(f"  {{{cstr(h['id'])}, {h['addr']}, {cstr('' if h['prologue'] == '-' else h['prologue'])}}},")
    out.append("};")
    out.append("struct MaskRow { int index, nameId, perkId, iconFrame; Perk perk; int param; };")
    out.append("static const MaskRow kMasks[] = {")
    for m in data["masks"]:
        out.append(f"  {{{m['index']}, {m['name_id']}, {m['perk_id']}, {m['icon_frame']}, Perk::{m['perk']}, {m['param']}}},  // {m['dev_label']}")
    out.append("};")
    out.append("struct DamageRow { uint32_t site; DmgKind kind; NpcRule npc; PlayerRule player; };")
    out.append("static const DamageRow kDamage[] = {")
    for d in data["damage"]:
        site = "0" if d["site"] == "other" else d["site"]
        out.append(f"  {{{site}, DmgKind::{d['kind']}, NpcRule::{d['npc_rule']}, PlayerRule::{d['player_rule']}}},")
    out.append("};")
    out.append("struct ScoreRow { ScoreEvent ev; int points, labelId, cash; };")
    out.append("static const ScoreRow kScoring[] = {")
    for s in data["scoring"]:
        out.append(f"  {{ScoreEvent::{s['event']}, {s['points']}, {s['label_id']}, {s['cash']}}},")
    out.append("};")
    out.append("struct WeaponRow { int id; HmRole role; int npcWeight, npcAmmo; ScoreEvent killEvent; };")
    out.append("static const WeaponRow kWeapons[] = {")
    for w in data["weapons"]:
        out.append(f"  {{{w['id']}, HmRole::{w['hm_role']}, {w['npc_weight']}, {w['npc_ammo']}, ScoreEvent::{w['kill_event']}}},  // {w['dev_label']}")
    out.append("};")
    out.append("struct NpcArmRow { int occupations[8]; int chancePct, aggressivePct; };")
    out.append("static const NpcArmRow kNpcArming[] = {")
    for n in data["npcs"]:
        occ = [int(x) for x in str(n["occupations"]).split(",")][:8]
        occ += [-99] * (8 - len(occ))
        out.append(f"  {{{{{', '.join(map(str, occ))}}}, {n['chance_pct']}, {n['aggressive_pct']}}},  // {n['rule']}")
    out.append("};")
    out.append("struct MusicRow { const char* key; const char* file; MusicCtx ctx; bool loop; };")
    out.append("static const MusicRow kMusic[] = {")
    for m in data["music"]:
        out.append(f"  {{{cstr(m['key'])}, {cstr(m['file'])}, MusicCtx::{m['context']}, {str(bool(m['loop'])).lower()}}},")
    out.append("};")
    music_idx = {m["key"]: i for i, m in enumerate(data["music"])}
    out.append("struct JobRow { const char* id; int nameId; const char* map; int minFromSpawn, minSeparation, enemies, enemyRemap; int weapons[4]; int track; };")
    out.append("static const JobRow kJobs[] = {")
    for j in data["jobs"]:
        ws = [int(x) for x in str(j["weapons"]).split(",")][:4]
        ws += [-1] * (4 - len(ws))
        out.append(f"  {{{cstr(j['id'])}, {j['name_id']}, {cstr(j['map'])}, {j['min_from_spawn']}, {j['min_separation']}, {j['enemies']}, {j['enemy_remap']}, {{{', '.join(map(str, ws))}}}, {music_idx[j['track']]}}},")
    out.append("};")
    out.append("struct AssetRow { Asset use; int anim, frames; };")
    out.append("static const AssetRow kAssets[] = {")
    for a in data["hm_assets"]:
        out.append(f"  {{Asset::{a['use']}, {a['anim']}, {a['frames']}}},")
    out.append("};")
    out.append("template <class T, int N> constexpr int countof(const T (&)[N]) { return N; }")
    os.makedirs(os.path.join(ROOT, "src", "gen"), exist_ok=True)
    open(os.path.join(ROOT, "src", "gen", "sheets.h"), "w", encoding="utf-8").write("\n".join(out) + "\n")


if __name__ == "__main__":
    data = load()
    errors, unverified = preflight(data)
    rows = sum(len(r) for r in data.values())
    cells = sum(len(r) * len(SCHEMA[n]) for n, r in data.items())
    print(f"preflight: {len(data)} sheets, {rows} rows, {cells} cells; {len(errors)} errors, {len(unverified)} unverified rows")
    for e in errors:
        print("  ERROR", e)
    for u in unverified:
        print("  UNVERIFIED", u)
    if len(sys.argv) > 1 and sys.argv[1] == "gen":
        if errors:
            sys.exit("gen refused: preflight not clean")
        gen(data)
        print("wrote src/gen/sheets.h")
    sys.exit(1 if errors else 0)
