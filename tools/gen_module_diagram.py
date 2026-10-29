#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate a mermaid module dependency diagram from Kconfig @module annotations.

Reusable core: kconfig2mermaid.parse() + kconfig2mermaid.render().
Project wrapper: --update / --check against a markdown file with markers.

Annotation format (in kconfig comment lines):
    # @module                             declare this file as a module (ID from filename)
    # @module ID                          declare module with explicit ID
    # @module ID "Override Name"          declare with explicit display name
    # @module [ID] : comp · list          explicit component line (overrides auto-derive)
    # @module [ID] -> TARGET              solid arrow (hard dependency)
    # @module [ID] --> TARGET             dashed arrow (optional dependency)
    # @module [ID] --> TARGET "label"     dashed arrow with edge label

When ID is omitted in edge/component lines, the file's default module ID is used.
Files without any @module annotation are not included in the diagram.

Auto-derived:
    - Module ID from filename (ble.kconfig -> BLE)
    - Display name from 'menu "Title"' in the file
    - Components from choice prompts (short labels) and standalone config symbols
    - '-> HAL' edge for every non-HAL module (when a HAL module exists)

Usage:
    kc2mm generate                       # emit mermaid to stdout
    kc2mm generate -o diagram.mmd        # emit to file
    kc2mm inject                         # update docs/COMPONENTS.md in-place
    kc2mm inject --check                 # exit 1 if stale
    kc2mm generate --long-names          # use full choice prompt text
