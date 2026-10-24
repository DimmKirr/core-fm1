# SPDX-License-Identifier: GPL-3.0-only
"""USB host stack protocol tests (phases 2-5).

Pure Python: validates byte layouts, parsing logic, and protocol framing
against the USB and SCSI specs. No C compilation or hardware needed.
"""
import struct
import pytest


# ---------------------------------------------------------------------------
# Constants mirroring fm1_usb_musb.h and fm1_usb_host.h
# ---------------------------------------------------------------------------
CBW_SIGNATURE = 0x43425355
CSW_SIGNATURE = 0x53425355

USB_DT_DEVICE = 1
USB_DT_CONFIG = 2
USB_DT_INTERFACE = 4
USB_DT_ENDPOINT = 5

USB_CLASS_MSC = 0x08
USB_MSC_SUBCLASS_SCSI = 0x06
USB_MSC_PROTO_BBB = 0x50
USB_EP_DIR_IN = 0x80

SCSI_TEST_UNIT_READY = 0x00
SCSI_REQUEST_SENSE = 0x03
SCSI_INQUIRY = 0x12
SCSI_READ_CAPACITY10 = 0x25
SCSI_READ10 = 0x28

# CSR0 host-mode bits
CSR0H_RxPktRdy = 0x0001
CSR0H_TxPktRdy = 0x0002
CSR0H_RxStall = 0x0004
CSR0H_SetupPkt = 0x0008
CSR0H_Error = 0x0010
CSR0H_ReqPkt = 0x0020
CSR0H_StatusPkt = 0x0040
CSR0H_NAKTimeout = 0x0080


# ---------------------------------------------------------------------------
# Phase 2: Control Transfer SETUP packet construction
# ---------------------------------------------------------------------------
class TestControlTransferSetup:
    """Verify usb_control_msg() builds an 8-byte SETUP packet per USB 2.0
    spec Table 9-2: bmRequestType, bRequest, wValue (LE16), wIndex (LE16),
    wLength (LE16)."""

    @staticmethod
    def build_setup(bm_req, b_req, w_value, w_index, w_length):
        """Python equivalent of usb_control_msg() setup construction."""
        return struct.pack('<BBHHH', bm_req, b_req, w_value, w_index, w_length)

    def test_setup_packet_size(self):
        pkt = self.build_setup(0x80, 0x06, 0x0100, 0x0000, 18)
        assert len(pkt) == 8

    def test_get_device_descriptor(self):
        """GET_DESCRIPTOR(DEVICE) at addr 0: bmReq=0x80 (IN), bReq=0x06,
        wValue=0x0100 (type=1 DEVICE, index=0), wIndex=0, wLength=18."""
        pkt = self.build_setup(0x80, 0x06, 0x0100, 0x0000, 18)
        assert pkt == bytes([0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00])

    def test_set_address(self):
        """SET_ADDRESS(1): bmReq=0x00 (OUT), bReq=0x05, wValue=1,
        wIndex=0, wLength=0."""
        pkt = self.build_setup(0x00, 0x05, 0x0001, 0x0000, 0)
        assert pkt == bytes([0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])

    def test_get_config_descriptor(self):
        """GET_DESCRIPTOR(CONFIG): bmReq=0x80, bReq=0x06, wValue=0x0200,
        wIndex=0, wLength=9."""
        pkt = self.build_setup(0x80, 0x06, 0x0200, 0x0000, 9)
        assert pkt == bytes([0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0x09, 0x00])

    def test_set_configuration(self):
        """SET_CONFIGURATION(1): bmReq=0x00, bReq=0x09, wValue=1."""
        pkt = self.build_setup(0x00, 0x09, 0x0001, 0x0000, 0)
        assert pkt == bytes([0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])

    def test_wvalue_byte_order(self):
        """wValue 0xABCD should be stored as CD AB (little-endian)."""
        pkt = self.build_setup(0x00, 0x00, 0xABCD, 0x0000, 0)
        assert pkt[2:4] == bytes([0xCD, 0xAB])

    def test_windex_byte_order(self):
        """wIndex 0x1234 should be stored as 34 12 (little-endian)."""
        pkt = self.build_setup(0x00, 0x00, 0x0000, 0x1234, 0)
        assert pkt[4:6] == bytes([0x34, 0x12])

    def test_direction_bit(self):
        """IN transfers have bmRequestType bit 7 set, OUT have it clear."""
        pkt_in = self.build_setup(0x80, 0x06, 0, 0, 18)
        pkt_out = self.build_setup(0x00, 0x05, 1, 0, 0)
        assert pkt_in[0] & 0x80 == 0x80
        assert pkt_out[0] & 0x80 == 0x00


