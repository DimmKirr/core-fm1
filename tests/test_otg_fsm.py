# SPDX-License-Identifier: GPL-3.0-only
"""OTG dual-role state machine tests.

Pure Python: validates FSM transitions, timing, debounce, and register
ordering invariants. No C compilation or hardware needed.
"""
import pytest


# ---------------------------------------------------------------------------
# Constants mirroring cfm1_otg.h
# ---------------------------------------------------------------------------
OTG_DEVICE = 0
OTG_HOST = 1

# IO_CON0 bit masks
IO_PULLUP_DP = 0x40     # D+ pull-up (device attach signal)
IO_PULLDOWN_DM = 0x10   # D- pull-down (host detection)

# Debounce / timeout thresholds (ms)
DEVICE_IDLE_THRESHOLD = 100   # frame_same ticks (each ~10ms = ~1s)
DEVICE_DEBOUNCE_MS = 500
HOST_REMOVAL_MS = 2000


# ---------------------------------------------------------------------------
# FSM model (mirrors the C implementation we'll write)
# ---------------------------------------------------------------------------
class OtgFsm:
    """OTG dual-role state machine.

    Inputs each tick: now_ms, usb_up, usb_config, frame_same,
    usb_resets, host_dev_status.
    Outputs: mode transitions + HW action log for register ordering tests.
    """

    def __init__(self, usb_mode=1):
        self.mode = OTG_DEVICE
        self.debounce_ms = 0
        self.idle_ms = 0
        self.actions = []  # [(action_name, *args), ...] for register tests
        self.usb_mode = usb_mode  # 0=MIDI (device only), 1=OTG (dual-role)

    def poll(self, now_ms, usb_up, usb_config, frame_same,
             host_dev_status, usb_resets=0, vbus_present=False):
        if self.mode == OTG_DEVICE:
            self._poll_device(now_ms, usb_up, usb_config, frame_same,
                              usb_resets, vbus_present)
        elif self.mode == OTG_HOST:
            self._poll_host(now_ms, host_dev_status)

    def _poll_device(self, now_ms, usb_up, usb_config, frame_same,
                     usb_resets, vbus_present):
        if self.usb_mode == 0:
            should_switch = False
        elif vbus_present:
            should_switch = False
        elif not usb_up:
            should_switch = False
        elif usb_config:
            # Was enumerated: detect disconnect via frame freeze
            should_switch = frame_same >= DEVICE_IDLE_THRESHOLD
        elif usb_resets > 0:
            # Host sent bus reset but hasn't configured yet: wait
            should_switch = False
        else:
            # SIE alive, no resets, no config: no host present
            should_switch = True
        # Active config with live SOF: host is here, stay in device mode
        if usb_config and frame_same < DEVICE_IDLE_THRESHOLD:
            should_switch = False

        if should_switch:
            if self.debounce_ms == 0:
                self.debounce_ms = now_ms
            elif now_ms - self.debounce_ms >= DEVICE_DEBOUNCE_MS:
                self._switch_to_host()
        else:
            self.debounce_ms = 0

    def _poll_host(self, now_ms, host_dev_status):
        if not host_dev_status:
            if self.idle_ms == 0:
                self.idle_ms = now_ms
            elif now_ms - self.idle_ms >= HOST_REMOVAL_MS:
                self._switch_to_device()
        else:
            self.idle_ms = 0

    def _switch_to_host(self):
        self.actions.append(('usb_detach',))
        self.actions.append(('io_con0_clear', IO_PULLUP_DP))
        self.actions.append(('delay_ms', 50))
        self.actions.append(('usb_host_init',))
        self.actions.append(('io_con0_set', IO_PULLDOWN_DM))
        self.mode = OTG_HOST
        self.debounce_ms = 0
        self.idle_ms = 0

    def _switch_to_device(self):
        self.actions.append(('usb_host_deinit',))
        self.actions.append(('io_con0_clear', IO_PULLDOWN_DM))
        self.actions.append(('usb_start',))
        self.mode = OTG_DEVICE
        self.debounce_ms = 0
        self.idle_ms = 0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def action_index(actions, name):
    """Index of first action matching name, or -1."""
    for i, a in enumerate(actions):
        if a[0] == name:
            return i
    return -1


