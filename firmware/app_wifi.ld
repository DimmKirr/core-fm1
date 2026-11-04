/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WiFi variant memory map (CONFIG_WIFI_NONFREE with vendor binary driver).
 * RAM expanded to 152 KiB for BT+WiFi BSS plus core-fm1 audio/UI.  Vendor-specific sections
 * (.sram, .*_bss, .bd_base, .*_data, .*_const, vendor code sections)
 * are routed to the correct regions to prevent orphan placement in POOL.
 * Layout matches fm1-base/device/app_wifi.ld. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02001000, LENGTH = 0x8C000
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 152K
    POOL   (rwx) : ORIGIN = 0x01C2E000, LENGTH = 0x46000
    NOINIT (rw)  : ORIGIN = 0x01C7C020, LENGTH = 0x3D30
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*)
              *(.bt_stack_code .ble_sm_code .hci_controller_code .ble_hci_code .ble_att_code .ble_db_code
                .classic_lmp_code .link_bulk_code .classic_rf_code .lmp_irq_code .ble_gatt_code .ble_ll_code
                .ble_rf_code .bt_rf_code .hci_interface_code .vendor_manager_code .device_manager_code)
              *(.*_const)
              *(.crypto_code)
              . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) *(.volatile_ram_code) *(*.rf.text.cache.L2.rf_trim)
                  . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .dsp_text : { _dsp_start = .; *(.dsp_text*) . = ALIGN(4); _dsp_end = .; } > RAMTEXT AT > XIP
    _dsp_load = LOADADDR(.dsp_text);
    .dsp_tables : { _dt_start = .; *(.dsp_tables*) . = ALIGN(4); _dt_end = .; } > RAMTEXT AT > XIP
    _dt_load = LOADADDR(.dsp_tables);
    _resource_low_start = ALIGN(_dt_end, 4);
    _resource_low_end = 0x01C07F00;
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) *(.*_data) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) *(.*_bss) *(.bd_base)
                      *(.sram)
                      . = ALIGN(4); _bss_end = .; } > RAM
    _resource_data_start = _bss_end;
    _resource_data_end = ORIGIN(RAM) + LENGTH(RAM);
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) *(.memp_memory_x) . = ALIGN(4); _pool_end = .; } > POOL
    .audio_text : { _audio_start = .; *(.audio_text*) . = ALIGN(4); _audio_end = .; } > POOL AT > XIP
    _audio_load = LOADADDR(.audio_text);
    _guard_ram_end = SIZEOF(.audio_text) ? _audio_end : _dsp_end;
    .cpu1_stacks (NOLOAD) : {
        . = ALIGN(32); _cpu1_stacks_lo = .;
        KEEP(*(.cpu1_stacks))
        _cpu1_stacks_hi = .; _resource_start = .;
    } > POOL
    _cpu1_ustack_top = _cpu1_stacks_lo + 4096;
    _cpu1_sstack_top = _cpu1_stacks_lo + 8192;
    _resource_end = ORIGIN(POOL) + LENGTH(POOL);
    AUDIO_RESIDENT = 1;
    _resource_minimum = 0;
    ASSERT(_resource_end - _resource_start >= _resource_minimum, "audio working arena below worst-case budget")
    .noinit (NOLOAD) : { *(.noinit*) . = ALIGN(4); _noinit_end = .; } > NOINIT
    .audio_scratch (NOLOAD) : { . = ALIGN(32); *(.audio_scratch*) . = ALIGN(4); _audio_scratch_end = .; } > NOINIT
    _resource_retained_start = _audio_scratch_end;
    _resource_retained_end = ORIGIN(NOINIT) + LENGTH(NOINIT);
    _resource_cache_start = ORIGIN(CACHE);
    _resource_cache_end = ORIGIN(CACHE) + LENGTH(CACHE);
    ASSERT(_resource_low_start <= _resource_low_end, "RAM code overlaps startup STATUS")
    ASSERT(_resource_data_start <= _resource_data_end, "data overlaps resource pool")
    ASSERT(_resource_retained_start <= _resource_retained_end, "retained state overlaps boot records")
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