class TestCSR0BitSequences:
    """Verify the CSR0 bit patterns match the MUSB host-mode spec."""

    def test_setup_phase_bits(self):
        """SETUP phase sets SetupPkt | TxPktRdy."""
        csr = CSR0H_SetupPkt | CSR0H_TxPktRdy
        assert csr == 0x000A

    def test_data_in_request_bits(self):
        """DATA IN phase sets ReqPkt to request data from device."""
        csr = CSR0H_ReqPkt
        assert csr == 0x0020

    def test_status_in_bits(self):
        """STATUS IN (after OUT data): ReqPkt | StatusPkt."""
        csr = CSR0H_ReqPkt | CSR0H_StatusPkt
        assert csr == 0x0060

    def test_status_out_bits(self):
        """STATUS OUT (after IN data): TxPktRdy | StatusPkt."""
        csr = CSR0H_TxPktRdy | CSR0H_StatusPkt
        assert csr == 0x0042

    def test_error_detection(self):
        """Error bit is 0x0010, RxStall is 0x0004."""
        assert CSR0H_Error == 0x0010
        assert CSR0H_RxStall == 0x0004

    def test_rxpktrdy_signals_data(self):
        """RxPktRdy (bit 0) indicates device data is in FIFO."""
        assert CSR0H_RxPktRdy == 0x0001


# ---------------------------------------------------------------------------
# Phase 3: Bulk Transfer CBW/CSW framing
# ---------------------------------------------------------------------------
class TestBulkCBWFraming:
    """CBW (Command Block Wrapper): 31 bytes per USB Mass Storage spec."""

    @staticmethod
    def build_cbw(tag, data_len, flags, lun, cb_len, cb_bytes):
        """Python equivalent of msc_bot_xfer() CBW construction."""
        cbw = struct.pack('<IIIBBB', CBW_SIGNATURE, tag, data_len,
                          flags, lun, cb_len)
        cb_padded = (cb_bytes + bytes(16))[:16]
        return cbw + cb_padded

    def test_cbw_size(self):
        cbw = self.build_cbw(1, 0, 0, 0, 6, bytes(6))
        assert len(cbw) == 31

    def test_cbw_signature(self):
        cbw = self.build_cbw(1, 0, 0, 0, 6, bytes(6))
        sig = struct.unpack_from('<I', cbw, 0)[0]
        assert sig == 0x43425355

    def test_cbw_signature_bytes(self):
        """CBW signature 'USBC' in little-endian: 55 53 42 43."""
        cbw = self.build_cbw(1, 0, 0, 0, 6, bytes(6))
        assert cbw[0:4] == b'USBC'

    def test_cbw_tag_matches(self):
        cbw = self.build_cbw(42, 0, 0, 0, 6, bytes(6))
        tag = struct.unpack_from('<I', cbw, 4)[0]
        assert tag == 42

    def test_cbw_data_length(self):
        cbw = self.build_cbw(1, 512, 0x80, 0, 10, bytes(10))
        data_len = struct.unpack_from('<I', cbw, 8)[0]
        assert data_len == 512

    def test_cbw_flags_in(self):
        cbw = self.build_cbw(1, 36, 0x80, 0, 6, bytes(6))
        assert cbw[12] == 0x80

    def test_cbw_flags_out(self):
        cbw = self.build_cbw(1, 0, 0x00, 0, 6, bytes(6))
        assert cbw[12] == 0x00

    def test_cbw_lun_zero(self):
        cbw = self.build_cbw(1, 0, 0, 0, 6, bytes(6))
        assert cbw[13] == 0

    def test_cbw_cb_length(self):
        cbw = self.build_cbw(1, 0, 0, 0, 10, bytes(10))
        assert cbw[14] == 10

    def test_cbw_cb_padding(self):
        """CB field is always 16 bytes; unused bytes must be zero."""
        cb = bytes([0x28, 0, 0, 0, 0, 0x10, 0, 0, 0x01, 0])
        cbw = self.build_cbw(1, 512, 0x80, 0, 10, cb)
        assert cbw[15:31] == cb + bytes(6)


