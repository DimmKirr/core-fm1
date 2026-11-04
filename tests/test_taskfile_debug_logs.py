"""Every `task debug:*` and `task logs*` run leaves its output in .scratch/debug/<name>.log.

The 2026-11-04 session lost a device status readout because `task logs:hello` printed to the
terminal only. This pins the convention: each debug task creates .scratch/debug, writes a dated
header line to its own log file, and tees every command's output into that file.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TASKFILE = ROOT / 'Taskfile.yml'


def _tasks():
    """{name: body} for every top-level task (two-space indent) in the Taskfile."""
    text = TASKFILE.read_text()
    text = text[text.index('\ntasks:\n'):]          # skip the includes block (debug:wifi is an include)
    found = {}
    for m in re.finditer(r'^  ([a-z][a-z0-9:_-]*):\n(.*?)(?=^  [a-z][a-z0-9:_-]*:\n|\Z)', text, re.M | re.S):
        found[m.group(1)] = m.group(2)
    return found


def _debug_tasks():
    return {n: b for n, b in _tasks().items() if n.startswith('debug:')}


def test_there_are_debug_tasks():
    assert len(_debug_tasks()) >= 7


def test_every_debug_task_tees_into_its_own_log():
    for name, body in _debug_tasks().items():
        log = f'.scratch/debug/{name.split(":", 1)[1]}.log'
        assert 'mkdir -p .scratch/debug' in body, f'{name}: must create .scratch/debug'
        assert f'=== {name} $(date -Iseconds) ===" | tee {log}' in body, f'{name}: dated header into {log}'
        cmd_lines = [l for l in body.splitlines()
                     if not l.strip().startswith('#') and ('uv run' in l or 'python' in l or 'curl' in l)]
        assert cmd_lines, f'{name}: no command line found'
        for line in cmd_lines:
            assert f'tee -a {log}' in line, f'{name}: command output not teed into {log}: {line.strip()}'


def test_logs_tasks_are_aliases_of_the_saving_debug_tasks():
    tasks = _tasks()
    for alias, target in (('logs', 'debug:follow'), ('logs:dump', 'debug:dump'), ('logs:hello', 'debug:hello')):
        assert alias in tasks, f'{alias} missing'
        assert f'task: {target}' in tasks[alias], f'{alias} must delegate to {target} so its output is saved'


def test_debug_section_is_commented():
    text = TASKFILE.read_text()
    assert re.search(r'^  # .*\.scratch/debug/<name>\.log', text, re.M), 'convention comment above the debug tasks'
