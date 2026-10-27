# AC791N (pi32v2) IP Block Ancestry

Research date: 2026-10-27

Identifies which established architectures JieLi used as inspiration or licensed IP
for the AC791N SoC. Sources: fm1-emu codebase analysis, core-fm1 hardware docs,
kagaimiq reverse-engineering documentation, Quarkslab teardowns, web research.

---

## 1. CPU ISA: Analog Devices Blackfin — Confirmed Lineage

### Evolution

JieLi's CPU architecture evolved through three generations:

1. **Licensed Blackfin cores** (earlier chips: AC510N/DV10, AC520N/DV12, AC460N/BT15)
2. **pi32** — custom Blackfin-derived ISA (ELF machine type 0xF0). Described as
   "heavily based off Blackfin" by kagaimiq.
3. **pi32v2** — enhanced pi32 with additions from ARM (ELF machine type 0xF1).
   Used in AC791N (WL82), the FM-1's SoC.

### Inherited Blackfin Features (confirmed in fm1-emu)

| Feature | Blackfin | pi32v2 | Evidence |
|---------|----------|--------|----------|
| System registers | RETI, RETE, RETX, RETS, RETN | reti, rete, retx, rets, retn | fm1-core/src/lib.rs register names |
| Interrupt config | SYSCFG, IMASK, ILAT, IPEND | icfg (sr11) | fm1-core/src/lib.rs:266-267 |
| Interrupt enable | CLI / STI | cli / sti | fm1-core/src/lib.rs |
| Nested interrupts | Push RETI to stack enables nesting, pop disables | Same rule | fm1-core/src/lib.rs:274 "Blackfin rule" |
| Zero-overhead loops | LC0/LT0/LB0 triple registers | `rep N` block (simplified) | fm1-core/src/lib.rs:190-199 |
| Parallel execution | `ins1 \|\| ins2` | `ins1 # ins2` | fm1-isa instruction pairs |
| GPRs | 16 (R0-R15) | 16 (r0-r15) | fm1-core register file |
| Register pairs | 64-bit (R1:R0 etc.) | 64-bit register pairs | BOARD.md |
| DSP | MAC, saturating arithmetic | MAC, saturating arithmetic, SIMD | BOARD.md |
| Dual-core | BF561 (dual Blackfin) | Dual pi32v2 cores | BOARD.md, fm1-core |
| Instruction width | Variable (16/32/64-bit) | Variable (16/32/48-bit) | fm1-isa |
| Endianness | Little-endian | Little-endian | BOARD.md |

### What pi32v2 Changed from Blackfin

- Simplified hardware loops: single `rep N rR { block }` replaces the LC/LT/LB triple
- Different instruction encodings (not binary-compatible)
- Added SIMD extensions beyond Blackfin's original set
- ARM-influenced additions (details not fully catalogued)
- ~1067 instruction classes vs Blackfin's ~80 base + extensions

### Significance

The Blackfin ISA manual is publicly available from Analog Devices. This means:
- Blackfin documentation can serve as a reference for understanding pi32v2 semantics
- The fm1-emu's pi32v2 implementation represents "Blackfin + deltas"
- For QEMU porting, the work is "implement Blackfin-derived core" not "implement unknown ISA"

---

## 2. USB Controller: Mentor Graphics MUSB — Confirmed

### Evidence

`fm1-emu/crates/fm1-soc/src/usb.rs`:
> "The controller is a Mentor MUSB core (the classic FS byte map) behind JieLi's register"

### Context

MUSB (Inventra MUSBMHDRC) is one of the most widely licensed USB OTG IP blocks:

| Vendor | Chips | Notes |
|--------|-------|-------|
| Texas Instruments | AM335x (BeagleBone), OMAP3/4 | Direct Mentor license |
| Allwinner | A10, A13, A20, A31, H3 | Scrambled register addresses |
| MediaTek | Various MT67xx, MT81xx | Via MTK_MUSB driver |
| **JieLi** | **AC791N (WL82)** | **Custom register remap** |

JieLi follows the Allwinner pattern: license the Mentor USB IP, wrap it behind
a vendor-specific register address layer. The underlying protocol engine is
standard MUSB.

### FM-1 USB capabilities

- USB 2.0 High Speed (480 Mbps)
- Device mode: composite UAC1 audio + USB-MIDI (current FM-1 use)
- Host mode: mass storage, HID, UAC, UVC (SDK supports; FM-1 hardware unconfirmed)
- OTG dual-role capable (SDK supports)

---

## 3. WiFi MAC/BBP: Ralink RT2x00 Family — High Confidence

### Evidence

**Direct statement** in `core-fm1/docs/hardware/WIFI.md` section 7.5:
> "The 'USB' naming is a Linux driver abstraction layer carried from the Ralink
> codebase, repurposed for the internal SDIO bus"

**Architectural fingerprints** matching Ralink RT2800:

