"""Decoder for the iteration-12i running-indicator rows (tools/wifi_ind_timeline.py)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import wifi_ind_timeline as t  # noqa: E402

LOG = ("[00:00:01.000] 12 A gate:0000F1035B0000000048000100FF82FF\n"
       "[00:00:01.000] 12 B gate:00480000000023CC000000370000000C\n"
       "[00:00:03.000] 2010 wifi: wl0 post=0\n"
       "[00:00:03.000] 2010 A mac0:000000007B00000000C80001009F827D\n"
       "[00:00:03.000] 2010 B mac0:00C80000000023CC000100370000000C\n")


def test_parse_pairs_rows_by_tag_in_log_order():
    s = t.parse(LOG)
    assert [tag for tag, _ in s] == ['gate', 'mac0']
    gate, mac0 = s[0][1], s[1][1]
    assert gate['wl0'] == 0xF103 and gate['wdma'] == 0x37 and gate['macsys'] == 0xC
    assert mac0['wl0'] == 0 and mac0['id'] == 0x7B000000 and mac0['c58'] == 0x009F827D


def test_table_decodes_the_five_indicator_bits():
    lines = t.table(t.parse(LOG)).splitlines()
    assert lines[0].split()[:4] == ['tag', 'WL_CTL0', 'ID29', 'CLK4.23']
    assert lines[1].startswith('vendr'), 'vendor target row right under the header'
    gate, mac0 = lines[2].split(), lines[3].split()
    assert gate[:4] == ['gate', 'F103', '0', '0'] and gate[7] == '0'
    assert mac0[:4] == ['mac0', '0', '1', '1'] and mac0[7] == '1'


def test_incomplete_pair_is_reported_not_dropped():
    s = t.parse("A rst:0000F1035B0000000048000100FF82FF\n")
    assert s == [] or 'incomplete' in t.table(s)
    s = t.parse("B rst:00480000000023CC000000370000000C\n")
    assert 'incomplete' in t.table(s)
