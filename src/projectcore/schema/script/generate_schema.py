#!/usr/bin/env python3
"""Generate the preset config schema YAML files from source code.

Sources of truth:
  - src/libslic3r/PrintConfig.cpp  : key definitions (type/nullable/min/max/mode/enum/default)
  - src/libslic3r/PrintConfig.hpp  : scope (which StaticPrintConfig class owns a key)
  - src/slic3r/GUI/Tab.cpp         : UI layout (preset tab -> page -> option group -> keys)

Outputs (doc/schema/):
  - print.yaml / filament.yaml / printer.yaml : keys reachable in preset tab UI
  - hidden.yaml : keys defined in PrintConfigDef but not shown in any preset tab

Run:  python scripts/schema_gen/generate_schema.py
"""
import os
import re
from collections import OrderedDict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAB_CPP = os.path.join(ROOT, "src", "slic3r", "GUI", "Tab.cpp")
PC_CPP = os.path.join(ROOT, "src", "libslic3r", "PrintConfig.cpp")
PC_HPP = os.path.join(ROOT, "src", "libslic3r", "PrintConfig.hpp")
OUT_DIR = os.path.join(ROOT, "doc", "schema")

TYPE_MAP = OrderedDict([
    ("coString", "string"), ("coStrings", "strings"),
    ("coInt", "int"), ("coInts", "ints"),
    ("coFloat", "float"), ("coFloats", "floats"),
    ("coPercent", "percent"), ("coPercents", "percents"),
    ("coBool", "bool"), ("coBools", "bools"),
    ("coEnum", "enum"), ("coEnums", "enums"),
    ("coPoint", "point"), ("coPoints", "points"),
    ("coPoint3", "point3"), ("coPoints3", "points3"),
    ("coFloatOrPercent", "float_or_percent"), ("coFloatsOrPercents", "floats_or_percents"),
    ("coDateTime", "datetime"),
])

MODE_MAP = {"comSimple": "simple", "comAdvanced": "advanced", "comDevelop": "develop"}

# widget (control) semantics: mirrors ConfigOptionDef::GUIType + UI-side
# inference; orthogonal to the storage `type` and resolved at emit time.
GUI_WIDGET_MAP = {
    "i_enum_open": "combobox_edit",   # editable combo, value may leave the list
    "f_enum_open": "combobox_edit",
    "select_open": "combobox",        # plain Choice, options filled at runtime
    "color": "color",                 # colour picker over a string value
    "one_string": "edit_serialized",  # edits the serialized form (vector as one string)
}

def widget_of(d):
    if d.get("_checkbox_override"):
        # Setting Overrides: a checkbox gates the edit field. checked =
        # override in effect (value non-null); unchecked = null, follow the
        # printer preset's base key
        return "checkbox_override_edit"
    if d.get("_multiline"):
        return "textarea"
    w = GUI_WIDGET_MAP.get(d.get("_gui_type", ""))
    if w:
        return w
    t = d.get("type", "")
    if t in ("enum", "enums") or "values" in d:
        return "combobox"
    if t in ("bool", "bools"):
        return "checkbox"
    if t in ("point", "point3", "points", "points3"):
        return "point"
    return "editline"

# scope priority: first class in this list that declares the key wins
SCOPE_CLASSES = OrderedDict([
    ("PrintObjectConfig", "print_object"),
    ("PrintRegionConfig", "print_region"),
    ("MachineEnvelopeConfig", "machine"),
    ("GCodeConfig", "gcode"),
    ("PrintConfig", "print"),
])
NON_SCOPE_CLASSES = {"SLAPrintConfig", "SLAPrintObjectConfig", "SLAMaterialConfig",
                     "SLAPrinterConfig", "SLAFullPrintConfig", "FullPrintConfig"}

warnings = []


