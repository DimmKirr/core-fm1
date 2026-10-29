#!/usr/bin/env python3
"""Unit tests for scheduler Kconfig and source structure."""
from pathlib import Path

import pytest
from kconfiglib import Kconfig, BOOL

ROOT = Path(__file__).resolve().parents[1]
KCONFIG = ROOT / 'Kconfig'


@pytest.fixture(autouse=True)
def skip_if_no_kconfig():
    if not KCONFIG.exists():
        pytest.skip('Kconfig not found')


@pytest.fixture
def kconf():
    return Kconfig(str(KCONFIG), warn=False)


def test_sched_kconfig_parses(kconf):
    s = kconf.syms['SCHED']
    assert s.type == BOOL


def test_sched_max_tasks_default(kconf):
    s = kconf.syms['SCHED_MAX_TASKS']
    assert s.str_value == '6'


def test_sched_source_exists():
    assert (ROOT / 'firmware' / 'src' / 'sched' / 'sched.c').exists()


def test_sched_ctx_asm_exists():
    assert (ROOT / 'firmware' / 'src' / 'sched' / 'ctx.S').exists()


def test_sched_included_in_unity_build():
    src = (ROOT / 'firmware' / 'src' / 'core_fm1.c').read_text()
    assert 'sched/sched.c' in src


def test_sched_has_required_functions():
    src = (ROOT / 'firmware' / 'src' / 'sched' / 'sched.c').read_text()
    assert 'sched_init(' in src
    assert 'sched_create(' in src
    assert 'sched_run(' in src
    assert 'yield(' in src
    assert 'sleep_ms(' in src


def test_sched_ctx_has_switch():
    src = (ROOT / 'firmware' / 'src' / 'sched' / 'ctx.S').read_text()
    assert 'sched_switch' in src


def test_sched_stack_canary():
    src = (ROOT / 'firmware' / 'src' / 'sched' / 'sched.c').read_text()
    assert '0xDEADBEEF' in src or 'SCHED_CANARY' in src


def test_sched_noop_stubs_without_config():
    """Non-scheduler builds must get no-op yield/sleep_ms stubs."""
    src = (ROOT / 'firmware' / 'src' / 'sched' / 'sched.c').read_text()
    else_section = src[src.index('#else'):]
    assert 'yield' in else_section
    assert 'sleep_ms' in else_section