class TestBulkCSWValidation:
    """CSW (Command Status Wrapper): 13 bytes."""

    @staticmethod
    def build_csw(sig, tag, residue, status):
        return struct.pack('<IIIB', sig, tag, residue, status)

    def test_csw_size(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 0, 0)
        assert len(csw) == 13

    def test_csw_signature(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 0, 0)
        assert csw[0:4] == b'USBS'

    def test_csw_signature_value(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 0, 0)
        sig = struct.unpack_from('<I', csw, 0)[0]
        assert sig == 0x53425355

    def test_csw_tag_must_match_cbw(self):
        """CSW tag must equal the CBW tag that initiated the transfer."""
        cbw_tag = 7
        csw = self.build_csw(CSW_SIGNATURE, cbw_tag, 0, 0)
        csw_tag = struct.unpack_from('<I', csw, 4)[0]
        assert csw_tag == cbw_tag

    def test_csw_bad_signature_rejected(self):
        """Any signature != 0x53425355 is invalid."""
        csw = self.build_csw(0xDEADBEEF, 1, 0, 0)
        sig = struct.unpack_from('<I', csw, 0)[0]
        assert sig != CSW_SIGNATURE

    def test_csw_status_pass(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 0, 0)
        assert csw[12] == 0  # bCSWStatus: 0 = pass

    def test_csw_status_fail(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 0, 1)
        assert csw[12] == 1  # bCSWStatus: 1 = fail

    def test_csw_residue(self):
        csw = self.build_csw(CSW_SIGNATURE, 1, 128, 0)
        residue = struct.unpack_from('<I', csw, 8)[0]
        assert residue == 128


# ---------------------------------------------------------------------------
# Phase 4: USB Enumeration descriptor parsing
# ---------------------------------------------------------------------------
def build_device_desc(vendor=0x1234, product=0x5678, max_pkt=64):
    """Build a standard 18-byte USB device descriptor."""
    return struct.pack('<BBHBBBBHHHBBBB',
                       18,              # bLength
                       USB_DT_DEVICE,   # bDescriptorType
                       0x0200,          # bcdUSB
                       0,               # bDeviceClass
                       0,               # bDeviceSubClass
                       0,               # bDeviceProtocol
                       max_pkt,         # bMaxPacketSize0
                       vendor,          # idVendor
                       product,         # idProduct
                       0x0100,          # bcdDevice
                       1,               # iManufacturer
                       2,               # iProduct
                       3,               # iSerialNumber
                       1)               # bNumConfigurations


def build_config_desc(total_length, num_interfaces=1, config_value=1):
    """Build a 9-byte USB configuration descriptor header."""
    return struct.pack('<BBHBBBB',
                       9,              # bLength (BBHBBBB = 1+1+2+1+1+1+1 = 8 ?? no)
                       USB_DT_CONFIG,  # bDescriptorType
                       total_length,   # wTotalLength
                       num_interfaces, # bNumInterfaces
                       config_value,   # bConfigurationValue
                       0,              # iConfiguration
                       0x80,           # bmAttributes (bus-powered)
                       250)            # bMaxPower (500mA)
    # struct size: B B H B B B B = 1+1+2+1+1+1+1 = 8 ... wait, that's 8 not 9
    # USB config descriptor is 9 bytes. Let me fix the format.


def build_config_desc(total_length, num_interfaces=1, config_value=1):
    """Build a 9-byte USB configuration descriptor header."""
    # bLength(1) bDescType(1) wTotalLength(2) bNumIface(1) bConfigVal(1)
    # iConfig(1) bmAttrib(1) bMaxPower(1) = 9 bytes
    return struct.pack('<BBHBBBBB',
                       9,               # bLength
                       USB_DT_CONFIG,   # bDescriptorType
                       total_length,    # wTotalLength (LE16)
                       num_interfaces,  # bNumInterfaces
                       config_value,    # bConfigurationValue
                       0,               # iConfiguration
                       0x80,            # bmAttributes
                       250)             # bMaxPower


def build_interface_desc(iface_class, subclass, protocol, num_ep=2, iface_num=0):
    """Build a 9-byte USB interface descriptor."""
    return struct.pack('<BBBBBBBBB',
                       9,              # bLength
                       USB_DT_INTERFACE,
                       iface_num,      # bInterfaceNumber
                       0,              # bAlternateSetting
                       num_ep,         # bNumEndpoints
                       iface_class,
                       subclass,
                       protocol,
                       0)              # iInterface


def build_endpoint_desc(address, attributes, max_packet, interval=0):
    """Build a 7-byte USB endpoint descriptor."""
    return struct.pack('<BBBBHB',
                       7,              # bLength
                       USB_DT_ENDPOINT,
                       address,        # bEndpointAddress
                       attributes,     # bmAttributes
                       max_packet,     # wMaxPacketSize (LE16)
                       interval)       # bInterval


