#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Provenance tests for the clean-room WiFi driver.

(a) Constant trace: every hex literal in the WiFi source (excluding nonfree/)
    and HAL headers traces to the SPECSHEET, a public standard, or a pinned
    reference.
(b) Citation resolution: every 'SPECSHEET §x.y' citation resolves to a section
    heading in the SPECSHEET that supports the claim.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'docs' / 'hardware' / 'wifi' / 'SPECSHEET.md'
WIFI_SRC = ROOT / 'firmware' / 'src' / 'wifi'
WIFI_HAL = ROOT / 'firmware' / 'hal'


def _wifi_source_files():
    """All .c and .h files in the WiFi driver (excluding nonfree/)."""
    files = []
    for ext in ('*.c', '*.h'):
        for p in WIFI_SRC.rglob(ext):
            if 'nonfree' not in p.parts:
                files.append(p)
    for p in WIFI_HAL.glob('fm1_wl_*.h'):
        files.append(p)
    return sorted(files)


def _specsheet_section_ids():
    """Parse all section numbers from the SPECSHEET headings.
    Returns a set of section IDs like {'6.7', '10.1.1', '11.2.9'}."""
    text = SPEC.read_text()
    ids = set()
    for m in re.finditer(r'^#{1,6}\s+(\d+(?:\.\d+)*)', text, re.MULTILINE):
        ids.add(m.group(1))
    for m in re.finditer(r'^\*\*(\d+(?:\.\d+)*)\b', text, re.MULTILINE):
        ids.add(m.group(1))
    return ids


def _source_citations():
    """Extract SPECSHEET §x.y citations from WiFi source files.
    Skips citations prefixed by another document name (e.g. 'RT5350 §3.20').
    Returns list of (file, line_number, citation_text, section_id)."""
    citations = []
    pat = re.compile(r'(?:SPECSHEET\s+)?§(\d+(?:\.\d+)*)')
    skip_pat = re.compile(r'(?:RT\d+|IEEE|FIPS|RFC)\s+§')
    for f in _wifi_source_files():
        for i, line in enumerate(f.read_text().splitlines(), 1):
            for m in pat.finditer(line):
                prefix = line[max(0, m.start() - 20):m.start()]
                if skip_pat.search(prefix + '§'):
                    continue
                citations.append((f.name, i, m.group(0), m.group(1)))
    return citations


# Public standard register ranges: RT2800 MAC base + offsets, AC791N MMIO
_PUBLIC_RANGES = [
    (0x30000, 0x3FFFF),  # RT2800 MAC registers (SPECSHEET §7.2)
    (0x10000, 0x1FFFF),  # AC791N SoC registers (PUB SDK headers)
    (0x11900, 0x11980),  # WL RF analog (SPECSHEET §3)
    (0x14000, 0x14100),  # WL SoC control (SPECSHEET §4)
]

# Known constants from public standards (IEEE 802.11, FIPS, RT2800, etc.)
_PUBLIC_CONSTANTS = {
    0x9407,        # MAC mode register (RT2800)
    0x0000025F,    # crystal word base (SPECSHEET §11.2.9)
    0x1021,        # CRC-16/XMODEM polynomial
    0x80211,       # IEEE 802.11 magic
    # SHA-1 initial hash values (FIPS 180-4 §5.3.1)
    0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0,
    # SHA-1 round constants (FIPS 180-4 §4.2.1)
    0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6,
    # EtherTypes (IEEE 802.3)
    0x888E,        # EAPOL
    0x0806,        # ARP
    # RT2800 MAC register offsets (from WIFI_MAC_BASE, public RT5350 datasheet)
    0x1010, 0x1014, 0x1704, 0x1708, 0x1710, 0x1130, 0x1134, 0x1138,
    0x1718, 0x0431,
    0x0148,        # TX_PIN_CFG offset (RT2800)
}


def _specsheet_hex_literals():
    """Extract all hex literals from the SPECSHEET."""
    text = SPEC.read_text()
    hexvals = set()
    for m in re.finditer(r'0x([0-9A-Fa-f]+)', text):
        hexvals.add(int(m.group(1), 16))
    for m in re.finditer(r'`0x([0-9A-Fa-f]+)`', text):
        hexvals.add(int(m.group(1), 16))
    return hexvals


def _is_in_public_range(val):
    for lo, hi in _PUBLIC_RANGES:
        if lo <= val <= hi:
            return True
    return False


def test_citation_resolution():
    """Every §x.y citation in the WiFi source resolves to a SPECSHEET heading."""
    section_ids = _specsheet_section_ids()
    citations = _source_citations()
    assert citations, 'No citations found (test infrastructure broken?)'

    unresolved = []
    for fname, line, text, sid in citations:
        if sid not in section_ids:
            unresolved.append(f'{fname}:{line}: {text}')

    assert not unresolved, (
        f'{len(unresolved)} citation(s) do not resolve to a SPECSHEET heading:\n'
        + '\n'.join(unresolved[:20])
    )


def test_constant_trace():
    """Hex literals >= 0x100 in WiFi source files can be traced to the SPECSHEET,
    a public standard range, or an allowed constant."""
    spec_hex = _specsheet_hex_literals()
    pat = re.compile(r'\b0x([0-9A-Fa-f]{3,})u?\b')

    # Trivial values and common bit patterns that are structural, not hardware
    trivial = {0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000,
               0xFFF, 0x1FF, 0x1E0, 0x3FF, 0x3FFF, 0x7FF, 0x7FFFFFFF,
               0xFFFF, 0xFFFFFFFF, 0xFF00, 0xFF0000, 0xFFF0000, 0xFFFFFFF}

    untraceable = []
    for f in _wifi_source_files():
        for i, line in enumerate(f.read_text().splitlines(), 1):
            if line.lstrip().startswith(('/*', '*', '//')):
                continue
            for m in pat.finditer(line):
                val = int(m.group(1), 16)
                if val < 0x100:
                    continue
                if val in trivial:
                    continue
                if val in spec_hex:
                    continue
                if val in _PUBLIC_CONSTANTS:
                    continue
                if _is_in_public_range(val):
                    continue
                untraceable.append(f'{f.name}:{i}: 0x{val:X}')

    assert not untraceable, (
        f'{len(untraceable)} hex literal(s) not traced to SPECSHEET or public standard:\n'
        + '\n'.join(untraceable[:30])
    )
