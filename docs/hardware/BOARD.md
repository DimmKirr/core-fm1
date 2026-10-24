# Hardware Reference: M-VAVE FM-1

## Board

Single-PCB design, no debug pads or recovery button exposed.

| Spec | Value |
|------|-------|
| PCB marking | `DX7 MB V07` |
| Dimensions | 161.5 x 96.5 x 28.6 mm |
| Weight | 251 g |
| Battery | 2000 mAh LiPo (DTP704060, 3.7V nominal), ~12h runtime |
| Charging | USB-C, pass-through supported |
| Speaker | Built-in mono, auto-mutes on headphone insert |

### ICs (photo-sourced, see [schematic](hardware/fm1.kicad_sch))

| Designator | Package | Marking | Role |
|------------|---------|---------|------|
| U1 | QFN-48 | JL C188760-11B8 | AC7911BA SoC (internal SiP: P25Q80H 1MB flash + 2MB SDRAM) |
| U2 | SOIC-16 | 74HC595D XJS 24G | 8-bit shift register (keyboard matrix columns / LED drive) |
| U3 | SOIC-16 | 74HC595D XJS 24G | 8-bit shift register (daisy-chained with U2) |
| U5 | SOT-23-5 | TBD | Voltage regulator (likely LDO) |
| U9 | QFN/SOIC | TBD | Power management (near L1 inductor, likely DC-DC) |
| U12 | SOP-8 | TBD | LiPo battery charger (TP4056 or equivalent) |
| FR7 | TSSOP | SLS3160 6253KD | Unidentified (possibly LED controller or key scanner) |
| Y1 | Ceramic SMD | OCIC P2362 2627 | 24.000 MHz crystal resonator |

### Controls

- 27 silicone keys (gold PCB contact pads, D1-D41 scanning diodes)
- 14 buttons with individual LEDs (LED01-LED55+, tactile switches K30, K36, K40, K41, K45, etc.)
- 7 rotary encoders + 1 master knob
- Pitch wheel and mod wheel (ADC-read)

### Connectors

| Port | Type | Function |
|------|------|----------|
| USB-C | USB 2.0 HS | Power, composite UAC1 audio + USB-MIDI |
| Headphone | 3.5mm TRS stereo | Audio output |
| MIDI In | 3.5mm TRS (Type A) | Hardware MIDI input |
| FPC | Ribbon cable | LCD display connection |
| Battery | Soldered red/black wires | LiPo battery |

### Display

240x240 RGB565 TFT LCD, driven over SPI1, connected via FPC ribbon cable.

### Power

4.7 uH inductor (marked "4R7") visible on board, likely part of DC-DC converter circuit.

## CPU: JieLi AC791N (WL82)

Manufactured by Zhuhai JieLi Technology Co., Ltd. (珠海杰理科技股份有限公司). Founded 2010, fabless IC design company headquartered in Zhuhai, Guangdong. IPO'd on Beijing Stock Exchange August 2026. Global leader in Bluetooth audio SoC: 5.686 billion units shipped 2023-2025.

### Core

| Spec | Value |
|------|-------|
| Architecture | pi32v2 (custom 32-bit, little-endian) |
| Cores | 2 |
| Clock | 240 MHz (PLL from 24 MHz crystal), max rated 320 MHz |
| Registers | 16 GPRs, 64-bit register pairs |
| FPU | Yes (hardware single-precision) |
| DSP/SIMD | Yes (MAC, saturating arithmetic, SIMD extensions) |
| Package | QFN-48, 6x6 mm, exposed pad |
| Temp range | -40 to +85 C |
| Supply | 2.0 - 5.5 V |

### pi32v2 Architecture

Custom 32-bit RISC-like ISA, derived from Analog Devices Blackfin lineage. Features:
- Little-endian byte order
- 16 general-purpose registers with 64-bit register pair support
- Hardware multiply-accumulate (MAC) for DSP workloads
- Saturating arithmetic instructions
- SIMD extensions for parallel data processing
- 32 KB instruction cache + 32 KB data cache

### Wireless

| Feature | Spec |
|---------|------|
| WiFi | 802.11 b/g/n |
| Bluetooth | 5.2 dual-mode (BR/EDR + BLE) |
| Coexistence | PTA (Packet Traffic Arbitration) |