"""
import argparse
import re
import sys
from pathlib import Path


# ---------------------------------------------------------------------------
# kconfig2mermaid core (portable, no project-specific paths)
# ---------------------------------------------------------------------------

ACRONYMS = {"MIDI", "HID", "USB", "BLE", "HAL", "SPI", "I2S", "OTA",
            "DMA", "WPA", "DAP", "CDP", "FT", "ADC", "CDC", "GM",
            "UART", "LCD"}
KNOWN_LABELS = {"LWIP": "lwIP"}
KNOWN_SHORT = {"multicore": "mc"}
UPSTREAM_NAMES = {"FELUCCA", "SLOOP", "MELODEE", "FMLAB", "OPTIMIST", "CORE"}
SKIP_SYMBOLS = {"GEN_FELUCCA_UI", "ENGINES"}
SKIP_WORDS = {"stub", "test"}
MAX_COMPONENTS_PER_LINE = 6

RE_MOD_BARE = re.compile(r'^#\s*@module\s*$')
RE_MOD_ID = re.compile(r'^#\s*@module\s+(\w+)\s*$')
RE_MOD_NAMED = re.compile(r'^#\s*@module\s+(\w+)\s+"([^"]+)"\s*$')
RE_COMP = re.compile(r'^#\s*@module\s+(?:(\w+)\s+)?:\s*(.+)$')
RE_EDGE_SOLID = re.compile(r'^#\s*@module\s+(?:(\w+)\s+)?->\s+(\w+)\s*$')
RE_EDGE_DASHED = re.compile(r'^#\s*@module\s+(?:(\w+)\s+)?-->\s+(\w+)\s*(?:"([^"]+)")?\s*$')
RE_MENU = re.compile(r'^menu\s+"([^"]+)"')
RE_CONFIG = re.compile(r'^config\s+(\w+)')
RE_BOOL_PROMPT = re.compile(r'^\s+bool\s+"([^"]+)"')
RE_CHOICE = re.compile(r'^choice\b')
RE_CHOICE_PROMPT = re.compile(r'^\s+prompt\s+"([^"]+)"')
RE_ENDCHOICE = re.compile(r'^endchoice\b')


def _id_from_filename(path):
    """ble.kconfig -> BLE, sys_menu.kconfig -> SYS_MENU."""
    return path.stem.upper()


def _strip_upstream(name):
    """Remove upstream name parts from a symbol segment."""
    parts = name.split("_")
    filtered = [p for p in parts if p not in UPSTREAM_NAMES]
    return "_".join(filtered)


def _symbol_to_label(symbol, prefix, long_names=False):
    """Convert config symbol name to a display label."""
    name = symbol
    if prefix and name.startswith(prefix):
        name = name[len(prefix):]

    if name in KNOWN_LABELS:
        return KNOWN_LABELS[name]

    if not long_names:
        stripped = _strip_upstream(name)
        if not stripped:
            return None
        short = stripped.lower()
        if short in KNOWN_SHORT:
            return KNOWN_SHORT[short]
        return short

    parts = name.split("_")
    result = []
    for p in parts:
        if p.upper() in ACRONYMS:
            result.append(p.upper())
        else:
            result.append(p.capitalize())
    return " ".join(result)


def _choice_label(prompt, first_config, module_prefix, long_names=False):
    """Derive a component label from a choice block."""
    if first_config:
        name = first_config
        if module_prefix and name.startswith(module_prefix):
            name = name[len(module_prefix):]
        stripped = _strip_upstream(name)
        if stripped:
            if long_names:
                return prompt
            short = stripped.lower()
            return KNOWN_SHORT.get(short, short)

    if long_names:
        return prompt
    word = prompt.split("(")[0].strip().split()[0].lower()
    return KNOWN_SHORT.get(word, word)


def _auto_derive_components(file_lines, module_id, long_names=False):
    """Extract component labels from config symbols and choice prompts."""
    components = []
    prefix = module_id + "_"
    i = 0

    while i < len(file_lines):
        line = file_lines[i]

        if RE_CHOICE.match(line):
            prompt = None
            first_config = None
            j = i + 1
            while j < len(file_lines) and not RE_ENDCHOICE.match(file_lines[j]):
                pm = RE_CHOICE_PROMPT.match(file_lines[j])
                if pm:
                    prompt = pm.group(1)
                if first_config is None:
                    cm = RE_CONFIG.match(file_lines[j])
                    if cm:
                        first_config = cm.group(1)
                j += 1

            if prompt:
                label = _choice_label(prompt, first_config, prefix, long_names)
                if label and not any(w in label.lower() for w in SKIP_WORDS):
                    if label not in components:
                        components.append(label)
            i = j + 1
            continue

        cm = RE_CONFIG.match(line)
        if cm:
            sym = cm.group(1)
            if sym == module_id or sym in SKIP_SYMBOLS:
                i += 1
                continue

            j = i + 1
            is_visible = False
            while j < len(file_lines):
                stripped = file_lines[j].strip()
                if not stripped or RE_CONFIG.match(file_lines[j]) or \
                   RE_MENU.match(file_lines[j]) or RE_CHOICE.match(file_lines[j]):
                    break
                if RE_BOOL_PROMPT.match(file_lines[j]):
                    is_visible = True
                j += 1

            if is_visible:
                label = _symbol_to_label(sym, prefix, long_names)
                if label and not any(w in label.lower() for w in SKIP_WORDS):
                    components.append(label)
        i += 1

    return components


def _group_components(components):
    result = []
    for i in range(0, len(components), MAX_COMPONENTS_PER_LINE):
        chunk = components[i:i + MAX_COMPONENTS_PER_LINE]
        result.append(" · ".join(chunk))
    return result


def parse(kconf_dir, *, hal_module="HAL", skip_symbols=None, long_names=False):
    """Parse a kconfig directory for @module annotations.

    Args:
        kconf_dir: Path to directory containing *.kconfig files.
        hal_module: Module ID to auto-add edges to (None to disable).
        skip_symbols: Extra symbols to exclude from component derivation.
        long_names: Use full choice prompt text instead of short symbol-derived labels.

    Returns:
        (modules, edges) where modules is {id: {name, components, order}}
        and edges is [{from, to, style, label}].
    """
    if skip_symbols:
        SKIP_SYMBOLS.update(skip_symbols)

    kconf_dir = Path(kconf_dir)
    modules = {}
    edges = []
    order = 0

    for kf in sorted(kconf_dir.glob("*.kconfig")):
        file_lines = kf.read_text().splitlines()
        file_default_id = _id_from_filename(kf)
        file_module_ids = []
        file_has_comp_override = set()
        has_module_annotation = False

        for line in file_lines:
            if RE_MOD_BARE.match(line):
                has_module_annotation = True
                mid = file_default_id
                if mid not in modules:
                    file_module_ids.append(mid)
                    modules[mid] = {"name": None, "components": [],
                                    "order": order}
                    order += 1
                continue

            m = RE_MOD_NAMED.match(line)
            if m:
                has_module_annotation = True
                mid, name = m.group(1), m.group(2)
                if mid not in modules:
                    file_module_ids.append(mid)
                    modules[mid] = {"name": name, "components": [],
                                    "order": order}
                    order += 1
                else:
                    modules[mid]["name"] = name
                continue

            m = RE_MOD_ID.match(line)
            if m:
                has_module_annotation = True
                mid = m.group(1)
                if mid not in modules:
                    file_module_ids.append(mid)
                    modules[mid] = {"name": None, "components": [],
                                    "order": order}
                    order += 1
                continue

            m = RE_COMP.match(line)
            if m:
                has_module_annotation = True
                mid = m.group(1) or (file_module_ids[0] if file_module_ids else file_default_id)
                comps = m.group(2).strip()
                file_has_comp_override.add(mid)
                if mid not in modules:
                    file_module_ids.append(mid)
                    modules[mid] = {"name": None, "components": [],
                                    "order": order}
                    order += 1
                modules[mid]["components"].append(comps)
                continue

            m = RE_EDGE_SOLID.match(line)
            if m:
                has_module_annotation = True
                src = m.group(1) or (file_module_ids[0] if file_module_ids else file_default_id)
                edges.append({"from": src, "to": m.group(2),
                              "style": "solid", "label": None})
                continue

            m = RE_EDGE_DASHED.match(line)
            if m:
                has_module_annotation = True
                src = m.group(1) or (file_module_ids[0] if file_module_ids else file_default_id)
                edges.append({"from": src, "to": m.group(2),
                              "style": "dashed", "label": m.group(3)})
                continue

        if not has_module_annotation:
            continue

        menu_title = None
        for line in file_lines:
            mm = RE_MENU.match(line)
            if mm:
                menu_title = mm.group(1)
                break

        for mid in file_module_ids:
            mod = modules[mid]
            if mod["name"] is None:
                if mid == file_default_id and menu_title:
                    mod["name"] = menu_title
                else:
                    mod["name"] = mid

        for mid in file_module_ids:
            if mid not in file_has_comp_override:
                auto = _auto_derive_components(file_lines, mid, long_names)
                if auto:
                    modules[mid]["components"] = _group_components(auto)

    if hal_module and hal_module in modules:
        hal_edges = {e["from"] for e in edges if e["to"] == hal_module}
        for mid in modules:
            if mid != hal_module and mid not in hal_edges:
                edges.append({"from": mid, "to": hal_module,
                              "style": "solid", "label": None})

    return modules, edges


def render(modules, edges, *, direction="TB", curve="step"):
    """Render modules and edges into a mermaid flowchart string.

    Args:
        modules: Dict from parse().
        edges: List from parse().
        direction: Mermaid flowchart direction (TB, LR, etc.).
        curve: Mermaid curve style.

    Returns:
        Mermaid flowchart as a string.
    """
    lines = [
        f'%%{{init: {{"flowchart": {{"curve": "{curve}"}}}} }}%%',
        f'flowchart {direction}',
    ]

    sorted_mods = sorted(modules.items(), key=lambda kv: kv[1]["order"])

    for mid, mod in sorted_mods:
        label_parts = [f'<b>{mod["name"]}</b>']
        if mod["components"]:
            comp_html = "<br/>".join(mod["components"])
            label_parts.append(f'<small>{comp_html}</small>')
        label = "<br/>".join(label_parts)
        lines.append(f'    {mid}["{label}"]')

    lines.append("")

    for edge in edges:
        src, dst = edge["from"], edge["to"]
        if edge["style"] == "solid":
            lines.append(f"    {src} --> {dst}")
        elif edge["label"]:
            lines.append(f"    {src} -.->|{edge['label']}| {dst}")
        else:
            lines.append(f"    {src} -.-> {dst}")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Markdown file update (project integration)
# ---------------------------------------------------------------------------

DEFAULT_START_MARKER = "<!-- module-diagram:start -->"
DEFAULT_END_MARKER = "<!-- module-diagram:end -->"


def wrap_block(mermaid, start_marker=None, end_marker=None):
    """Wrap mermaid in a marker-delimited fenced code block."""
    start = start_marker or DEFAULT_START_MARKER
    end = end_marker or DEFAULT_END_MARKER
    return f"{start}\n```mermaid\n{mermaid}\n```\n{end}"


def replace_in_doc(doc_text, new_block, start_marker=None, end_marker=None):
    """Replace content between markers in a document string."""
    start = start_marker or DEFAULT_START_MARKER
    end = end_marker or DEFAULT_END_MARKER
    start_idx = doc_text.find(start)
    end_idx = doc_text.find(end)
    if start_idx == -1 or end_idx == -1:
        raise ValueError(f"Markers not found: {start} / {end}")
    end_idx += len(end)
    return doc_text[:start_idx] + new_block + doc_text[end_idx:]


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _add_common_args(parser, root):
    """Add arguments shared by all subcommands."""
    parser.add_argument("--kconf-dir", type=Path,
                        default=root / "kconf",
                        help="Directory containing *.kconfig files")
    parser.add_argument("--hal-module", default="HAL",
                        help="Module ID for auto HAL edges (empty to disable)")
    parser.add_argument("--direction", default="TB",
                        help="Mermaid flowchart direction (TB, LR, etc.)")
    parser.add_argument("--long-names", action="store_true",
                        help="Use full choice prompt text instead of short labels")


def _parse_and_render(args):
    """Parse kconfig and render mermaid from parsed args."""
    hal = args.hal_module or None
    modules, edges = parse(args.kconf_dir, hal_module=hal,
                           long_names=args.long_names)
    if not modules:
        print(f"Error: no @module annotations found in {args.kconf_dir}/",
              file=sys.stderr)
        sys.exit(1)
    return render(modules, edges, direction=args.direction)


def cmd_generate(args):
    """generate: emit mermaid to stdout or a file."""
    mermaid = _parse_and_render(args)
    if args.output:
        args.output.write_text(mermaid + "\n")
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(mermaid)


def cmd_inject(args):
    """inject: replace marker-delimited block in a markdown file."""
    mermaid = _parse_and_render(args)
    new_block = wrap_block(mermaid, args.start_marker, args.end_marker)

    try:
        doc = args.file.read_text()
    except FileNotFoundError:
        print(f"Error: {args.file} not found", file=sys.stderr)
        sys.exit(1)

    if args.start_marker not in doc:
        if args.check:
            print(f"Error: markers not found in {args.file}", file=sys.stderr)
            sys.exit(1)
        doc += "\n" + new_block + "\n"
        args.file.write_text(doc)
        print(f"Injected markers into {args.file}")
        return

    try:
        updated = replace_in_doc(doc, new_block,
                                 args.start_marker, args.end_marker)
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    if args.check:
        if doc != updated:
            print(f"STALE: {args.file} does not match {args.kconf_dir}/ annotations",
                  file=sys.stderr)
            sys.exit(1)
        print(f"OK: {args.file} is up to date")
    else:
        args.file.write_text(updated)
        print(f"Updated {args.file}")


def main():
    root = Path(__file__).resolve().parents[1]

    parser = argparse.ArgumentParser(
        prog="kc2mm",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command")

    # --- generate (default) ---
    gen = sub.add_parser("generate", aliases=["gen"],
                         help="Emit mermaid diagram to stdout or file")
    _add_common_args(gen, root)
    gen.add_argument("-o", "--output", type=Path, default=None,
                     help="Write to file instead of stdout")
    gen.set_defaults(func=cmd_generate)

    # --- inject ---
    inj = sub.add_parser("inject",
                         help="Insert/update diagram in a markdown file")
    _add_common_args(inj, root)
    inj.add_argument("file", type=Path, nargs="?",
                     default=root / "docs" / "COMPONENTS.md",
                     help="Markdown file to update")
    inj.add_argument("--start-marker", default=DEFAULT_START_MARKER)
    inj.add_argument("--end-marker", default=DEFAULT_END_MARKER)
    inj.add_argument("--check", action="store_true",
                     help="Exit 1 if file is stale (don't write)")
    inj.set_defaults(func=cmd_inject)

    # back-compat: bare --update / --check without subcommand
    parser.add_argument("--update", action="store_true",
                        help="(compat) same as: inject")
    parser.add_argument("--check", action="store_true",
                        help="(compat) same as: inject --check")

    args = parser.parse_args()

    if args.command:
        args.func(args)
    elif args.update or args.check:
        args.file = root / "docs" / "COMPONENTS.md"
        args.start_marker = DEFAULT_START_MARKER
        args.end_marker = DEFAULT_END_MARKER
        args.kconf_dir = root / "kconf"
        args.hal_module = "HAL"
        args.direction = "TB"
        args.long_names = False
        args.check = args.check
        cmd_inject(args)
    else:
        args.kconf_dir = root / "kconf"
        args.hal_module = "HAL"
        args.direction = "TB"
        args.long_names = False
        args.output = None
        cmd_generate(args)


if __name__ == "__main__":
    main()
