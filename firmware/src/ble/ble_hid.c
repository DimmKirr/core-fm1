/* SPDX-License-Identifier: GPL-3.0-only */
/* The HID over GATT client of the central role (BLE_CENTRAL; ble_hid.h), discovering an HID service on
 * an Xbox Series X|S Wireless Controller and subscribing to its input report notifications. The gamepad's
 * report (Report ID 1, 16 bytes: sticks, triggers, hat, buttons) is scaled to 7-bit MIDI CC values and
 * injected into the firmware's MIDI input as USB-MIDI event packets.
 *
 * Written from the USB HID over GATT Profile 1.0, the HID Service 1.0, the Core Specification Vol 3
 * Part F / G, and the Xbox Series X|S HID Report Descriptor (Report ID 1, 16 bytes). No vendor code. */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_util.h"
#include "ble_diag.h"
#include "ble_hid.h"

enum { H_IDLE, H_MTU, H_SVC, H_CHR, H_RMAP, H_RMAP_BLOB, H_PMODE_R, H_PMODE_W,
       H_RPT_DSC, H_RPT_REF, H_RPT_CCCD_WRITE, H_NEXT_RPT, H_READY, H_FAIL };
enum { HE_AUTHEN = 0x05, HE_NOT_SUPP = 0x06, HE_KEY_SIZE = 0x0C, HE_NOT_FOUND = 0x0A, HE_ENC = 0x0F };

#ifndef HID_MAX_REPORTS
#define HID_MAX_REPORTS 4u
#endif

/* (ble_central.c) */
static void ble_central_hid_ready(void);
static void ble_central_hid_fail(uint8_t why, uint8_t code);
static int ble_central_hid_auth(uint8_t code);

static struct {
    uint8_t st, wait_sec, due;                     /* H_*; a request waits for the link's security; one waits for room */
    uint8_t req[23], req_n;                        /* the request outstanding (sent again after security) */
    uint32_t t_req;                                /* when it went (the 30 s timeout) */
    uint16_t svc_s, svc_e;                         /* HID service handle range */
    uint16_t rmap_h, pmode_h, hinfo_h, hctrl_h;   /* characteristic value handles */
    uint16_t rmap_len;                             /* Report Map bytes read so far (Read Blob) */
    uint8_t rmap_hash;                             /* XOR hash of the Report Map (diagnostics) */
    struct {
        uint16_t val_h;                            /* Report (0x2A4D) value handle */
        uint16_t ref_h;                            /* Report Reference descriptor (0x2908) handle */
        uint16_t cccd_h;                           /* CCCD (0x2902) handle */
        uint8_t report_id, report_type;            /* from the Report Reference (1=Input, 2=Output, 3=Feature) */
        uint8_t subscribed;
    } rpt[HID_MAX_REPORTS];
    uint8_t rpt_n;                                 /* reports found */
    uint8_t rpt_cur;                               /* the report being processed now (descriptor discovery) */
    uint16_t chr_next;                             /* next handle for characteristic discovery */
    uint16_t dsc_end;                              /* end handle for the current report's descriptor range */
    /* CC shadow state for change-only sending */
    uint8_t shadow[32];                            /* CC 85..116 -> shadow[0..31] */
    uint8_t shadow_valid;                          /* 0 until the first report: send everything */
} bhid;

/* ---- helpers (the same pattern as ble_gattc.c) ------------------------------------------- */

static void hid_send(void)
{
    if (!ble_ll_send(L2CAP_CID_ATT, bhid.req, bhid.req_n)) {
        bhid.due = 1;
        return;
    }
    bhid.due = 0;
    bhid.t_req = ble_hw_time_us();
}

static void hid_req_range(uint8_t op, uint16_t s, uint16_t e, uint16_t type)
{
    bhid.req[0] = op;
    ble_wr16(bhid.req + 1, s);
    ble_wr16(bhid.req + 3, e);
    bhid.req_n = 5;
    if (type) {
        ble_wr16(bhid.req + 5, type);
        bhid.req_n = 7;
    }
    hid_send();
}

static void hid_fail(uint8_t why, uint8_t code)
{
    bhid.st = H_FAIL;
    BLE_DG(ble_dgc.hid_state = H_FAIL);
    ble_central_hid_fail(why, code);
}

static void hid_step(uint8_t st)
{
    bhid.st = st;
    BLE_DG(ble_dgc.hid_state = st);
}

