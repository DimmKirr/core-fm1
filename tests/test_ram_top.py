"""tools/ram_top.py: rank .data/.bss input sections from the linker map, so `task flash`
shows what fills the 96 KB RAM region before flashing."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ram_top  # noqa: E402

MAP = """
.text           0x0000000002001000     0x1000
 .text.foo      0x0000000002001000       0x10 build/core_fm1.o

.bss            0x0000000001c08040    0x17fa4 load address 0x000000000203c524
 .bss.wifi_tx_buf
                0x0000000001c08040     0x658 build/core_fm1.o
 .bss.small     0x0000000001c08698       0x4 build/core_fm1.o
 .bss.zero      0x0000000001c0869c       0x0 build/core_fm1.o
 .bss.sched_stacks
                0x0000000001c0869c    0x2000 build/core_fm1.o

.data           0x0000000001c1ffe4      0x100
 .data.table    0x0000000001c1ffe4      0x80 build/usb_sdk.o

.pool           0x0000000001c20000    0x140a8
 .pool          0x0000000001c20000    0x1000 build/core_fm1.o
"""


def test_parse_ranks_data_and_bss_sections_only():
    rows = ram_top.parse(MAP)
    assert [r.name for r in rows] == ['.bss.sched_stacks', '.bss.wifi_tx_buf', '.data.table', '.bss.small']
    assert rows[0].size == 0x2000 and rows[0].section == 'bss' and rows[0].obj == 'core_fm1.o'
    assert all(r.size > 0 for r in rows), 'zero-size sections dropped'
    assert not any(r.name.startswith('.pool') or r.name.startswith('.text') for r in rows)


def test_table_lists_top_n_with_total():
    out = ram_top.table(ram_top.parse(MAP), top=2)
    lines = out.splitlines()
    assert lines[0].startswith('.data+.bss:')
    assert '8192' in lines[0] + lines[1] + lines[2]
    assert 'sched_stacks' in lines[1] and 'wifi_tx_buf' in lines[2]
    assert len(lines) == 3