| Feature | Ralink RT2800 | AC791N WiFi | Match? |
|---------|--------------|-------------|--------|
| BBP indirect access | Single window via BBP_CSR_CFG | 4 windows via 0x3101C (C, C', D, D') | Extended version |
| BBP write protocol | Write addr, write data, set start bit | Same: addr reg, data reg, commit reg | Yes |
| RF-die programming | SPI via RF_CSR_CFG | SPI via 0x14028/0x1402C | Yes |
| LUT tables | RF frequency/power LUTs | 256-entry RF-die LUT (2 words/entry) | Yes |
| Host interface | SDIO/USB/PCIe variants | Internal SDIO bus (CMD52/CMD53) | Yes |
| Driver abstractions | Linux wireless stack (sk_buff, netif) | Linux shim layer (sk_buff, netif, tasklet) | Yes |
| TX/RX naming | URB-based (USB variant) | "URB-like abstraction" over SDIO | Yes |
| Calibration | Per-channel power/frequency trim | VCO scan, PA trim, BBP DC/IQ trim | Similar |

### Interpretation

Two possibilities:
1. **Licensed IP**: JieLi licensed WiFi MAC/BBP IP from Ralink/MediaTek (MediaTek
   acquired Ralink in 2011; JieLi added WiFi in later chip generations)
2. **Architectural reference**: JieLi built their own WiFi engine but used the
   RT2x00 architecture and open-source driver as the design template

The presence of Ralink driver abstractions in the vendor binary strongly suggests
option 1 (licensed IP with the reference driver adapted), but option 2 cannot be
ruled out.

### Significance for clean-room WiFi driver

The RT2x00 GPL driver in the Linux kernel (`drivers/net/wireless/ralink/rt2x00/`)
is a valid architectural reference for implementing the AC791N WiFi driver, as
acknowledged in `core-fm1/docs/hardware/WIFI_RE_PROCESS.md`.

---

## 4. Radio RF: Possibly MediaTek-derived — Medium Confidence

### Evidence

- The WiFi radio shares architectural patterns with Ralink designs (see section 3)
- The BLE radio was reverse-engineered from scratch (documented in BLE-HW-FACTS.md)
  and does not obviously match any known BLE radio IP
- JieLi may have designed the shared 2.4 GHz analog front-end in-house, while
  licensing the digital WiFi MAC/BBP

### Shared radio architecture

The AC791N uses a single radio front-end for both Bluetooth and WiFi:
- Common PLL/VCO, PA, LNA, ADC/DAC
- Hardware priority controller (RFPRIO at 0x05E/0x060) arbitrates access
- WiFi analog init is required before BLE can start (shared dependency)

---

## 5. SoC Peripherals — Industry Standard

| Peripheral | Likely IP source | Notes |
|-----------|-----------------|-------|
| SPI flash (XIP) | In-house or generic IP | Standard for all Chinese MCUs |
| I2S audio | In-house or generic IP | Common audio SoC peripheral |
| Dual 16-bit ADC/DAC | In-house | Integrated audio codec |
| Hardware AES/SHA/CRC | Generic IP or in-house | Standard security accelerators |
| DMA controller | In-house | Vendor-specific |
| Timer/PWM | In-house | Standard |
| GPIO | In-house | 28 pins, standard |

No strong evidence of specific licensed peripheral IP beyond USB and WiFi.

---

## 6. Competitive Landscape

Other Chinese Bluetooth audio SoCs for comparison:

| Vendor | Example SoC | CPU | USB IP | WiFi IP | Notes |
|--------|-------------|-----|--------|---------|-------|
| **JieLi** | AC791N | pi32v2 (Blackfin-derived) | Mentor MUSB | Ralink-derived | FM-1 SoC |
| Actions Semi | ATS2835 | MIPS-based | Unknown | N/A (BT only) | Competing BT audio SoC |
| Bestechnic | BES2600 | ARM Cortex-M33 | Unknown | Unknown | AirPods competitor |
| Realtek | RTL8763E | ARM Cortex-M4 | Unknown | N/A | BLE audio |
| Qualcomm (CSR) | QCC5141 | Kalimba DSP + ARM | Synopsys DWC | Qualcomm | High-end TWS |
| Airoha (MediaTek) | AB1568 | ARM Cortex-M4F | Unknown | N/A | TWS earbuds |

JieLi is unique in using a Blackfin-derived CPU rather than ARM or MIPS. This likely
reflects their early licensing deal with ADI and subsequent investment in custom
toolchains (pi32 compiler, linker, debugger).

---

## 7. QEMU Porting Implications

The ancestry analysis changes the QEMU porting picture:

| Component | Without ancestry knowledge | With ancestry knowledge |
|-----------|--------------------------|----------------------|
| CPU target | "Novel proprietary ISA, 1067 instruction classes" | "Blackfin-derived ISA, public Blackfin manual as reference" |
| USB peripheral | "Unknown USB controller" | "Mentor MUSB: QEMU already has MUSB emulation (hw/usb/hcd-musb.c)" |
| WiFi peripheral | "Completely unknown WiFi engine" | "RT2x00-like: Linux kernel driver + BBP/RF architecture well-documented" |

QEMU already has:
- **MUSB emulation** in `hw/usb/hcd-musb.c` (can be adapted)
- No Blackfin target (but public ISA docs exist)
- No RT2x00 device model (but the Linux driver documents the register interface)