/* ---- discovery steps --------------------------------------------------------------------- */

static void hid_find_service(void)                 /* Read By Group Type: primary services (0x2800) */
{
    hid_step(H_SVC);
    hid_req_range(0x10, 1, 0xFFFF, 0x2800);
}

static void hid_chars(uint16_t from)
{
    hid_step(H_CHR);
    hid_req_range(0x08, from, bhid.svc_e, 0x2803);
}

static void hid_service(uint16_t s, uint16_t e)
{
    bhid.svc_s = s;
    bhid.svc_e = e;
    bhid.rmap_h = bhid.pmode_h = bhid.hinfo_h = bhid.hctrl_h = 0;
    bhid.rpt_n = 0;
    bhid.chr_next = s;
    hid_chars(s);
}

static void hid_read_rmap(void);
static void hid_read_pmode(void);
static void hid_process_rpt(void);

/* Read By Group Type Response (primary services): handle, end, UUID */
static void hid_svc_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1], last = 0;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        last = ble_rd16(p + i + 2);
        if (el == 6u && ble_rd16(p + i + 4) == 0x1812u) {   /* 16-bit UUID = HID Service */
            hid_service(ble_rd16(p + i), last);
            return;
        }
    }
    if (!last || last == 0xFFFFu) {
        hid_fail(BLE_CF_NO_HID, 0);
        return;
    }
    hid_req_range(0x10, (uint16_t)(last + 1u), 0xFFFF, 0x2800);   /* more services */
}

/* Read By Type Response (0x2803 characteristic declarations): handle, properties, value handle, UUID */
static void hid_chars_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1], last = 0, uuid;
    if (el < 7u)
        return;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        last = ble_rd16(p + i);
        if (el != 7u)
            continue;                              /* skip 128-bit UUID declarations */
        uuid = ble_rd16(p + i + 5);
        switch (uuid) {
        case 0x2A4Bu: bhid.rmap_h = ble_rd16(p + i + 3); break;       /* Report Map */
        case 0x2A4Eu: bhid.pmode_h = ble_rd16(p + i + 3); break;      /* Protocol Mode */
        case 0x2A4Au: bhid.hinfo_h = ble_rd16(p + i + 3); break;      /* HID Information */
        case 0x2A4Cu: bhid.hctrl_h = ble_rd16(p + i + 3); break;      /* HID Control Point */
        case 0x2A4Du:                                                  /* Report */
            if (bhid.rpt_n < HID_MAX_REPORTS) {
                bhid.rpt[bhid.rpt_n].val_h = ble_rd16(p + i + 3);
                bhid.rpt[bhid.rpt_n].ref_h = 0;
                bhid.rpt[bhid.rpt_n].cccd_h = 0;
                bhid.rpt[bhid.rpt_n].report_id = 0;
                bhid.rpt[bhid.rpt_n].report_type = 0;
                bhid.rpt[bhid.rpt_n].subscribed = 0;
                bhid.rpt_n++;
            }
            break;
        default:
            break;
        }
    }
    if (last >= bhid.svc_e || !last) {
        /* all characteristics found: start the Report Map read */
        hid_read_rmap();
        return;
    }
    hid_chars((uint16_t)(last + 1u));
}

/* Read the Report Map (0x2A4B): Read Request first, then Read Blob for the rest */
static void hid_read_rmap(void)
{
    if (!bhid.rmap_h) {
        hid_read_pmode();                          /* no Report Map: skip to Protocol Mode */
        return;
    }
    hid_step(H_RMAP);
    bhid.rmap_len = 0;
    bhid.rmap_hash = 0;
    bhid.req[0] = 0x0A;                           /* Read Request */
    ble_wr16(bhid.req + 1, bhid.rmap_h);
    bhid.req_n = 3;
    hid_send();
}

static void hid_rmap_data(const uint8_t *data, uint16_t n)
{
    uint16_t i;
    for (i = 0; i < n; i++)
        bhid.rmap_hash ^= data[i];
    bhid.rmap_len = (uint16_t)(bhid.rmap_len + n);
}

static void hid_rmap_rsp(const uint8_t *p, uint16_t n)
{
    hid_rmap_data(p + 1, (uint16_t)(n - 1u));
    if (n - 1u < ble_att_mtu() - 1u) {            /* the whole value (or the last fragment) */
        hid_read_pmode();
        return;
    }
    /* more data: Read Blob */
    hid_step(H_RMAP_BLOB);
    bhid.req[0] = 0x0C;                           /* Read Blob Request */
    ble_wr16(bhid.req + 1, bhid.rmap_h);
    ble_wr16(bhid.req + 3, bhid.rmap_len);
    bhid.req_n = 5;
    hid_send();
}