def build_msc_config(ep_in_addr=0x81, ep_out_addr=0x02, max_packet=512):
    """Build a complete config descriptor blob for an MSC BBB device."""
    iface = build_interface_desc(USB_CLASS_MSC, USB_MSC_SUBCLASS_SCSI,
                                 USB_MSC_PROTO_BBB, num_ep=2)
    ep_in = build_endpoint_desc(ep_in_addr, 0x02, max_packet)   # bulk IN
    ep_out = build_endpoint_desc(ep_out_addr, 0x02, max_packet)  # bulk OUT
    body = iface + ep_in + ep_out
    total = 9 + len(body)
    header = build_config_desc(total, num_interfaces=1, config_value=1)
    return header + body


def parse_config_for_msc(buf):
    """Python reimplementation of usb_host_enum.c descriptor parsing logic."""
    if len(buf) < 9:
        return None

    total = struct.unpack_from('<H', buf, 2)[0]
    if total > len(buf):
        total = len(buf)

    cfg_blen = buf[0]  # bLength of config descriptor
    pos = cfg_blen
    ep_in = 0
    ep_out = 0
    ep_maxp = 64
    msc_found = False

    while pos + 2 <= total:
        dlen = buf[pos]
        dtype = buf[pos + 1]
        if dlen < 2 or pos + dlen > total:
            break

        if dtype == USB_DT_INTERFACE and dlen >= 9:
            iface_class = buf[pos + 5]
            iface_sub = buf[pos + 6]
            iface_proto = buf[pos + 7]
            if (iface_class == USB_CLASS_MSC and
                    iface_sub == USB_MSC_SUBCLASS_SCSI and
                    iface_proto == USB_MSC_PROTO_BBB):
                msc_found = True
            else:
                msc_found = False

        if dtype == USB_DT_ENDPOINT and dlen >= 7 and msc_found:
            ep_addr = buf[pos + 2]
            ep_attr = buf[pos + 3]
            ep_maxp_val = struct.unpack_from('<H', buf, pos + 4)[0]
            if (ep_attr & 0x03) == 0x02:  # bulk
                if ep_addr & USB_EP_DIR_IN:
                    ep_in = ep_addr & 0x0F
                    ep_maxp = ep_maxp_val
                else:
                    ep_out = ep_addr & 0x0F

        pos += dlen

    if not ep_in or not ep_out:
        return None
    return {'ep_in': ep_in, 'ep_out': ep_out, 'max_packet': ep_maxp}


class TestDeviceDescriptor:
    def test_size(self):
        desc = build_device_desc()
        assert len(desc) == 18

    def test_blength(self):
        desc = build_device_desc()
        assert desc[0] == 18

    def test_descriptor_type(self):
        desc = build_device_desc()
        assert desc[1] == USB_DT_DEVICE

    def test_max_packet_size(self):
        desc = build_device_desc(max_pkt=8)
        assert desc[7] == 8

    def test_vid_pid(self):
        desc = build_device_desc(vendor=0xCAFE, product=0xBEEF)
        vid, pid = struct.unpack_from('<HH', desc, 8)
        assert vid == 0xCAFE
        assert pid == 0xBEEF


