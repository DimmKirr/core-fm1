"""Diff of two register sweeps (tools/wifi_sweep_diff.py)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import wifi_sweep_diff as d  # noqa: E402

CORE = "junk\n0x00030000 = 0x00000001\n0x00030f04 = 0x00000037\n0x00031200 = 0x00030f00\n"
VENDOR = "=== MMIO ===\n0x00030000 = 0x00000001\n0x00030f04 = 0x00010037\n0x00031208 = 0x00000005\n"


def test_parse_accepts_both_dumps():
    assert d.parse(CORE) == {0x30000: 1, 0x30F04: 0x37, 0x31200: 0x30F00}
    assert d.parse(VENDOR)[0x31208] == 5


def test_diff_lists_differences_and_one_sided_rows_only():
    rows = d.diff(d.parse(CORE), d.parse(VENDOR))
    assert rows == [(0x30F04, 0x37, 0x10037), (0x31200, 0x30F00, None), (0x31208, None, 5)]


def test_table_names_known_rt2800_registers():
    out = d.table(d.diff(d.parse(CORE), d.parse(VENDOR)))
    assert 'MAC_STATUS_CFG' in out or 'MAC_STATUS' in out
    assert 'xor=0x00010000' in out