static void hid_rmap_blob_rsp(const uint8_t *p, uint16_t n)
{
    hid_rmap_data(p + 1, (uint16_t)(n - 1u));
    if (n - 1u < ble_att_mtu() - 1u) {
        hid_read_pmode();
        return;
    }
    bhid.req[0] = 0x0C;
    ble_wr16(bhid.req + 1, bhid.rmap_h);
    ble_wr16(bhid.req + 3, bhid.rmap_len);
    bhid.req_n = 5;
    hid_send();
}

/* Read Protocol Mode; if Boot Protocol write Report Protocol */
static void hid_read_pmode(void)
{
    if (!bhid.pmode_h) {
        bhid.rpt_cur = 0;
        hid_process_rpt();
        return;
    }
    hid_step(H_PMODE_R);
    bhid.req[0] = 0x0A;                           /* Read Request */
    ble_wr16(bhid.req + 1, bhid.pmode_h);
    bhid.req_n = 3;
    hid_send();
}

static void hid_pmode_rsp(const uint8_t *p, uint16_t n)
{
    if (n >= 2u && p[1] == 0x00u) {                /* Boot Protocol -> write Report Protocol */
        uint8_t cmd[4];
        cmd[0] = 0x52;                            /* Write Without Response (Write Command) */
        ble_wr16(cmd + 1, bhid.pmode_h);
        cmd[3] = 0x01;                            /* Report Protocol */
        ble_ll_send(L2CAP_CID_ATT, cmd, 4);       /* no response expected */
    }
    /* proceed to report descriptor discovery */
    bhid.rpt_cur = 0;
    hid_process_rpt();
}

/* ---- per-report descriptor discovery ----------------------------------------------------- */

/* the descriptor range for report rpt_cur: after its value handle, up to the next report's value handle - 1
 * or the service's end */
static uint16_t hid_rpt_dsc_end(uint8_t idx)
{
    uint8_t next;
    for (next = (uint8_t)(idx + 1u); next < bhid.rpt_n; next++)
        if (bhid.rpt[next].val_h > bhid.rpt[idx].val_h)
            return (uint16_t)(bhid.rpt[next].val_h - 1u);
    return bhid.svc_e;
}

static void hid_rpt_find_descs(void);
static void hid_rpt_read_ref(void);
static void hid_rpt_subscribe(void);

/* process the current report (rpt_cur): find its descriptors, read the reference, subscribe if input */
static void hid_process_rpt(void)
{
    if (bhid.rpt_cur >= bhid.rpt_n) {
        /* all reports processed: ready */
        hid_step(H_READY);
        BLE_DG(ble_dgc.hid_starts++);
        ble_central_hid_ready();
        return;
    }
    bhid.rpt[bhid.rpt_cur].ref_h = 0;
    bhid.rpt[bhid.rpt_cur].cccd_h = 0;
    bhid.dsc_end = hid_rpt_dsc_end(bhid.rpt_cur);
    hid_rpt_find_descs();
}

static void hid_rpt_find_descs(void)
{
    uint16_t from = (uint16_t)(bhid.rpt[bhid.rpt_cur].val_h + 1u);
    if (from > bhid.dsc_end) {
        /* no descriptors: move to next report */
        hid_step(H_NEXT_RPT);
        bhid.rpt_cur++;
        hid_process_rpt();
        return;
    }
    hid_step(H_RPT_DSC);
    hid_req_range(0x04, from, bhid.dsc_end, 0);   /* Find Information */
}