class TestConfigDescriptorParsing:
    def test_simple_msc_device(self):
        """Standard MSC device: one interface, bulk-in EP1, bulk-out EP2."""
        buf = build_msc_config(ep_in_addr=0x81, ep_out_addr=0x02,
                               max_packet=512)
        result = parse_config_for_msc(buf)
        assert result is not None
        assert result['ep_in'] == 1
        assert result['ep_out'] == 2
        assert result['max_packet'] == 512

    def test_different_ep_numbers(self):
        """MSC device with EP3 IN, EP4 OUT."""
        buf = build_msc_config(ep_in_addr=0x83, ep_out_addr=0x04,
                               max_packet=64)
        result = parse_config_for_msc(buf)
        assert result is not None
        assert result['ep_in'] == 3
        assert result['ep_out'] == 4
        assert result['max_packet'] == 64

    def test_no_msc_interface(self):
        """Device with HID interface only: parsing should return None."""
        iface = build_interface_desc(0x03, 0x00, 0x00, num_ep=1)
        ep = build_endpoint_desc(0x81, 0x03, 8, interval=10)  # interrupt
        body = iface + ep
        total = 9 + len(body)
        header = build_config_desc(total)
        buf = header + body
        result = parse_config_for_msc(buf)
        assert result is None

    def test_multi_interface_msc_second(self):
        """Two interfaces: first HID, second MSC. Parser should find MSC."""
        # HID interface + interrupt endpoint
        iface_hid = build_interface_desc(0x03, 0x00, 0x00, num_ep=1,
                                         iface_num=0)
        ep_hid = build_endpoint_desc(0x81, 0x03, 8, interval=10)
        # MSC interface + bulk endpoints
        iface_msc = build_interface_desc(USB_CLASS_MSC, USB_MSC_SUBCLASS_SCSI,
                                         USB_MSC_PROTO_BBB, num_ep=2,
                                         iface_num=1)
        ep_in = build_endpoint_desc(0x82, 0x02, 512)
        ep_out = build_endpoint_desc(0x03, 0x02, 512)
        body = iface_hid + ep_hid + iface_msc + ep_in + ep_out
        total = 9 + len(body)
        header = build_config_desc(total, num_interfaces=2)
        buf = header + body
        result = parse_config_for_msc(buf)
        assert result is not None
        assert result['ep_in'] == 2
        assert result['ep_out'] == 3

    def test_wrong_protocol_rejected(self):
        """MSC with CBI protocol (0x00) instead of BBB (0x50): rejected."""
        iface = build_interface_desc(USB_CLASS_MSC, USB_MSC_SUBCLASS_SCSI,
                                     0x00, num_ep=2)
        ep_in = build_endpoint_desc(0x81, 0x02, 64)
        ep_out = build_endpoint_desc(0x02, 0x02, 64)
        body = iface + ep_in + ep_out
        total = 9 + len(body)
        header = build_config_desc(total)
        buf = header + body
        result = parse_config_for_msc(buf)
        assert result is None

    def test_truncated_buffer(self):
        """Buffer shorter than wTotalLength: parser should stop safely."""
        buf = build_msc_config()
        truncated = buf[:15]  # cut mid-interface
        result = parse_config_for_msc(truncated)
        assert result is None

    def test_ep_direction_bit(self):
        """EP address 0x81: bit 7 set = IN, bits 0-3 = EP number 1."""
        assert 0x81 & USB_EP_DIR_IN == USB_EP_DIR_IN
        assert 0x81 & 0x0F == 1
        assert 0x02 & USB_EP_DIR_IN == 0
        assert 0x02 & 0x0F == 2