def read_lines(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.readlines()


def find_line(lines, pattern, start=0):
    rx = re.compile(pattern)
    for i in range(start, len(lines)):
        if rx.search(lines[i]):
            return i
    return -1


def to_num(s):
    s = s.strip()
    if re.fullmatch(r"-?[\d.]+[fF]", s):  # strip C++ float literal suffix
        s = s[:-1]
    f = float(s.rstrip("."))
    return int(f) if f == int(f) else f


# --------------------------------------------------------------------------
# Part 1: key definitions from PrintConfig.cpp
# --------------------------------------------------------------------------

def parse_defs():
    lines = read_lines(PC_CPP)
    begin = find_line(lines, r"void PrintConfigDef::init_common_params\(\)")
    end = find_line(lines, r"void PrintConfigDef::init_extruder_option_keys\(\)")
    assert begin >= 0 and end >= 0, "cannot locate PrintConfigDef sections"

    # constexpr string constants used in place of literals (FLOW_MODE_STANDARD)
    cpp_constants = {}
    for hl in read_lines(PC_HPP):
        cm = re.search(r'constexpr\s+const\s+char\*\s*(\w+)\s*=\s*"([^"]*)"', hl)
        if cm:
            cpp_constants[cm.group(1)] = cm.group(2)
    # numeric constants used in min/max bounds (const int max_temp = 1500)
    for cl in lines:
        cm = re.search(r'\bconst\s+(?:int|float|double|long\s+long)\s+(\w+)\s*=\s*(-?[\d.]+)[fF]?\s*;', cl)
        if cm and cm.group(1) not in cpp_constants:
            try:
                cpp_constants[cm.group(1)] = to_num(cm.group(2))
            except ValueError:
                pass

    # enum maps "ui string" <-> symbol: symbol -> ui string, for resolving
    # ConfigOptionEnumsGeneric defaults like { ZHopType::zhtSlope }
    sym_to_str = {}
    in_map = False
    for cl in lines:
        s = cl.strip()
        if "t_config_enum_values" in s and not s.startswith("//"):
            in_map = True
            continue
        if in_map and s.startswith("};"):
            in_map = False
            continue
        if in_map:
            # entry shapes seen in the wild: {"v", Sym}, {"v", int(E::Sym)},
            # {"v", E::Sym}, {"v", Sym},  // trailing comment
            em = re.match(r'\{\s*(?:"([^"]+)"|(\w+))\s*,.*?(?:int\(\s*)?(?:\w+::)*(\w+)\s*\)?\s*\}\s*,?(?:\s*//.*)?$', s)
            if em:
                key = em.group(1) if em.group(1) is not None else cpp_constants.get(em.group(2))
                if key is not None:
                    sym_to_str[em.group(3)] = key

    defs = OrderedDict()
    rx_add = re.compile(r'def\s*=\s*this->add\("([^"]+)",\s*(co\w+)\)')
    rx_null = re.compile(r"def->nullable\s*=\s*true")
    # numeric literal (optional C++ float suffix) or a possibly-negated named
    # constant; the trailing ";" keeps member-chain RHS (it_opt->second.min)
    # from matching.
    rx_min = re.compile(r"def->min\s*=\s*(-?[\d.]+[fF]?|-([A-Za-z_]\w*)|([A-Za-z_]\w*))\s*;")
    rx_max = re.compile(r"def->max\s*=\s*(-?[\d.]+[fF]?|-([A-Za-z_]\w*)|([A-Za-z_]\w*))\s*;")
    rx_mode = re.compile(r"def->mode\s*=\s*(com\w+)")
    rx_ev = re.compile(r'enum_values\.(?:push_back|emplace_back)\(\s*(?:"([^"]+)"|(\w+))\s*\)')
    # enum_labels entries: L("...") for translatable text, bare "..." for
    # unit-like labels ("1 mm") that are deliberately untranslated
    rx_el = re.compile(r'enum_labels\.(?:push_back|emplace_back)\(\s*(?:_?L\()?"((?:[^"\\]|\\.)*)"\)?\s*\)')
    # display-layer fields collected as _<field> intermediates (emitted with
    # translations into the i18n files only, never into the main YAMLs)
    rx_disp = re.compile(r'def->(label|full_label|tooltip|sidetext|category)\s*=\s*')
    rx_strlit = re.compile(r'"((?:[^"\\]|\\.)*)"')
    rx_defv = re.compile(r"set_default_value\(\s*new\s+ConfigOption(\w+)")
    rx_gtype = re.compile(r"gui_type\s*=\s*ConfigOptionDef::GUIType::(\w+)")
    rx_multiline = re.compile(r"def->multiline\s*=\s*true")
    rx_readonly = re.compile(r"def->readonly\s*=\s*true")

    def balanced_content(s):
        """content of the first balanced (...) or {...} group; quote-aware"""
        depth, in_str, esc, start = 0, False, False, None
        for idx, ch in enumerate(s):
            if in_str:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
                continue
            if ch == '"':
                in_str = True
            elif ch in "({":
                if depth == 0:
                    start = idx + 1
                depth += 1
            elif ch in ")}":
                depth -= 1
                if depth == 0:
                    return s[start:idx], True
        return None, False

    def split_args(s):
        """split a C++ initializer body on top-level commas (quote-aware)"""
        parts, buf, depth, in_str, esc = [], [], 0, False, False
        for ch in s:
            if in_str:
                buf.append(ch)
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    in_str = False
                continue
            if ch == '"':
                in_str = True
            elif ch in "({":
                depth += 1
            elif ch in ")}":
                depth -= 1
            elif ch == "," and depth == 0:
                parts.append("".join(buf))
                buf = []
                continue
            buf.append(ch)
        if "".join(buf).strip():
            parts.append("".join(buf))
        return parts

    def unescape(s):
        return (s.replace("\\\\", "\x00").replace('\\"', '"').replace("\\n", "\n")
                 .replace("\\t", "\t").replace("\\r", "\r").replace("\x00", "\\"))

    def quoted_strs(s):
        """one string per top-level comma segment; adjacent literals concatenated"""
        out = []
        for part in split_args(s):
            segs = re.findall(r'"((?:[^"\\]|\\.)*)"', part)
            if segs:
                out.append(unescape("".join(segs)))
        return out

    def parse_default(kind, arg, enum_values):
        arg = arg.strip()
        if arg.startswith("{") and arg.endswith("}"):
            arg = arg[1:-1].strip()
        if kind == "Bool":
            if arg in ("true", "1"):
                return True
            if arg in ("false", "0"):
                return False
            return None
        if kind == "FloatOrPercent":
            m = re.match(r"^(-?[\d.]+)\s*,\s*(true|false)$", arg)
            if not m:
                return None
            v = to_num(m.group(1))
            return ("%g%%" % v) if m.group(2) == "true" else v
        if kind in ("Float", "Int", "Percent"):
            if not arg:
                return None
            try:
                return to_num(arg)
            except ValueError:
                return None
        if kind in ("Floats", "Ints", "Percents", "Bools"):
            vals = []
            for p in split_args(arg):
                p = p.strip()
                if not p:
                    continue
                if kind == "Bools":
                    if p in ("true", "1"):
                        vals.append(True)
                    elif p in ("false", "0"):
                        vals.append(False)
                    else:
                        vals.append(None)
                else:
                    try:
                        vals.append(to_num(p))
                    except ValueError:
                        vals.append(None)
            return vals if vals else ([] if not arg else None)
        if kind == "FloatsOrPercents":
            vals = []
            for e in re.findall(r"FloatOrPercent\s*\{([^{}]*)\}", arg):
                m = re.match(r"^\s*(-?[\d.]+)\s*,\s*(true|false)\s*$", e)
                if m:
                    v = to_num(m.group(1))
                    vals.append(("%g%%" % v) if m.group(2) == "true" else v)
            return vals or None
        if kind in ("String", "Strings"):
            segs = quoted_strs(arg)
            if not segs and arg and re.fullmatch(r"\w+", arg):
                # constant used in place of a literal, e.g. FLOW_MODE_STANDARD
                v = cpp_constants.get(arg)
                if v is not None:
                    segs = [v]
            if kind == "String":
                return segs[0] if segs else ("" if not arg else None)
            return segs if segs else ([] if not arg else None)
        if kind in ("Point", "Point3"):
            vec = "Vec2d" if kind == "Point" else "Vec3d"
            m = re.search(vec + r"\(([^()]*)\)", arg)
            if not m:
                return None
            try:
                return [to_num(t) for t in m.group(1).split(",")]
            except ValueError:
                return None
        if kind in ("Points", "Points3"):
            vec = "Vec2d" if kind == "Points" else "Vec3d"
            out = []
            for pm in re.finditer(vec + r"\(([^()]*)\)", arg):
                try:
                    out.append([to_num(t) for t in pm.group(1).split(",")])
                except ValueError:
                    pass
            return out if out else ([] if not arg else None)
        if kind == "Enum":
            if not arg:
                return None
            sym = arg.strip()
            # resolve through the enum keys map first: handles qualified
            # symbols (PerimeterGeneratorType::Arachne) and prefixed ones
            # (gcfMarlinLegacy) that the lowercase heuristic misses
            base = sym.split("::")[-1]
            for cand in (sym, base):
                if cand in sym_to_str:
                    return sym_to_str[cand]
            low = base.lower()
            for cand in (low, low[2:], low[3:]):
                if cand in enum_values:
                    return cand
            warnings.append("unresolved Enum default: %s" % sym)
            return sym  # keep raw symbol when heuristic fails
        if kind == "EnumsGeneric":
            # vector-of-int storage; resolve the symbol through the enum maps
            sym = re.sub(r"^\(int\)", "", arg).strip()
            sym = sym.split("::")[-1].strip()
            if sym in sym_to_str:
                return [sym_to_str[sym]]
            for cand in (sym, sym.lower(), sym.lower()[3:] if len(sym) > 3 else sym):
                if cand in enum_values:
                    return [cand]
            if sym:
                warnings.append("unresolved EnumsGeneric default: %s" % sym)
                return [sym]
            return None
        return None  # exotic initializers

    def _bound_value(m, line_no):
        """resolve an rx_min/rx_max match: numeric literal or named constant"""
        tok = m.group(1)
        if re.fullmatch(r"-?[\d.]+[fF]?", tok):
            return to_num(tok)
        name = m.group(2) or m.group(3)  # group(2) = negated constant name
        if name in cpp_constants and isinstance(cpp_constants[name], (int, float)):
            v = cpp_constants[name]
            return -v if tok.startswith("-") else v
        warnings.append("unresolved min/max constant: %s (PrintConfig.cpp:%d)" % (tok, line_no))
        return None

    defv = None
    enum_values = []
    loop_keys = []       # [(key, axis_name)] expanded from a for-axis add
    axes_data = {}       # axis name -> {max_feedrate/max_acceleration/max_jerk: [...]}
    alias_map = {}       # local var bound by `auto var = def = this->add("key",...)`
    override_var = None  # loop var of the add_nullable("filament_"+key) expansion
    override_keys = []   # base keys listed in that loop's braced initializer
    pending_overrides = []  # [(key, base)] awaiting the trailing mode strcmp list
    simple_overrides = set()
    i = begin
    while i < end:
        line = lines[i]
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("#"):
            i += 1
            continue
        # AxisDefault table driving per-axis machine limit definitions
        m = re.match(r'\{\s*"(\w)"\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*\}', stripped)
        if m:
            def _nums(s):
                try:
                    return [to_num(t) for t in s.split(",") if t.strip()]
                except ValueError:
                    return None
            axes_data[m.group(1)] = {"max_feedrate": _nums(m.group(2)),
                                     "max_acceleration": _nums(m.group(3)),
                                     "max_jerk": _nums(m.group(4))}
            i += 1
            continue
        m = rx_add.search(line)
        if m:
            if defv is not None and enum_values:
                defv["values"] = enum_values
            key, ctype = m.group(1), m.group(2)
            defv = defs.setdefault(key, OrderedDict())
            # `auto alias = def = this->add("key", ...)`: remember the local
            # alias so later cross-def copies can resolve it
            am = re.search(r'(\w+)\s*=\s*def\s*=\s*this->add\(', line)
            if am:
                alias_map[am.group(1)] = key
            defv["type"] = TYPE_MAP.get(ctype, ctype)
            if ctype not in TYPE_MAP:
                warnings.append("unknown coType %s for key %s (PrintConfig.cpp:%d)"
                                % (ctype, key, i + 1))
            enum_values = []
            loop_keys = []
            i += 1
            continue
        # per-axis definition inside the AxisDefault loop:
        #   def = this->add("machine_max_speed_" + axis.name, coFloats);
        m = re.search(r'this->add\("([^"]+)"\s*\+\s*axis\.name,\s*(co\w+)\)', line)
        if m:
            prefix, ctype = m.group(1), m.group(2)
            loop_keys = []
            for axis in axes_data:
                key = prefix + axis
                d = defs.setdefault(key, OrderedDict())
                d["type"] = TYPE_MAP.get(ctype, ctype)
                loop_keys.append((key, axis))
            defv = None
            enum_values = []
            i += 1
            continue
        # `for (const char *opt_key : { "a", "b", ... })` whose body registers
        # add_nullable("filament_" + opt_key, ...): capture the key list here,
        # expand it when the add_nullable line shows up below. Commented-out
        # entries (// or /* */) inside the list are dropped.
        m = re.match(r'for\s*\(\s*const\s+char\s*\*\s*(\w+)\s*:\s*\{', stripped)
        if m:
            text, j = re.sub(r'//.*$', '', stripped), i
            while "})" not in lines[j] and j + 1 < end:
                j += 1
                text += " " + re.sub(r'//.*$', '', lines[j].strip())
            text = re.sub(r'/\*.*?\*/', ' ', text)
            override_var = m.group(1)
            override_keys = re.findall(r'"([^"]*)"', text)
            i = j + 1
            continue
        # dynamically-named add: this->add(project_schema.config_key, ...) or
        # this->add_nullable(std::string("filament_") + opt_key, ...) -- the
        # key is not a literal, so the attribute lines that follow must not be
        # attributed to the previous def (project_schema_version is covered by
        # MANUAL_KEYS; the add_nullable loop is expanded below)
        if re.search(r'this->add(?:_nullable)?\(\s*[^"\s]', line):
            if defv is not None and enum_values:
                defv["values"] = enum_values
            # add_nullable("filament_" + opt_key, base->type): expand into one
            # def per listed base key, copying type/min/max/values/labels from
            # the already-parsed base def (what the runtime loop copies
            # field-by-field through it_opt->second)
            m = re.search(r'add_nullable\(\s*std::string\("([^"]+)"\)\s*\+\s*(\w+)', line)
            if m and m.group(2) == override_var and override_keys:
                prefix = m.group(1)
                pending_overrides = []
                for base in override_keys:
                    b = defs.get(base)
                    if b is None:
                        warnings.append("add_nullable base key not parsed: %s (PrintConfig.cpp:%d)"
                                        % (base, i + 1))
                        continue
                    key = prefix + base
                    d = defs.setdefault(key, OrderedDict())
                    for prop in ("type", "min", "max", "values",
                                 "_label", "_full_label", "_tooltip",
                                 "_sidetext", "_enum_labels"):
                        if prop in b:
                            d[prop] = list(b[prop]) if isinstance(b[prop], list) else b[prop]
                    # schema semantics: null = "no override" (follow the
                    # printer preset's base key); the runtime-copied default
                    # is only a technical fallback, see README
                    d["nullable"] = True
                    d["default"] = [None]
                    d["_checkbox_override"] = True
                    pending_overrides.append((key, base))
            defv = None
            loop_keys = []
            enum_values = []
            i += 1
            continue
        # inside the add_nullable loop body: harvest the strcmp simple-mode
        # list and settle every pending key at the trailing else->comAdvanced;
        # also keeps the it_opt->second copy lines from touching anything
        if pending_overrides:
            m = re.search(r'strcmp\(\s*\w+\s*,\s*"([^"]+)"\)', line)
            if m:
                simple_overrides.add(m.group(1))
            if re.search(r'def->mode\s*=\s*comAdvanced', line):
                for key, base in pending_overrides:
                    defs[key]["mode"] = "simple" if base in simple_overrides else "advanced"
                pending_overrides = []
            i += 1
            continue
        if defv is None and not loop_keys:
            i += 1
            continue

        targets = [defv] if defv is not None else [defs[k] for k, _a in loop_keys]
        if rx_null.search(line):
            for t in targets:
                t["nullable"] = True
        m = rx_min.search(line)
        if m:
            v = _bound_value(m, i + 1)
            if v is not None:
                for t in targets:
                    t["min"] = v
        m = rx_max.search(line)
        if m:
            v = _bound_value(m, i + 1)
            if v is not None:
                for t in targets:
                    t["max"] = v
        m = rx_mode.search(line)
        if m:
            if m.group(1) in MODE_MAP:
                for t in targets:
                    t["mode"] = MODE_MAP[m.group(1)]
            else:
                warnings.append("unknown mode %s (PrintConfig.cpp:%d)" % (m.group(1), i + 1))
        m = rx_ev.search(line)
        if m:
            v = m.group(1) if m.group(1) is not None else cpp_constants.get(m.group(2))
            if v is None:
                warnings.append("unresolved enum_values constant: %s" % m.group(2))
            else:
                enum_values.append(v)
        # display-layer field: def->label/full_label/tooltip/sidetext/category
        # = L("...") or a bare "..." (unit symbols), possibly spanning lines
        # through adjacent string-literal concatenation. boost::format RHS
        # (machine limits full_label/tooltip, assembled with the axis name at
        # runtime) is skipped: it has no static text.
        m = rx_disp.search(line)
        if m:
            seg = line[m.end():]
            j = i
            while ";" not in rx_strlit.sub("", seg) and j + 1 < end:
                j += 1
                seg += lines[j]
            if not re.match(r'\s*\(boost::format', seg):
                text = unescape("".join(rx_strlit.findall(seg)))
                if text:
                    for t in targets:
                        t["_" + m.group(1)] = text
            i = j + 1
            continue
        m = rx_el.search(line)
        if m:
            for t in targets:
                t.setdefault("_enum_labels", []).append(m.group(1))
        m = rx_gtype.search(line)
        if m:
            for t in targets:
                t["_gui_type"] = m.group(1)
        # cross-def copies through a bound alias, e.g.
        #   def->enum_values = def_top_fill_pattern->enum_values;
        #   append(def->enum_values, support_interface_top_layers->enum_values);
        #   def->gui_type    = def_infill_anchor_min->gui_type;
        m = re.match(r'def->enum_values\s*=\s*(\w+)->enum_values\s*;', stripped)
        if m and m.group(1) in alias_map:
            src = defs.get(alias_map[m.group(1)], {})
            enum_values = list(src.get("values") or [])
        m = re.match(r'append\(\s*def->enum_values\s*,\s*(\w+)->enum_values\s*\)', stripped)
        if m and m.group(1) in alias_map:
            src = defs.get(alias_map[m.group(1)], {})
            enum_values = list(enum_values) + list(src.get("values") or [])
        m = re.match(r'def->enum_labels\s*=\s*(\w+)->enum_labels\s*;', stripped)
        if m and m.group(1) in alias_map:
            src = defs.get(alias_map[m.group(1)], {})
            src_labels = list(src.get("_enum_labels") or [])
            for t in targets:
                t["_enum_labels"] = list(src_labels)
        m = re.match(r'append\(\s*def->enum_labels\s*,\s*(\w+)->enum_labels\s*\)', stripped)
        if m and m.group(1) in alias_map:
            src = defs.get(alias_map[m.group(1)], {})
            for t in targets:
                t.setdefault("_enum_labels", []).extend(src.get("_enum_labels") or [])
        m = re.match(r'def->gui_type\s*=\s*(\w+)->gui_type\s*;', stripped)
        if m and m.group(1) in alias_map:
            src = defs.get(alias_map[m.group(1)], {})
            if "_gui_type" in src:
                for t in targets:
                    t["_gui_type"] = src["_gui_type"]
        if rx_multiline.search(line):
            for t in targets:
                t["_multiline"] = True
        if rx_readonly.search(line):
            for t in targets:
                t["_readonly"] = True
        m = rx_defv.search(line)
        if m:
            # the initializer may span lines: grow the segment until balanced
            kind = m.group(1)
            seg = line[m.end():]
            j = i
            arg, ok = balanced_content(seg)
            while not ok and j < end - 1:
                j += 1
                seg += lines[j]
                arg, ok = balanced_content(seg)
            if ok:
                # default referencing the per-axis table, e.g. axis.max_feedrate
                am = re.match(r"axis\.(\w+)$", (arg or "").strip())
                if am and loop_keys:
                    for key, axis in loop_keys:
                        vals = (axes_data.get(axis) or {}).get(am.group(1))
                        if vals:
                            defs[key]["default"] = vals
                else:
                    d = parse_default(kind, arg, enum_values)
                    if d is not None:
                        for t in targets:
                            t["default"] = d
            else:
                warnings.append("unbalanced set_default_value at PrintConfig.cpp:%d" % (i + 1))
            i = j  # resume after the (possibly multi-line) initializer
        i += 1
    return defs


# --------------------------------------------------------------------------
# Part 2: scope from PrintConfig.hpp class blocks
# --------------------------------------------------------------------------

def parse_scopes():
    lines = read_lines(PC_HPP)
    begin = find_line(lines, r"PRINT_CONFIG_CLASS_DEFINE\(\s*$")
    end = find_line(lines, r"#undef STATIC_PRINT_CONFIG_CACHE_BASE")
    rx_member = re.compile(r"\(\(\s*ConfigOption[\w<>,\s]*?,\s*(\w+)\s*\)\)")

    scopes = {}
    current_scope = None
    for i in range(begin, end):
        stripped = lines[i].strip()
        if re.match(r"^PRINT_CONFIG_CLASS_(DERIVED_)?DEFINE", stripped):
            current_scope = None
            for j in range(i, min(i + 4, end)):
                nm = re.search(r"^\s*(\w+),?\s*$", lines[j])
                if nm:
                    name = nm.group(1)
                    if name in SCOPE_CLASSES:
                        current_scope = SCOPE_CLASSES[name]
                    else:
                        current_scope = "SKIP"  # SLA classes / FullPrintConfig
                    break
            continue
        if current_scope in (None, "SKIP"):
            continue
        if stripped.startswith("//"):
            continue
        m = rx_member.search(stripped)
        if m:
            scopes[m.group(1)] = current_scope
    return scopes


# --------------------------------------------------------------------------
# Part 3: UI structure from Tab.cpp
# --------------------------------------------------------------------------

UI_FUNCTIONS = [
    # (bucket, signature regex, end-marker regex)
    ("print",     r"void TabPrint::build\(\)",                 r"void TabPrint::reload_config"),
    ("filament",  r"void TabFilament::build\(\)",              r"void TabFilament::reload_config"),
    # the Setting Overrides page is appended last inside build(), but built by
    # a separate function -- parse it after build so the tab order matches
    ("filament",  r"PageShp TabFilament::add_filament_overrides_page\(\)",
     r"void TabFilament::update_filament_overrides_page"),
    ("printer",   r"void TabPrinter::build_fff\(\)",           r"void TabPrinter::build_sla"),
    ("printer",   r"TabPrinter::build_kinematics_page\(\)",    r"void TabPrinter::build_unregular_pages"),
    ("printer",   r"TabPrinter::build_unregular_pages\(",      r"void TabPrinter::reload_config"),
]


def _resolve_key_expr(expr, loopvars):
    """Turn 'machine_max_acceleration_' + axis / speed_axis into concrete key(s)."""
    expr = expr.strip()
    m = re.match(r'^"([^"]*)"\s*\+\s*(\w+)$', expr)
    if m:
        vals = loopvars.get(m.group(2))
        return [m.group(1) + v for v in vals] if vals else [m.group(1) + "<%s>" % m.group(2)]
    m = re.match(r'^"([^"]+)"$', expr)
    if m:
        return m.group(1)
    if expr in loopvars:
        return list(loopvars[expr])
    return None


def parse_ui():
    lines = read_lines(TAB_CPP)
    ui = OrderedDict()  # bucket -> tabs -> groups -> keys
    for bucket, sig, endsig in UI_FUNCTIONS:
        begin = find_line(lines, sig)
        if begin < 0:
            warnings.append("UI function not found: " + sig)
            continue
        end = find_line(lines, endsig, begin + 1)
        if end < 0:
            warnings.append("UI end marker not found: " + endsig)
            continue
        bucket_ui = ui.setdefault(bucket, OrderedDict())

        cur_page = None
        cur_group = None
        loopvars = {}
        in_if0 = 0
        i = begin
        while i < end:
            line = lines[i]
            stripped = line.strip()
            if not stripped:
                i += 1
                continue
            if stripped.startswith("#if 0"):
                in_if0 += 1
                i += 1
                continue
            if in_if0:
                if stripped.startswith("#endif"):
                    in_if0 -= 1
                i += 1
                continue
            if stripped.startswith("//") or stripped.startswith("#"):
                i += 1
                continue

            # string-vector locals (single- or multi-line) used in loop appends
            m = re.match(r'const\s+std::vector<std::string>\s+(\w+)\s*\{(.*)', stripped)
            if m:
                text = m.group(2)
                j = i
                while "}" not in text and j + 1 < end:
                    j += 1
                    text += " " + lines[j].strip()
                    if lines[j].strip().startswith("//"):
                        break
                vals = re.findall(r'"([^"]*)"', text.split("}")[0])
                if vals:
                    loopvars[m.group(1)] = vals
                i = j + 1
                continue

            # for-range loop over a known vector: bind loop variable -> values
            m = re.match(r'for\s*\(\s*const\s+std::string\s*&?\s*(\w+)\s*:\s*(\w+)\s*\)', stripped)
            if m and m.group(2) in loopvars:
                loopvars[m.group(1)] = loopvars[m.group(2)]
                i += 1
                continue

            # for-range over a braced literal list:
            #   for (const std::string opt_key : { "k1", "k2", ... })
            # (entries commented out with // inside the list are dropped)
            m = re.match(r'for\s*\(\s*const\s+std::string\s*&?\s*(\w+)\s*:\s*\{', stripped)
            if m:
                text, j = re.sub(r'//.*$', '', stripped), i
                while "})" not in lines[j] and j + 1 < end:
                    j += 1
                    text += " " + re.sub(r'//.*$', '', lines[j].strip())
                text = re.sub(r'/\*.*?\*/', ' ', text)
                vals = re.findall(r'"([^"]*)"', text)
                if vals:
                    loopvars[m.group(1)] = vals
                i = j + 1
                continue

            # new page: the dynamic per-extruder pages, title is
            # _L("Extruder %d") formatted at runtime (Tab.cpp) -- keep the
            # %d template as the page key so the i18n lookup hits the po entry
            if "add_options_page(page_name" in stripped:
                cur_page = bucket_ui.setdefault("Extruder %d", OrderedDict())
                cur_group = None
                i += 1
                continue
            m = re.search(r'add_options_page\(\s*_?L\("([^"]+)"\)', stripped)
            if m:
                cur_page = bucket_ui.setdefault(m.group(1), OrderedDict())
                cur_group = None
                i += 1
                continue

            # unnamed groups still hold real keys (e.g. TabPrintModel "Frequent")
            if re.search(r'new_optgroup\(""\)', stripped):
                cur_group = (cur_page.setdefault("(untitled)", OrderedDict())
                             if cur_page is not None else None)
                i += 1
                continue
            m = re.search(r'new_optgroup\(\s*_?L\("([^"]+)"\)', stripped)
            if m and cur_page is not None:
                cur_group = cur_page.setdefault(m.group(1), OrderedDict())
                i += 1
                continue

            # keys
            target = None
            per_extruder = False
            m = re.search(r'append_single_option_line\("([^"]+)"(?:,\s*"[^"]*")?(,\s*extruder_idx)?\)', stripped)
            if m:
                target = m.group(1)
                per_extruder = bool(m.group(2))
            if target is None:
                # append_single_option_line(opt_key); -- bare loop variable from
                # a for-range over a literal list (Setting Overrides page)
                m = re.match(r'append_single_option_line\(\s*(\w+)\s*\)\s*;', stripped)
                if m and m.group(1) in loopvars:
                    target = list(loopvars[m.group(1)])
            if target is None:
                m = re.search(r'append_option_line\(\s*optgroup\s*,\s*(.+?)\)\s*;', stripped)
                if m:
                    target = _resolve_key_expr(m.group(1), loopvars)
            if target is None:
                m = re.search(r'(?:get_option|create_single_option_line)\("([^"]+)"', stripped)
                if m:
                    target = m.group(1)
            if target is None:
                # create_line_with_widget(optgroup.get(), "key", ...) mounts a
                # real option line too (it internally calls
                # create_single_option_line + append_line)
                m = re.search(r'create_line_with_widget\(\s*[\w.]+(?:\.get\(\))?\s*,\s*"([^"]+)"', stripped)
                if m:
                    target = m.group(1)

            if target is not None:
                if cur_group is None:
                    warnings.append("key outside any group (Tab.cpp:%d): %s" % (i + 1, target))
                else:
                    # indexed mounts (append_single_option_line(key, "", extruder_idx))
                    # bind a vector key to one extruder slot; per the schema
                    # convention this is implied by the Extruder %d template
                    # page itself, so it is not emitted as a per-key field --
                    # but a mount like that outside the template page would
                    # break the convention and is flagged instead
                    if per_extruder and cur_page is not bucket_ui.get("Extruder %d"):
                        warnings.append("indexed mount outside Extruder %%d page (Tab.cpp:%d): %s"
                                        % (i + 1, target))
                    for key in (target if isinstance(target, list) else [target]):
                        cur_group.setdefault(key, OrderedDict())
            i += 1

        # drop legend buckets if any slipped through
        for page in list(bucket_ui.values()):
            page.pop("(legend)", None)

    # Real display order of the pages inside each tab. The parse order (order
    # of the functions in UI_FUNCTIONS) does not always match it: the C++
    # inserts some pages at explicit m_pages positions instead of appending.
    PAGE_ORDER = {
        # TabFilament::build: the overrides page is created between "Cooling"
        # and "Advanced", not last where the separate parse function lands it
        "filament": ["Filament", "Cooling", "Setting Overrides", "Advanced",
                     "Multimaterial", "Dependencies", "Notes"],
        # TabPrinter::build_fff + build_unregular_pages(from_initial_build)
        # with a Marlin-class gcode_flavor: kinematics page inserted at
        # index 2, extruder pages right after it, Multimaterial inserted two
        # slots before the end (i.e. before "Notes"). Non-Marlin flavors
        # shift the extruder block ahead of "Motion ability" and clear that
        # page -- Marlin order is kept here as the default.
        "printer": ["Basic information", "Machine G-code", "Motion ability",
                    "Extruder %d", "Multimaterial", "Notes"],
    }
    for bucket, order in PAGE_ORDER.items():
        if bucket not in ui:
            continue
        known = [p for p in order if p in ui[bucket]]
        extra = [p for p in ui[bucket] if p not in order]
        if extra:
            warnings.append("page order table missing pages (%s): %s" % (bucket, extra))
        ui[bucket] = OrderedDict((p, ui[bucket][p]) for p in known + extra)
    return ui


# --------------------------------------------------------------------------
# Part 4: YAML emit
# --------------------------------------------------------------------------

def yq(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    s = s.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return '"%s"' % s


def fmt_scalar(v):
    if v is None:
        return "null"  # nullable "unset" element (filament_* override keys)
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    if isinstance(v, list):
        return "[%s]" % ", ".join(fmt_scalar(x) for x in v)  # nested: points
    return yq(str(v))


PROP_ORDER = ["type", "widget", "edit_enable", "scope", "mode", "nullable",
              "default", "min", "max", "values"]


def emit_option(out, indent, key, props):
    pad = " " * indent
    out.append("%s%s:" % (pad, key))
    for k in PROP_ORDER:
        if k not in props:
            continue
        v = props[k]
        if isinstance(v, list):
            out.append("%s  %s: [%s]" % (pad, k, ", ".join(fmt_scalar(x) for x in v)))
        else:
            out.append("%s  %s: %s" % (pad, k, fmt_scalar(v)))


# per-key field reference shared by the preset and hidden YAML headers:
# one synthetic entry carrying every property PROP_ORDER can emit
KEY_FIELD_NOTES = [
    "#   example_key:              # 合成全部字段的示例，实际 key 只带其中子集",
    "#     type: \"enum\"            # 存储/序列化类型（ConfigOption 子类）；复数形",
    "#                             # （strings/floats/bools/...）= per-extruder 向量，",
    "#                             # default 必须是数组，每个耗材槽一个元素（≠ 多选）",
    "#     widget: \"combobox\"      # UI 渲染控件，与 type 正交：editline 单行输入 /",
    "#                             # textarea 多行 / combobox 严格下拉（硬枚举）/",
    "#                             # combobox_edit 可输入下拉（软枚举）/ checkbox 勾选 /",
    "#                             # checkbox_override_edit 勾选门控编辑 / color 颜色 /",
    "#                             # edit_serialized 序列化串 / point 坐标",
    "#     edit_enable: true       # false = UI 只读（源码 readonly = true）",
    "#     scope: \"print_object\"   # 切片侧类型边界 = 所属 StaticPrintConfig 类：",
    "#                             # print_object 对象级 / print_region 部件级 / machine",
    "#                             # 机器限制 / gcode G-code 输出期 / print 全局工艺 /",
    "#                             # none preset 元数据",
    "#     mode: \"advanced\"        # UI 显示层级：simple / advanced / develop（缺省 simple）",
    "#     nullable: true          # 值可为 null = 「自动 / 不覆盖」而非 0；仅 Setting",
    "#                             # Overrides 回抽覆盖 key 出现，default 固定 [null]",
    "#     default: \"aligned\"      # 默认值；float_or_percent 百分比写带引号字符串",
    "#                             # （\"400%\"），percent 写数值（20，UI 才加 %）",
    "#     min: 0                  # 连续值域下界（缺省 = 不限）",
    "#     max: 100                # 连续值域上界",
    "#     values: [\"a\", \"b\"]      # 离散值域（下拉候选），与 min/max 相对，任何 type 可带",
]

# header field reference shared by the preset YAMLs (print/filament/printer)
PRESET_FIELD_NOTES = [
    "# 预设 schema：页面结构 + 每个 key 的语义与取值；展示层文本/翻译在 i18n/*.yaml。",
    "# 层级：tablist → tabs.<tab>.groups.<group>.keys.<key>；tablist 显式声明 tab 显示",
    "# 顺序（YAML mapping 不保证顺序，消费端按此数组还原页序）。完整文档见 README.md。",
    "# 槽位绑定约定：Extruder %d 模板页内的向量 key 一律绑定第 i 个挤出机槽位",
    "# （源码挂载全部带 extruder_idx 索引，由页面语义蕴含，不再逐 key 标记）；",
    "# filament 页的向量 key 绑定当前活跃耗材槽。",
    "# 每个 key 的字段说明：",
] + KEY_FIELD_NOTES


def emit_preset_yaml(path, tabs):
    out = ["# Generated by scripts/schema_gen/generate_schema.py -- do not edit by hand",
           ] + PRESET_FIELD_NOTES + [
           "version: 0.0.1",
           "tablist: [%s]" % ", ".join(yq(t) for t in tabs.keys()),
           "tabs:"]
    nkeys = 0
    for tab, groups in tabs.items():
        out.append("  %s:" % yq(tab))
        out.append("    groups:")
        for group, keys in groups.items():
            out.append("      %s:" % yq(group))
            if keys:
                out.append("        keys:")
                for key, props in keys.items():
                    emit_option(out, 10, key, props)
                    nkeys += 1
            else:
                out.append("        keys: {}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out).rstrip() + "\n")
    return nkeys


HIDDEN_FIELD_NOTES = [
    "# PrintConfigDef 中定义、但不出现在任何预设 tab UI 的 key；无 tab 层，",
    "# 顶层 groups 直接按 scope（StaticPrintConfig 类）分组：groups.<scope>.keys。",
    "# 每个 key 的字段说明：",
] + KEY_FIELD_NOTES


def emit_hidden_yaml(path, scope_groups):
    out = ["# Generated by scripts/schema_gen/generate_schema.py -- do not edit by hand",
           ] + HIDDEN_FIELD_NOTES + [
           "version: 0.0.1",
           "groups:"]
    nkeys = 0
    for scope, keys in scope_groups.items():
        out.append("  %s:" % yq(scope))
        if keys:
            out.append("    keys:")
            for key, props in keys.items():
                emit_option(out, 6, key, props)
                nkeys += 1
        else:
            out.append("    keys: {}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out).rstrip() + "\n")
    return nkeys


# --------------------------------------------------------------------------
# i18n: labels + translations into per-preset language files
# --------------------------------------------------------------------------

PO_FILES = {  # language -> po path (relative to repo root)
    "zh_CN": os.path.join("localization", "i18n", "zh_CN", "Snapmaker_Orca_zh_CN.po"),
}

# keys whose definition is built dynamically at runtime and thus invisible
# to the static parser; properties transcribed by hand
MANUAL_KEYS = {
    "project_schema_version": {
        "type": "int", "scope": "gcode", "mode": "develop",
        "min": 1, "default": 1,
        "_label": "Project schema version",
    },
}


def load_po(path):
    """minimal po parser: msgid -> msgstr, skipping fuzzy entries"""
    tr = {}
    msgid = msgstr = None
    state = None
    fuzzy = False

    def unesc(s):
        return s.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("#"):
                if "fuzzy" in line:
                    fuzzy = True
                continue
            if line.startswith("msgid "):
                if msgid and msgstr and not fuzzy:
                    tr[msgid] = msgstr
                msgid, msgstr, state, fuzzy = unesc(line[6:].strip('"')), None, "id", False
            elif line.startswith("msgstr "):
                msgstr, state = unesc(line[7:].strip('"')), "str"
            elif line.startswith('"') and state in ("id", "str"):
                frag = unesc(line[1:-1]) if line.endswith('"') else ""
                if state == "id":
                    msgid += frag
                elif msgstr is not None:
                    msgstr += frag
    if msgid and msgstr and not fuzzy:
        tr[msgid] = msgstr
    return tr


def yaml_text(s):
    """plain scalar when safe, quoted otherwise"""
    if s and not re.search(r'[:#"\'\n]|^\s|\s$', s):
        return s
    return yq(s)


# display-layer fields carried per key in the i18n files (source field names)
DISPLAY_FIELDS = ("label", "full_label", "tooltip", "sidetext", "category")


def _disp_lines(field, text, translations, pad, ntr):
    """one display field as: text (source) + one line per language that
    actually translates it; equal-to-source and missing translations are
    omitted so consumers fall back to `text`."""
    lines = ["%s%s:" % (pad, field),
             "%s  text: %s" % (pad, yq(text))]
    for lang, tr in translations.items():
        t = tr.get(text)
        if t and t != text:
            lines.append("%s  %s: %s" % (pad, lang, yq(t)))
            ntr[0] += 1
    return lines


I18N_FIELD_NOTES = [
    "# 展示层：每个 key 的源码原文 + 已有翻译。结构与语义在主 YAML，",
    "# 一切「显示什么、翻译成什么」在这里。单个 key 的完整字段示例",
    "# （合成全部展示字段，实际 key 只带其中子集）：",
    "#   example_key:",
    "#     label:                  # 主标签（选项行标题）",
    "#       text: \"Layer height\"  # 源码原文（L() 的 msgid），永远存在",
    "#       zh_CN: \"层高\"         # 有译文才出现该语言键；缺失 = 回落 text",
    "#     full_label:             # 完整标签（对象设置面板等空间充裕处）",
    "#       text: \"...\"",
    "#     tooltip:                # 悬浮提示",
    "#       text: \"...\"",
    "#     sidetext:               # 行内单位/短后缀（mm、% 等，多为源码裸串无翻译）",
    "#       text: \"mm\"",
    "#     category:               # 搜索/归类的分类名",
    "#       text: \"Strength\"",
    "#     enum_labels:            # 枚举选项标签数组，与主 YAML 的 values 按下标平行",
    "#       - text: \"Auto\"        # values[i] 显示为 enum_labels[i]，长度相等可 zip",
    "#         zh_CN: \"自动\"",
    "# 文件末尾 tabs:/groups: 节收录 tab/组显示名，{text, 语言} 结构同上",
    "# （hidden.yaml 无此二节；Extruder %d 模板名原样保留，运行时格式化编号）。",
]


def emit_i18n(path, ordered_keys, tab_names, group_names, defs, translations):
    """Display layer: per key the source label/full_label/tooltip/sidetext/
    category/enum_labels text plus available translations, keyed by language
    inside each field. tab/group names get the same treatment. Structure and
    semantics stay in the main YAMLs; everything visual lives here."""
    out = ["# Generated by scripts/schema_gen/generate_schema.py -- do not edit by hand",
           ] + I18N_FIELD_NOTES + [
           "version: 0.0.1",
           "keys:"]
    n_keys = 0
    ntr = [0]
    wrote_any = False
    for key in ordered_keys:
        d = defs.get(key)
        if d is None:
            continue
        lines = []
        for field in DISPLAY_FIELDS:
            text = d.get("_" + field)
            if text:
                lines += _disp_lines(field, text, translations, "    ", ntr)
        els = d.get("_enum_labels") or []
        if els:
            lines.append("    enum_labels:")
            for el in els:
                lines.append("      - text: %s" % yq(el))
                for lang, tr in translations.items():
                    t = tr.get(el)
                    if t and t != el:
                        lines.append("        %s: %s" % (lang, yq(t)))
                        ntr[0] += 1
        if lines:
            out.append("  %s:" % key)
            out += lines
            n_keys += 1
            wrote_any = True
    if not wrote_any:
        out.append("  {}")
    for section, names in (("tabs", tab_names), ("groups", group_names)):
        out.append("%s:" % section)
        wrote = False
        seen = set()
        for name in names:
            if not name or name in seen:
                continue
            seen.add(name)
            out.append("  %s:" % yq(name))
            out.append("    text: %s" % yq(name))
            for lang, tr in translations.items():
                t = tr.get(name)
                if t and t != name:
                    out.append("    %s: %s" % (lang, yq(t)))
                    ntr[0] += 1
            wrote = True
        if not wrote:
            out.append("  {}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out).rstrip() + "\n")
    return n_keys, ntr[0]


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    defs = parse_defs()
    scopes = parse_scopes()
    ui = parse_ui()

    for key, d in defs.items():
        sc = scopes.get(key)
        if sc is None:
            # not owned by any StaticPrintConfig class: preset metadata /
            # DynamicPrintConfig-only entries (printhost, inherits, ...)
            sc = "none"
        d["scope"] = sc

    # keys defined dynamically at runtime (invisible to the static parser)
    for key, d in MANUAL_KEYS.items():
        defs.setdefault(key, OrderedDict(d))

    # resolve the widget (control) for every key: gui_type/multiline when the
    # source sets them explicitly, otherwise inferred from type/values
    for key, d in defs.items():
        d["widget"] = widget_of(d)
        # editable by default; only source-level readonly (=true) disables it
        d["edit_enable"] = not d.get("_readonly", False)

    os.makedirs(OUT_DIR, exist_ok=True)

    shown = set()
    counts = {}
    for preset in ("print", "filament", "printer"):
        tabs = ui.get(preset, OrderedDict())
        for tab, groups in tabs.items():
            for group, keys in groups.items():
                for key in list(keys):
                    if key not in defs:
                        warnings.append("UI key not in PrintConfigDef: %s (%s/%s/%s)"
                                        % (key, preset, tab, group))
                        continue
                    props = keys[key]
                    merged = OrderedDict()
                    for k in PROP_ORDER:
                        if k in props:
                            merged[k] = props[k]
                        elif k in defs[key]:
                            merged[k] = defs[key][k]
                    keys[key] = merged
                    shown.add(key)
        path = os.path.join(OUT_DIR, preset + ".yaml")
        counts[preset] = emit_preset_yaml(path, tabs)

    hidden = OrderedDict()
    for key, d in defs.items():
        if key in shown:
            continue
        entry = OrderedDict((k, d[k]) for k in PROP_ORDER if k in d)
        hidden.setdefault(d["scope"], OrderedDict())[key] = entry

    path = os.path.join(OUT_DIR, "hidden.yaml")
    hcount = emit_hidden_yaml(path, hidden)

    # i18n files: one per preset tab + one for hidden keys; every key carries
    # its display fields (source text) with whatever translations exist
    translations = {}
    for lang, po in PO_FILES.items():
        po_path = os.path.join(ROOT, po)
        if os.path.exists(po_path):
            translations[lang] = load_po(po_path)
        else:
            warnings.append("po not found, language %s skipped: %s" % (lang, po))
    i18n_dir = os.path.join(OUT_DIR, "i18n")
    os.makedirs(i18n_dir, exist_ok=True)
    i18n_counts = {}
    for preset in ("print", "filament", "printer"):
        ordered, tnames, gnames = [], [], []
        for tab, groups in ui.get(preset, OrderedDict()).items():
            tnames.append(tab)
            for group, gkeys in groups.items():
                gnames.append(group)
                for key in gkeys:
                    if key in defs and key not in ordered:
                        ordered.append(key)
        i18n_counts[preset] = emit_i18n(
            os.path.join(i18n_dir, preset + ".yaml"),
            ordered, tnames, gnames, defs, translations)
    hidden_ordered = [key for keys in hidden.values() for key in keys]
    i18n_counts["hidden"] = emit_i18n(
        os.path.join(i18n_dir, "hidden.yaml"),
        hidden_ordered, [], [], defs, translations)

    print("defs parsed:           %d" % len(defs))
    print("print.yaml keys:       %d" % counts["print"])
    print("filament.yaml keys:    %d" % counts["filament"])
    print("printer.yaml keys:     %d" % counts["printer"])
    print("hidden.yaml keys:      %d" % hcount)
    for preset, (nk, nt) in i18n_counts.items():
        print("i18n/%s.yaml: %d keys, %d translated fields" % (preset, nk, nt))
    if warnings:
        print("\nWARNINGS (%d):" % len(warnings))
        for w in warnings:
            print("  - " + w)


if __name__ == "__main__":
    main()