/* Find Information Response: handle + UUID pairs */
static void hid_rpt_descs_rsp(const uint8_t *p, uint16_t n)
{
    uint16_t i, el = p[1] == 1u ? 4u : 18u, last = 0;
    for (i = 2; i + el <= n; i = (uint16_t)(i + el)) {
        last = ble_rd16(p + i);
        if (el == 4u) {
            uint16_t uuid = ble_rd16(p + i + 2);
            if (uuid == 0x2908u)
                bhid.rpt[bhid.rpt_cur].ref_h = last;
            else if (uuid == 0x2902u)
                bhid.rpt[bhid.rpt_cur].cccd_h = last;
        }
    }
    if (bhid.rpt[bhid.rpt_cur].ref_h && bhid.rpt[bhid.rpt_cur].cccd_h) {
        hid_rpt_read_ref();                        /* both found: read the Report Reference */
        return;
    }
    if (last && last < bhid.dsc_end) {
        hid_req_range(0x04, (uint16_t)(last + 1u), bhid.dsc_end, 0);   /* more descriptors */
        return;
    }
    /* one or both not found: if we have the ref, read it anyway */
    if (bhid.rpt[bhid.rpt_cur].ref_h) {
        hid_rpt_read_ref();
        return;
    }
    /* skip this report */
    bhid.rpt_cur++;
    hid_process_rpt();
}

/* Read the Report Reference descriptor (0x2908): 2 bytes, Report ID and Report Type */
static void hid_rpt_read_ref(void)
{
    hid_step(H_RPT_REF);
    bhid.req[0] = 0x0A;                           /* Read Request */
    ble_wr16(bhid.req + 1, bhid.rpt[bhid.rpt_cur].ref_h);
    bhid.req_n = 3;
    hid_send();
}

static void hid_rpt_ref_rsp(const uint8_t *p, uint16_t n)
{
    if (n >= 3u) {
        bhid.rpt[bhid.rpt_cur].report_id = p[1];
        bhid.rpt[bhid.rpt_cur].report_type = p[2];
    }
    /* subscribe to input reports (type 1) that have a CCCD */
    if (bhid.rpt[bhid.rpt_cur].report_type == 1u && bhid.rpt[bhid.rpt_cur].cccd_h) {
        hid_rpt_subscribe();
        return;
    }
    bhid.rpt_cur++;
    hid_process_rpt();
}

static void hid_rpt_subscribe(void)
{
    hid_step(H_RPT_CCCD_WRITE);
    bhid.req[0] = 0x12;                           /* Write Request */
    ble_wr16(bhid.req + 1, bhid.rpt[bhid.rpt_cur].cccd_h);
    bhid.req[3] = 0x01;                           /* notifications */
    bhid.req[4] = 0x00;
    bhid.req_n = 5;
    hid_send();
}

/* ---- report-to-MIDI-CC conversion -------------------------------------------------------- */

/* the hat switch (1-8 clockwise from N, 0 center) to D-pad CCs: {DU, DD, DL, DR} */
static const uint8_t HID_HAT_MAP[9][4] = {
    {  0,   0,   0,   0},                         /* 0: center */
    {127,   0,   0,   0},                         /* 1: N */
    {127,   0,   0, 127},                         /* 2: NE */
    {  0,   0,   0, 127},                         /* 3: E */
    {  0, 127,   0, 127},                         /* 4: SE */
    {  0, 127,   0,   0},                         /* 5: S */
    {  0, 127, 127,   0},                         /* 6: SW */
    {  0,   0, 127,   0},                         /* 7: W */
    {127,   0, 127,   0},                         /* 8: NW */
};

static void hid_cc(uint8_t cc, uint8_t val)
{
    uint8_t idx = (uint8_t)(cc - 85u);
    uint32_t pkt;
    if (idx >= 32u)
        return;
    if (bhid.shadow_valid && bhid.shadow[idx] == val)
        return;
    bhid.shadow[idx] = val;
    pkt = 0x0Bu | (0xB0u << 8) | ((uint32_t)cc << 16) | ((uint32_t)val << 24);
    ble_app_midi_in(pkt, 0, 0);
    BLE_DG(ble_dgc.hid_ccs_out++);
}

static void hid_bend(uint16_t val14)
{
    uint32_t pkt = 0x0Eu | (0xE0u << 8) | ((uint32_t)(val14 & 0x7Fu) << 16) | ((uint32_t)(val14 >> 7) << 24);
    ble_app_midi_in(pkt, 0, 0);
}

