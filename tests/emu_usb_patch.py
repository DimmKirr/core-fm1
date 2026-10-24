#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Patch the fm1-emu web files to expose memory scan for USB safety tests.

Creates a temporary directory with symlinks to the original emulator files,
plus patched worklet.js and main.js that add:
  - worklet: "peek" message -> reads N bytes from WASM memory
  - worklet: "scan" message -> finds a byte pattern in WASM memory
  - page:    window.fm1_peek(addr, len) -> Promise<Uint8Array>
  - page:    window.fm1_scan(pattern)   -> Promise<{found, offset}>
"""
import os
import shutil
import tempfile
from pathlib import Path

WORKLET_PATCH = '''
        else if (m.type === "peek" && this.x) {
          const buf = new Uint8Array(this.x.memory.buffer, m.addr, m.len).slice();
          this.port.postMessage({ type: "peek-result", id: m.id, data: buf }, [buf.buffer]);
        }
        else if (m.type === "scan" && this.x) {
          const mem = new Uint8Array(this.x.memory.buffer);
          const pat = new Uint8Array(m.pattern);
          let offset = -1;
          for (let i = 0; i <= mem.length - pat.length; i++) {
            let match = true;
            for (let j = 0; j < pat.length; j++) {
              if (mem[i + j] !== pat[j]) { match = false; break; }
            }
            if (match) { offset = i; break; }
          }
          this.port.postMessage({ type: "scan-result", id: m.id, found: offset >= 0, offset });
        }
'''

MAIN_PATCH = '''
// USB safety test hooks: peek/scan the emulator's WASM memory from Playwright
(() => {
  let _peekId = 0;
  const _peekWaiters = new Map();

  const origOnWorklet = onWorklet;
  onWorklet = function(e) {
    const m = e.data;
    if (m.type === "peek-result" || m.type === "scan-result") {
      const w = _peekWaiters.get(m.id);
      if (w) { _peekWaiters.delete(m.id); w(m); }
      return;
    }
    return origOnWorklet.call(this, e);
  };

  window.fm1_peek = (addr, len) => new Promise((resolve) => {
    const id = ++_peekId;
    _peekWaiters.set(id, (m) => resolve(Array.from(m.data)));
    node.port.postMessage({ type: "peek", id, addr, len });
  });

  window.fm1_scan = (pattern) => new Promise((resolve) => {
    const id = ++_peekId;
    _peekWaiters.set(id, (m) => resolve({ found: m.found, offset: m.offset }));
    node.port.postMessage({ type: "scan", id, pattern });
  });

  window.fm1_scan_all = (pattern) => new Promise((resolve) => {
    const id = ++_peekId;
    _peekWaiters.set(id, (m) => resolve({ found: m.found, offset: m.offset }));
    node.port.postMessage({ type: "scan", id, pattern });
  });
})();
'''


def create_patched_emu(emu_dir):
    """Create a temp directory with patched emulator files. Returns the path."""
    emu_dir = Path(emu_dir)
    tmp = Path(tempfile.mkdtemp(prefix='fm1-emu-usb-'))

    for item in emu_dir.iterdir():
        if item.name in ('worklet.js', 'main.js'):
            continue
        dst = tmp / item.name
        if item.is_dir():
            os.symlink(str(item), str(dst))
        else:
            os.symlink(str(item), str(dst))

    worklet_src = (emu_dir / 'worklet.js').read_text()
    insert_after = 'else if (m.type === "vel") this.x.fm1_velocity(m.us);'
    if insert_after not in worklet_src:
        insert_after = 'else if (m.type === "analog")'
        pos = worklet_src.find(insert_after)
        end = worklet_src.find('\n', worklet_src.find(';', pos))
        patched_worklet = worklet_src[:end + 1] + WORKLET_PATCH + worklet_src[end + 1:]
    else:
        pos = worklet_src.find(insert_after)
        end = worklet_src.find('\n', pos)
        patched_worklet = worklet_src[:end + 1] + WORKLET_PATCH + worklet_src[end + 1:]
    (tmp / 'worklet.js').write_text(patched_worklet)

    main_src = (emu_dir / 'main.js').read_text()
    # We need onWorklet to be reassignable. It's defined as `function onWorklet(e)`.
    # Replace with `let onWorklet = function(e)` so our patch can wrap it.
    main_src = main_src.replace('function onWorklet(e) {', 'let onWorklet = function(e) {', 1)
    patched_main = main_src + '\n' + MAIN_PATCH
    (tmp / 'main.js').write_text(patched_main)

    return tmp


def cleanup_patched_emu(path):
    """Remove the patched emulator directory."""
    shutil.rmtree(path, ignore_errors=True)