def run_ticks(fsm, start_ms, count, interval_ms=10, **kwargs):
    """Run FSM for `count` ticks at `interval_ms` apart."""
    defaults = dict(usb_up=False, usb_config=False, frame_same=0,
                    host_dev_status=False, usb_resets=0,
                    vbus_present=False)
    defaults.update(kwargs)
    for i in range(count):
        fsm.poll(start_ms + i * interval_ms, **defaults)


# ---------------------------------------------------------------------------
# Phase 1: Basic state identity
# ---------------------------------------------------------------------------
class TestBootState:

    def test_boots_in_device_mode(self):
        fsm = OtgFsm()
        assert fsm.mode == OTG_DEVICE

    def test_no_actions_at_boot(self):
        fsm = OtgFsm()
        assert fsm.actions == []


# ---------------------------------------------------------------------------
# Phase 2: Device -> Host transition
# ---------------------------------------------------------------------------
class TestDeviceToHost:

    def test_no_transition_while_configured_and_active(self):
        """config=1 + frame_same=0 means host is actively talking: stay."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=True,
                  frame_same=0)
        assert fsm.mode == OTG_DEVICE

    def test_configured_then_disconnect_via_frame_freeze(self):
        """config=1 but frame_same high = cable pulled. Must switch."""
        fsm = OtgFsm()
        # 60 ticks at 10ms = 600ms > 500ms debounce
        run_ticks(fsm, 1000, 60, usb_up=True, usb_config=True,
                  frame_same=150)
        assert fsm.mode == OTG_HOST

    def test_never_configured_switches_after_debounce(self):
        """SIE up but config=0 (no host): switch after debounce.
        This is the OTG-hub-at-boot scenario."""
        fsm = OtgFsm()
        # 60 ticks at 10ms = 600ms > 500ms debounce
        run_ticks(fsm, 1000, 60, usb_up=True, usb_config=False,
                  frame_same=0)
        assert fsm.mode == OTG_HOST

    def test_no_transition_when_usb_down(self):
        """usb.up=0 means SIE not running: don't trust anything."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 200, usb_up=False, usb_config=False,
                  frame_same=200)
        assert fsm.mode == OTG_DEVICE

    def test_configured_no_transition_below_frame_threshold(self):
        """config=1, frame_same < 100: host might just be slow."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=True,
                  frame_same=99)
        assert fsm.mode == OTG_DEVICE

    def test_transition_unconfigured_after_debounce(self):
        """Unconfigured path: switch after debounce only."""
        fsm = OtgFsm()
        # 60 ticks at 10ms = 600ms > 500ms debounce
        run_ticks(fsm, 1000, 60, usb_up=True, usb_config=False,
                  frame_same=150)
        assert fsm.mode == OTG_HOST

    def test_debounce_prevents_early_switch(self):
        """Must sustain idle for 500ms. 400ms is not enough."""
        fsm = OtgFsm()
        # 40 ticks at 10ms = 400ms < 500ms debounce
        run_ticks(fsm, 1000, 40, usb_up=True, usb_config=False,
                  frame_same=150)
        assert fsm.mode == OTG_DEVICE

    def test_debounce_resets_on_recovery(self):
        """If SOF resumes mid-debounce, reset the debounce timer."""
        fsm = OtgFsm()
        # Start debounce: 30 ticks of idle (unconfigured path)
        run_ticks(fsm, 1000, 30, usb_up=True, usb_config=False,
                  frame_same=0)
        assert fsm.mode == OTG_DEVICE
        assert fsm.debounce_ms != 0

        # Host appears and configures us
        run_ticks(fsm, 1300, 5, usb_up=True, usb_config=True,
                  frame_same=0)
        assert fsm.debounce_ms == 0

        # Host drops config but SOF still running: debounce restarts
        run_ticks(fsm, 1350, 40, usb_up=True, usb_config=False,
                  frame_same=0)
        assert fsm.mode == OTG_DEVICE  # only 400ms, not enough

    def test_no_switch_during_enumeration(self):
        """Host sent bus reset but hasn't configured yet: wait for it."""
        fsm = OtgFsm()
        # SIE up, no config, but host sent a reset: enumeration in progress
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=False,
                  frame_same=0, usb_resets=1)
        assert fsm.mode == OTG_DEVICE

    def test_switch_after_enumeration_stalls(self):
        """Host sent reset but never configures: eventually switch.
        Reset count stays at 1 but host never sends SET_CONFIGURATION.
        Once reset count is > 0, we hold. But if config never comes,
        the user must replug. (This is the conservative choice.)"""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 500, usb_up=True, usb_config=False,
                  frame_same=0, usb_resets=1)
        # Stays in device because resets > 0 means someone tried
        assert fsm.mode == OTG_DEVICE

    def test_resets_zero_at_boot_allows_otg_switch(self):
        """Fresh boot with OTG hub: resets=0, config=0 -> switch."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0, usb_resets=0)
        assert fsm.mode == OTG_HOST


# ---------------------------------------------------------------------------
# Phase 3: Host -> Device transition
# ---------------------------------------------------------------------------
class TestHostToDevice:

    @pytest.fixture
    def host_fsm(self):
        """FSM already in host mode, idle timer clean."""
        fsm = OtgFsm()
        # Force transition via unconfigured path
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)
        assert fsm.mode == OTG_HOST
        fsm.actions.clear()
        fsm.idle_ms = 0  # clean slate: no idle time accumulated during setup
        return fsm

    def test_host_stays_while_device_present(self, host_fsm):
        run_ticks(host_fsm, 2000, 500, host_dev_status=True)
        assert host_fsm.mode == OTG_HOST

    def test_host_to_device_after_removal_timeout(self, host_fsm):
        """2 seconds with no device -> return to device mode."""
        # 210 ticks at 10ms = 2100ms > 2000ms
        run_ticks(host_fsm, 2000, 210, host_dev_status=False)
        assert host_fsm.mode == OTG_DEVICE

    def test_host_no_return_before_timeout(self, host_fsm):
        """1.5s with no device is not enough."""
        run_ticks(host_fsm, 2000, 150, host_dev_status=False)
        assert host_fsm.mode == OTG_HOST

    def test_host_idle_resets_on_reattach(self, host_fsm):
        """Device removed then reattached resets the removal timer."""
        # 150 ticks no device
        run_ticks(host_fsm, 2000, 150, host_dev_status=False)
        assert host_fsm.mode == OTG_HOST
        assert host_fsm.idle_ms != 0

        # Device reappears
        run_ticks(host_fsm, 3500, 5, host_dev_status=True)
        assert host_fsm.idle_ms == 0

        # Disappears again: needs full 2s from scratch
        run_ticks(host_fsm, 3550, 150, host_dev_status=False)
        assert host_fsm.mode == OTG_HOST  # only 1.5s


# ---------------------------------------------------------------------------
# Phase 4: Rapid cycling (stress)
# ---------------------------------------------------------------------------
class TestRapidCycling:

    def test_ten_transitions_no_crash(self):
        """Rapid role switches don't corrupt state."""
        fsm = OtgFsm()
        t = 0
        for _ in range(5):
            # Device -> Host (unconfigured path)
            run_ticks(fsm, t, 60, usb_up=True, usb_config=False,
                      frame_same=0)
            assert fsm.mode == OTG_HOST
            t += 600

            # Host -> Device
            run_ticks(fsm, t, 210, host_dev_status=False)
            assert fsm.mode == OTG_DEVICE
            t += 2100

        assert fsm.mode == OTG_DEVICE

    def test_switching_state_is_transient(self):
        """FSM never exposes an intermediate state between polls."""
        fsm = OtgFsm()
        modes_seen = set()
        for i in range(200):
            fsm.poll(i * 10, usb_up=True, usb_config=False,
                     frame_same=0, host_dev_status=False)
            modes_seen.add(fsm.mode)
        # Only OTG_DEVICE and OTG_HOST, never a third state
        assert modes_seen <= {OTG_DEVICE, OTG_HOST}