static void hid_report_in(uint16_t handle, const uint8_t *data, uint16_t len)
{
    uint8_t i, hat;
    uint16_t lx, ly, rx, rz, lt, rt, btns;
    const uint8_t *dpad;

    /* find which report this handle belongs to */
    for (i = 0; i < bhid.rpt_n; i++)
        if (bhid.rpt[i].val_h == handle && bhid.rpt[i].subscribed)
            break;
    if (i >= bhid.rpt_n)
        return;

    BLE_DG(ble_dgc.hid_reports_in++);

    /* only Report ID 1 (the Xbox gamepad report), 16 bytes */
    if (bhid.rpt[i].report_id != 1u || len < 16u)
        return;

    lx  = ble_rd16(data + 0);
    ly  = ble_rd16(data + 2);
    rx  = ble_rd16(data + 4);
    rz  = ble_rd16(data + 6);
    lt  = ble_rd16(data + 8)  & 0x03FFu;          /* 10 bits */
    rt  = ble_rd16(data + 10) & 0x03FFu;

    /* axes: gamepad CCs (85-90) for our protocol, plus standard CCs the firmware acts on */
    hid_cc(85, (uint8_t)(lx >> 9));
    hid_cc(86, (uint8_t)(127u - (ly >> 9)));
    hid_cc(87, (uint8_t)(rx >> 9));
    hid_cc(88, (uint8_t)(127u - (rz >> 9)));

    /* triggers: 10-bit >> 3 (0..127) */
    hid_cc(89, (uint8_t)(lt >> 3));
    hid_cc(90, (uint8_t)(rt >> 3));

    /* expression: LY -> mod wheel (CC 1), LT -> breath (CC 2), RT -> foot (CC 4), RY -> pitch bend */
    {
        static uint8_t s_mod = 0xFF, s_br = 0xFF, s_ft = 0xFF;
        static uint16_t s_bend = 0xFFFF;
        uint8_t mod = (uint8_t)(127u - (ly >> 9));
        uint8_t br  = (uint8_t)(lt >> 3);
        uint8_t ft  = (uint8_t)(rt >> 3);
        uint16_t bend = (uint16_t)(rz >> 2);
        uint32_t pkt;
        if (mod != s_mod) {
            s_mod = mod;
            pkt = 0x0Bu | (0xB0u << 8) | (1u << 16) | ((uint32_t)mod << 24);
            ble_app_midi_in(pkt, 0, 0);
        }
        if (br != s_br) {
            s_br = br;
            pkt = 0x0Bu | (0xB0u << 8) | (2u << 16) | ((uint32_t)br << 24);
            ble_app_midi_in(pkt, 0, 0);
        }
        if (ft != s_ft) {
            s_ft = ft;
            pkt = 0x0Bu | (0xB0u << 8) | (4u << 16) | ((uint32_t)ft << 24);
            ble_app_midi_in(pkt, 0, 0);
        }
        if (bend != s_bend) {
            s_bend = bend;
            hid_bend(bend);
        }
    }

    /* hat switch */
    hat = data[12] & 0x0Fu;
    if (hat > 8u)
        hat = 0;
    dpad = HID_HAT_MAP[hat];
    hid_cc(113, dpad[0]);                          /* DU */
    hid_cc(114, dpad[1]);                          /* DD */
    hid_cc(115, dpad[2]);                          /* DL */
    hid_cc(116, dpad[3]);                          /* DR */

    /* buttons 1-11 -> CC 102..112 (buttons 12-15 are the d-pad, already sent as CC 113..116 from the hat) */
    btns = ble_rd16(data + 13);
    for (i = 0; i < 11u; i++)
        hid_cc((uint8_t)(102u + i), (uint8_t)((btns >> i) & 1u ? 127u : 0u));

    bhid.shadow_valid = 1;
}

/* ---- error handling ---------------------------------------------------------------------- */

static void hid_error(const uint8_t *p)
{
    uint8_t code = p[4];
    if (code == HE_AUTHEN || code == HE_ENC || code == HE_KEY_SIZE) {
        if (ble_central_hid_auth(code))
            bhid.wait_sec = 1;
        else
            hid_fail(BLE_CF_AUTH, code);
        return;
    }
    switch (bhid.st) {
    case H_SVC:
        hid_fail(code == HE_NOT_FOUND ? BLE_CF_NO_HID : BLE_CF_GATT, code);
        return;
    case H_CHR:
        if (code == HE_NOT_FOUND) {
            hid_read_rmap();                       /* no more characteristics: proceed */
            return;
        }
        hid_fail(BLE_CF_GATT, code);
        return;
    case H_RMAP:
    case H_RMAP_BLOB:
        hid_read_pmode();                          /* Report Map unreadable: carry on */
        return;
    case H_PMODE_R:
        bhid.rpt_cur = 0;                         /* Protocol Mode unreadable: carry on */
        hid_process_rpt();
        return;
    case H_RPT_DSC:
        if (code == HE_NOT_FOUND) {                /* no descriptors: skip this report */
            if (bhid.rpt[bhid.rpt_cur].ref_h) {
                hid_rpt_read_ref();
                return;
            }
            bhid.rpt_cur++;
            hid_process_rpt();
            return;
        }
        hid_fail(BLE_CF_GATT, code);
        return;
    case H_RPT_REF:
        bhid.rpt_cur++;                            /* Report Reference unreadable: skip */
        hid_process_rpt();
        return;
    case H_RPT_CCCD_WRITE:
        hid_fail(BLE_CF_GATT, code);
        return;
    default:
        hid_fail(BLE_CF_GATT, code);
        return;
    }
}

