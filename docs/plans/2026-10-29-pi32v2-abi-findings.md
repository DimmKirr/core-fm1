# pi32v2 ABI Findings for Cooperative Scheduler

## 1. sp vs ssp: which stack does the main loop use?

**Answer: sp (user stack)**

`crt0.S:13-14` sets both:
```asm
sp = _ustack_top     ; 0x01C7A000
ssp = _sstack_top    ; 0x01C7C000
```
Then `call fm1_cstart` runs on `sp`. The main loop (`fm1_main`) runs on the user stack (sp). The supervisor stack (ssp) is the alternate stack pointer, not actively used by application code.

## 2. Callee-saved registers

**Answer: r4-r15 are callee-saved, r0-r3 are caller-saved**

Evidence from `fm1_isr.S`:
```asm
[--sp] = {psr, rets, reti}   ; save interrupt context
[--sp] = {r3-r0}             ; save ONLY r0-r3
call body                     ; C handler may clobber r0-r3
{r3-r0} = [sp++]             ; restore r0-r3
{psr, rets, reti} = [sp++]   ; restore interrupt context
```

ISRs only save r0-r3. This means r4-r15 MUST be preserved by called functions (callee-saved). The C compiler guarantees this for any function, so the ISR's call to a C body is safe without saving r4-r15.

fm1-base ctx.S confirms: it saves `rets + r4-r15` (13 words = 52 bytes) for context switch, which is exactly the callee-saved set + return address.

## 3. Stack alignment

fm1-base uses 8-byte aligned stack top. The 13-word initial frame (52 bytes) is placed below the aligned top. This matches JieLi clang 4.0.1 expectations.

## 4. ISR stack behavior

**Answer: ISRs push onto the interrupted task's sp**

The ISR macro (`fm1_isr.S:11`) uses `[--sp]` to push, which is the current user stack pointer. There is no stack switch to a dedicated ISR stack. This means:

- Each task stack must budget for ISR nesting headroom
- Audio ISR (priority 3) pushes 7 words = 28 bytes onto the interrupted task's sp
- Timer5 ISR (priority 4) can nest inside audio ISR, adding another 28 bytes
- Total ISR headroom per task: ~56 bytes worst case (both nested)

## 5. EMU stack guard strategy

**Decision: (C) Reconfigure EMU on context switch**

From `app.ld:69`:
```
_guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
_guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
```

The EMU guard window covers `[_ustack_lo, _sstack_top]`. Task stacks are allocated in POOL (0x01C20000-0x01C74000), which is outside this window.

Options:
- (A) Allocate in guard window: not enough space for 6 tasks
- (B) Widen guard: would need to cover entire POOL, too broad
- **(C) Reconfigure EMU per switch**: 4 register writes per context switch. Set EMU low/high to the current task's stack bounds. Cost: ~4 cycles per switch, negligible.

Software canaries (0xDEADBEEF paint, check bottom 8 bytes on yield) provide defense-in-depth independent of EMU. Both mechanisms will be active.

## Summary for scheduler implementation

| Question | Answer |
|----------|--------|
| Main loop stack | sp (user), 0x74100-0x7A000 (24KB) |
| Callee-saved | r4-r15 (rets saved explicitly) |
| Context switch frame | 13 words: rets + r4..r15 |
| ISR stack | Uses interrupted task's sp |
| ISR headroom per task | ~56 bytes (Timer5 + audio nested) |
| Stack alignment | 8-byte aligned top |
| EMU guard | Reconfigure per switch (option C) |
| Canaries | 0xDEADBEEF paint + check on yield |