FM-1 uses BLE for wireless MIDI (BLE MIDI profile).

### Integrated Peripherals

- 4x ADC + 2x DAC (16-bit)
- I2S audio interface
- SPI (multiple channels)
- USB 2.0 High Speed PHY
- Hardware AES-128/256, SHA-128/256, CRC
- GPIO: 28 pins

### Part Numbers

| Variant | Notes |
|---------|-------|
| AC7911BA | Base variant (JLCPCB: C42389637) |
| AC791N | Common reference in community firmware |
| WL82 | JieLi internal codename for this SoC family |

## Memory

### On-Chip

| Type | Size |
|------|------|
| SRAM | 578 KB |
| I-Cache | 32 KB |
| D-Cache | 32 KB |

### External Flash

| Type | Size | Interface |
|------|------|-----------|
| SPI NOR | 1 MB | SPI, XIP-mapped at 0x02000000 |

Used for firmware storage, projects, presets, and OTA updates. Firmware is lz4-compressed and signed (`.fwsc` format).

## IO / USB

### USB

| Spec | Value |
|------|-------|
| Standard | USB 2.0 High Speed |
| Connector | USB-C |
| Controller | MUSB-like register map |
| Device class | Composite: UAC1 audio + USB-MIDI |
| Power | Bus-powered charging + pass-through operation |

The FM-1 presents as a composite USB device:
1. **UAC1 Audio**: stereo audio output over USB (class-compliant, no drivers needed)
2. **USB-MIDI**: MIDI input/output over USB (class-compliant)

### USB Host / OTG

The AC791N USB controller supports three modes via the SDK's `otg_dev_data` structure:

| Mode | SDK constant | Description |
|------|-------------|-------------|
| Host | `OTG_HOST_MODE` | FM-1 acts as USB host |
| Device | `OTG_SLAVE_MODE` | FM-1 acts as USB peripheral (current default) |
| Charge | `OTG_CHARGE_MODE` | Charge detection only |

These modes can be combined for true OTG dual-role operation.

**Host-mode USB classes in SDK:** Mass Storage, HID, UAC (audio), UVC (video/camera).
**Device-mode USB classes in SDK:** Mass Storage, UAC, UVC, HID, CDC (virtual serial).

**FM-1 board-level status: unconfirmed.** Two open questions block host mode on the FM-1 hardware:

1. **VBUS supply**: USB-C host mode requires the FM-1 to supply 5V on VBUS. Unknown whether the board has a boost converter or load switch for this.
2. **CC resistors**: USB-C role detection depends on CC pin resistor configuration. The FM-1's CC wiring hasn't been examined.

A powered OTG adapter or USB hub could bypass both issues but is non-standard for Type-C. Community investigation is tracked in [Felucca #289](https://github.com/kmplngj/Felucca/issues/289).

### Audio Path

| Spec | Value |
|------|-------|
| Sample rate | 44.1 kHz |
| Channels | Stereo |
| Buffer | 128-sample DMA half-buffer |
| Codec interface | I2S |
| DAC resolution | 16-bit (integrated) |

### MIDI

| Transport | Interface |
|-----------|-----------|
| USB-MIDI | USB 2.0 composite device |
| BLE MIDI | Bluetooth Low Energy |
| TRS MIDI | 3.5mm Type A input |

## Sources

- [AL-255/FM-1-RE architecture](https://github.com/AL-255/FM-1-RE/blob/main/docs/architecture.md)
- [aroum/fm1-custom-fw](https://github.com/aroum/fm1-custom-fw)
- [kagaimiq JieLi pi32v2](https://kagaimiq.github.io/jielie/cpu/pi32v2.html)
- [kagaimiq JieLi chips](https://kagaimiq.github.io/jielie/chips/)
- [JieLi AC79 AIoT SDK (Gitee)](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK)
- [JLCPCB AC7911BA](https://jlcpcb.com/partdetail/JieLiTech-AC7911BA/C42389637)
- [altstrument FM-1](https://altstrument.org/devices/m-vave-fm-1)
- [Synth Anatomy FM-1 review](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)