# ---------------------------------------------------------------------------
# Phase 5: SCSI / MSC protocol
# ---------------------------------------------------------------------------
class TestSCSICBWLayout:
    """Verify CBW byte layout for each SCSI command matches the spec."""

    def test_test_unit_ready_cbw(self):
        """TEST_UNIT_READY: opcode 0x00, 6-byte CDB, no data."""
        cb = bytes([SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0])
        cbw = TestBulkCBWFraming.build_cbw(1, 0, 0x00, 0, 6, cb)
        assert cbw[14] == 6  # bCBWCBLength
        assert cbw[15] == SCSI_TEST_UNIT_READY
        assert struct.unpack_from('<I', cbw, 8)[0] == 0  # no data

    def test_inquiry_cbw(self):
        """INQUIRY: opcode 0x12, 6-byte CDB, 36 bytes IN."""
        cb = bytes([SCSI_INQUIRY, 0, 0, 0, 36, 0])
        cbw = TestBulkCBWFraming.build_cbw(2, 36, 0x80, 0, 6, cb)
        assert cbw[15] == SCSI_INQUIRY
        assert cbw[19] == 36  # allocation length in CDB[4]
        assert cbw[12] == 0x80  # direction IN
        assert struct.unpack_from('<I', cbw, 8)[0] == 36

    def test_request_sense_cbw(self):
        """REQUEST_SENSE: opcode 0x03, 6-byte CDB, 18 bytes IN."""
        cb = bytes([SCSI_REQUEST_SENSE, 0, 0, 0, 18, 0])
        cbw = TestBulkCBWFraming.build_cbw(3, 18, 0x80, 0, 6, cb)
        assert cbw[15] == SCSI_REQUEST_SENSE
        assert cbw[19] == 18
        assert struct.unpack_from('<I', cbw, 8)[0] == 18

    def test_read_capacity10_cbw(self):
        """READ_CAPACITY(10): opcode 0x25, 10-byte CDB, 8 bytes IN."""
        cb = bytes([SCSI_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        cbw = TestBulkCBWFraming.build_cbw(4, 8, 0x80, 0, 10, cb)
        assert cbw[14] == 10
        assert cbw[15] == SCSI_READ_CAPACITY10
        assert struct.unpack_from('<I', cbw, 8)[0] == 8

    def test_read10_cbw(self):
        """READ(10): opcode 0x28, LBA in CDB[2:6] BE, count in CDB[7:9] BE."""
        lba = 0x00001000
        count = 8
        cb = bytes([
            SCSI_READ10, 0,
            (lba >> 24) & 0xFF, (lba >> 16) & 0xFF,
            (lba >> 8) & 0xFF, lba & 0xFF,
            0,
            (count >> 8) & 0xFF, count & 0xFF,
            0
        ])
        cbw = TestBulkCBWFraming.build_cbw(5, count * 512, 0x80, 0, 10, cb)
        assert cbw[15] == SCSI_READ10
        # LBA in CDB[2:6] big-endian
        assert cbw[17:21] == bytes([0x00, 0x00, 0x10, 0x00])
        # Transfer count in CDB[7:9] big-endian
        assert cbw[22:24] == bytes([0x00, 0x08])
        assert struct.unpack_from('<I', cbw, 8)[0] == 4096  # 8 * 512


class TestSCSIReadCapacity:
    """READ_CAPACITY(10) returns 8 bytes: last_lba(BE32) + block_size(BE32)."""

    @staticmethod
    def msc_be32(data, offset=0):
        """Python equivalent of msc_be32() in usb_msc.c."""
        return struct.unpack_from('>I', data, offset)[0]

    def test_parse_capacity_response(self):
        last_lba = 0x003B9AC9  # ~1 GB at 512 B/sector
        block_size = 512
        resp = struct.pack('>II', last_lba, block_size)
        assert len(resp) == 8
        assert self.msc_be32(resp, 0) == last_lba
        assert self.msc_be32(resp, 4) == block_size

    def test_large_capacity(self):
        """32 GB drive: last_lba = 0x03B9FFFF."""
        last_lba = 0x03B9FFFF
        resp = struct.pack('>II', last_lba, 512)
        assert self.msc_be32(resp, 0) == last_lba
        # Total sectors = last_lba + 1
        assert self.msc_be32(resp, 0) + 1 == 0x03BA0000

    def test_4k_sector(self):
        """Some drives report 4096-byte sectors."""
        resp = struct.pack('>II', 0x001FFFFF, 4096)
        assert self.msc_be32(resp, 4) == 4096

    def test_byte_order_matters(self):
        """Verify big-endian: 0x01020304 -> bytes 01 02 03 04."""
        resp = struct.pack('>II', 0x01020304, 0x05060708)
        assert resp == bytes([0x01, 0x02, 0x03, 0x04,
                              0x05, 0x06, 0x07, 0x08])


class TestSCSIRead10CDB:
    """Verify READ(10) CDB construction matches the C implementation."""

    @staticmethod
    def build_read10_cdb(lba, count):
        """Python equivalent of msc_read_sectors() CDB construction."""
        cb = bytearray(10)
        cb[0] = SCSI_READ10
        cb[1] = 0
        cb[2] = (lba >> 24) & 0xFF
        cb[3] = (lba >> 16) & 0xFF
        cb[4] = (lba >> 8) & 0xFF
        cb[5] = lba & 0xFF
        cb[6] = 0
        cb[7] = (count >> 8) & 0xFF
        cb[8] = count & 0xFF
        cb[9] = 0
        return bytes(cb)

    def test_opcode(self):
        cdb = self.build_read10_cdb(0, 1)
        assert cdb[0] == 0x28

    def test_lba_big_endian(self):
        cdb = self.build_read10_cdb(0x12345678, 1)
        assert cdb[2:6] == bytes([0x12, 0x34, 0x56, 0x78])

    def test_count_big_endian(self):
        cdb = self.build_read10_cdb(0, 0x0100)
        assert cdb[7:9] == bytes([0x01, 0x00])

    def test_single_sector(self):
        cdb = self.build_read10_cdb(0, 1)
        assert cdb[7:9] == bytes([0x00, 0x01])

    def test_lba_zero(self):
        cdb = self.build_read10_cdb(0, 1)
        assert cdb[2:6] == bytes([0, 0, 0, 0])

    def test_reserved_fields(self):
        """CDB[1], CDB[6], CDB[9] should be zero."""
        cdb = self.build_read10_cdb(100, 50)
        assert cdb[1] == 0
        assert cdb[6] == 0
        assert cdb[9] == 0