This doesn't reduce effort dramatically but provides a much clearer path for
each component.

---

## 8. Existing Blackfin Emulators and Tools (Open Source)

Several open-source Blackfin emulators and tools exist that could be used as
references, spike tests, or starting points for pi32v2 work.

### 8.1 GNU Blackfin Simulator (GDB sim/bfin/)

The authoritative Blackfin instruction-level simulator. ~45k LOC C, contributed
by Analog Devices to the GNU project. Simulates the full ISA including CEC
(interrupt controller), MMU, L1 SRAM, UART, SIC. Achieves ~3 MIPS on modern
hardware. GPL-3.0+.

**Use for pi32v2**: Extract instruction decoder tables and diff against fm1-isa's
encoding patterns. The instruction semantics in `sim/bfin/bfin-sim.c` are the
ground truth for what Blackfin does; comparing against fm1-core shows exactly
where pi32v2 diverges.

- Source: GDB tree `sim/bfin/` directory
- Decoder header: https://course.khoury.northeastern.edu/cs3650/parent/gdb-12-src/gdb-12.1/sim/bfin/bfin-sim.h
- Instruction list: https://course.khoury.northeastern.edu/cs3650/parent/gdb-12-src/gdb-12.1/sim/bfin/insn_list.def
- Docs: https://blackfin.uclinux.org/doku.php?id=toolchain%3Asim

### 8.2 bcore: Blackfin JIT Recompiler

Lifts Blackfin ELF32 executables to LLVM IR and executes via LLJIT. Decodes
16-bit and 32-bit instructions using a template-based binary pattern matcher.
JIT-compiles one basic block at a time with caching. Emulates the Core Event
Controller (CEC): interrupts, exceptions, supervisor/user mode transitions.

Built for the **Teenage Engineering OP-1 synthesizer emulation** (op1emu project).
This is a direct parallel: another music hardware device running on Blackfin,
being emulated with a JIT approach, just like fm1-emu does for the FM-1's
Blackfin-derived pi32v2.

- Repository: https://github.com/op1emu/bcore
- Parent project: https://github.com/op1emu

### 8.3 Ghidra Blackfin Extension

Full decoder and p-code semantics for the classic Blackfin ISA (ADSP-BF53x).
Decodes the whole instruction set including multi-issue packets and hardware
loops. Verified against GNU objdump and the GNU simulator. Useful for static
comparison of instruction encodings between Blackfin and pi32v2.

- Repository: https://github.com/aleph-void/ghidra-blackfin

### 8.4 cdj2000-emulator

Boots Pioneer CDJ-2000 firmware on a patched GNU Blackfin simulator (BF531 for
the display board) plus a QEMU SH-4 board. Another music hardware emulation
project. Includes `cdj-gui-run`, a fast Blackfin GUI simulator.

- Repository: https://github.com/cdj2k-revival/cdj2000-emulator
- Fast GUI sim PR: https://github.com/cdj2k-revival/cdj2000-emulator/pull/37

### 8.5 QEMU Blackfin Port (vapier)

An out-of-tree QEMU port for Blackfin by Mike Frysinger (vapier), the original
Blackfin Linux port maintainer. Not mainlined into QEMU but demonstrates the
feasibility of a Blackfin TCG frontend.

### 8.6 Spike Test Strategy

To validate the Blackfin-pi32v2 connection using these tools:

1. **Encoding comparison** (quickest): Extract the GNU sim's `insn_list.def`
   opcode patterns and compare against fm1-isa's `generated_isa.rs` bit fields.
   Shared encoding structure = confirmed decoder lineage.

2. **Semantic comparison**: Compare bcore's LLVM IR lifter output for common
   Blackfin instructions against fm1-core's interpreter/JIT for the equivalent
   pi32v2 instructions. Same semantics with different encodings = confirmed
   ISA evolution (not just inspiration).

3. **Cross-project learnings**: bcore (OP-1 emulator) faces the same challenge
   as fm1-emu but for actual Blackfin. Their JIT architecture decisions,
   CEC emulation approach, and instruction decoder design are directly
   comparable.

---

## Sources

- fm1-emu codebase: `crates/fm1-core/src/lib.rs`, `crates/fm1-soc/src/usb.rs`
- core-fm1 docs: `docs/hardware/BOARD.md`, `docs/hardware/WIFI.md`, `docs/hardware/WIFI_RE_PROCESS.md`
- kagaimiq JieLi documentation: https://kagaimiq.github.io/jielie/cpu/pi32v2.html
- Quarkslab JieLi teardown research
- Linux kernel: `drivers/net/wireless/ralink/rt2x00/`, `drivers/usb/musb/`
- Analog Devices Blackfin Processor Programming Reference (public)
- GNU Blackfin Simulator: https://blackfin.uclinux.org/doku.php?id=toolchain%3Asim
- bcore Blackfin JIT: https://github.com/op1emu/bcore
- Ghidra Blackfin: https://github.com/aleph-void/ghidra-blackfin
- cdj2000-emulator: https://github.com/cdj2k-revival/cdj2000-emulator
- Blackfin GitHub topic: https://github.com/topics/blackfin