# ---------------------------------------------------------------------------
# Phase 5: Register ordering invariants
# ---------------------------------------------------------------------------
class TestRegisterOrdering:

    def test_pullup_cleared_before_host_init(self):
        """D+ pull-up must be off before host SIE starts."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)
        assert fsm.mode == OTG_HOST

        idx_clear = action_index(fsm.actions, 'io_con0_clear')
        idx_host = action_index(fsm.actions, 'usb_host_init')
        assert idx_clear >= 0
        assert idx_host >= 0
        assert idx_clear < idx_host

    def test_detach_before_host_init(self):
        """Device SIE must be stopped before host SIE starts."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)

        idx_detach = action_index(fsm.actions, 'usb_detach')
        idx_host = action_index(fsm.actions, 'usb_host_init')
        assert idx_detach >= 0
        assert idx_host >= 0
        assert idx_detach < idx_host

    def test_host_deinit_before_device_start(self):
        """Host SIE must be closed before device SIE starts."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)
        fsm.actions.clear()

        run_ticks(fsm, 1000, 210, host_dev_status=False)
        assert fsm.mode == OTG_DEVICE

        idx_deinit = action_index(fsm.actions, 'usb_host_deinit')
        idx_start = action_index(fsm.actions, 'usb_start')
        assert idx_deinit >= 0
        assert idx_start >= 0
        assert idx_deinit < idx_start

    def test_pulldown_cleared_before_device_start(self):
        """D- pull-down must be off before device mode re-enables pull-up."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)
        fsm.actions.clear()

        run_ticks(fsm, 1000, 210, host_dev_status=False)

        idx_clear = action_index(fsm.actions, 'io_con0_clear')
        idx_start = action_index(fsm.actions, 'usb_start')
        assert idx_clear >= 0
        assert idx_start >= 0
        assert idx_clear < idx_start

    def test_delay_between_detach_and_host_init(self):
        """Bus settle delay between device teardown and host init."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0)

        idx_delay = action_index(fsm.actions, 'delay_ms')
        idx_host = action_index(fsm.actions, 'usb_host_init')
        assert idx_delay >= 0
        assert idx_host >= 0
        assert idx_delay < idx_host
        assert fsm.actions[idx_delay] == ('delay_ms', 50)


# ---------------------------------------------------------------------------
# Phase 6: VBUS sensing (iPhone / computer connected as host)
# ---------------------------------------------------------------------------
class TestVbusSensing:

    def test_vbus_prevents_host_switch_unconfigured(self):
        """iPhone supplies VBUS but slow to enumerate: stay device."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=False,
                  frame_same=0, usb_resets=0, vbus_present=True)
        assert fsm.mode == OTG_DEVICE

    def test_vbus_prevents_host_switch_on_suspend(self):
        """iPhone suspends USB (screen lock): VBUS still present, stay device."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=True,
                  frame_same=150, vbus_present=True)
        assert fsm.mode == OTG_DEVICE

    def test_no_vbus_allows_host_switch(self):
        """No VBUS (flash drive): allow normal OTG host switch."""
        fsm = OtgFsm()
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.mode == OTG_HOST

    def test_vbus_clears_debounce(self):
        """If VBUS appears mid-debounce, cancel the switch."""
        fsm = OtgFsm()
        # Start debounce without VBUS
        run_ticks(fsm, 0, 30, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.debounce_ms != 0

        # iPhone connects (VBUS appears)
        run_ticks(fsm, 300, 10, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=True)
        assert fsm.debounce_ms == 0
        assert fsm.mode == OTG_DEVICE


# ---------------------------------------------------------------------------
# Phase 7: USB MODE setting
# ---------------------------------------------------------------------------
class TestUsbModeSetting:

    def test_midi_mode_prevents_host_switch(self):
        """usb_mode=0 (MIDI): never switch to host, even with no VBUS."""
        fsm = OtgFsm(usb_mode=0)
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.mode == OTG_DEVICE

    def test_otg_mode_allows_host_switch(self):
        """usb_mode=1 (OTG): normal dual-role switching works."""
        fsm = OtgFsm(usb_mode=1)
        run_ticks(fsm, 0, 60, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.mode == OTG_HOST

    def test_midi_mode_overrides_vbus_absent(self):
        """usb_mode=0 stays device even when VBUS absent and conditions favor host."""
        fsm = OtgFsm(usb_mode=0)
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=True,
                  frame_same=150, vbus_present=False)
        assert fsm.mode == OTG_DEVICE

    def test_runtime_mode_change(self):
        """Changing usb_mode at runtime takes effect on next poll."""
        fsm = OtgFsm(usb_mode=0)
        run_ticks(fsm, 0, 200, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.mode == OTG_DEVICE
        fsm.usb_mode = 1
        run_ticks(fsm, 2000, 60, usb_up=True, usb_config=False,
                  frame_same=0, vbus_present=False)
        assert fsm.mode == OTG_HOST