/* ---- public API -------------------------------------------------------------------------- */

BLE_API void ble_hid_reset(void)
{
    ble_zero((uint8_t *)&bhid, sizeof bhid);
}

BLE_API void ble_hid_start(void)
{
    ble_hid_reset();
    BLE_DG(ble_dgc.hid_starts++);
    hid_step(H_MTU);
    bhid.req[0] = 0x02;                           /* Exchange MTU Request */
    ble_wr16(bhid.req + 1, BLE_ATT_MTU_MAX);
    bhid.req_n = 3;
    hid_send();
}

BLE_API void ble_hid_retry(void)
{
    if (!bhid.wait_sec)
        return;
    bhid.wait_sec = 0;
    hid_send();
}

BLE_API int ble_hid_ready(void) { return bhid.st == H_READY; }

BLE_API int ble_hid_rx(const uint8_t *p, uint16_t n)
{
    uint8_t op = p[0];
    if (op == 0x1Bu || op == 0x1Du) {              /* Handle Value Notification / Indication */
        uint16_t h;
        if (op == 0x1Du) {
            static const uint8_t conf = 0x1E;
            ble_ll_send(L2CAP_CID_ATT, &conf, 1);
        }
        if (n >= 3u && bhid.st == H_READY) {
            h = ble_rd16(p + 1);
            hid_report_in(h, p + 3, (uint16_t)(n - 3u));
        }
        return 1;
    }
    if (op != 0x01u && (!(op & 1u) || op > 0x19u))
        return 0;                                  /* requests and commands: the server's */
    if (bhid.st == H_IDLE || bhid.st == H_READY || bhid.st == H_FAIL || bhid.wait_sec)
        return 1;
    bhid.t_req = 0;
    if (op == 0x01u) {
        if (n == 5u && p[1] == bhid.req[0])
            hid_error(p);
        return 1;
    }
    if (op != (uint8_t)(bhid.req[0] + 1u))
        return 1;                                  /* not the answer to ours */
    switch (bhid.st) {
    case H_MTU:
        if (n == 3u) {
            uint16_t m = ble_rd16(p + 1);
            ble_att_set_mtu(m < 23u ? 23u : m);
        }
        hid_find_service();
        return 1;
    case H_SVC:
        if (n >= 2u)
            hid_svc_rsp(p, n);
        return 1;
    case H_CHR:
        if (n >= 2u)
            hid_chars_rsp(p, n);
        return 1;
    case H_RMAP:
        if (n >= 2u)
            hid_rmap_rsp(p, n);
        return 1;
    case H_RMAP_BLOB:
        if (n >= 2u)
            hid_rmap_blob_rsp(p, n);
        return 1;
    case H_PMODE_R:
        hid_pmode_rsp(p, n);
        return 1;
    case H_RPT_DSC:
        if (n >= 2u)
            hid_rpt_descs_rsp(p, n);
        return 1;
    case H_RPT_REF:
        if (n >= 2u)
            hid_rpt_ref_rsp(p, n);
        return 1;
    case H_RPT_CCCD_WRITE:                         /* Write Response: subscribed */
        bhid.rpt[bhid.rpt_cur].subscribed = 1;
        bhid.rpt_cur++;
        hid_process_rpt();
        return 1;
    default:
        return 1;
    }
}

BLE_API void ble_hid_event(uint32_t now)
{
    if (bhid.st == H_IDLE || bhid.st == H_READY || bhid.st == H_FAIL || bhid.wait_sec)
        return;
    if (bhid.due) {
        hid_send();
        return;
    }
    if (bhid.t_req && now - bhid.t_req > BLE_GATTC_TIMEOUT_US) {
        hid_fail(BLE_CF_GATT, 0xFF);
    }
}
