/* SPDX-License-Identifier: GPL-3.0-only */
/* SDK configuration stubs for vendor binary libraries.
 * These constants and functions are normally provided by the JieLi SDK.
 * Minimal values for WiFi STA with BLE peripheral (no classic BT). */
#include <stdint.h>

/* bt_profile_config.c */
const int config_btctler_modules = 0;
const int config_btctler_le_tws = 0;
const int config_btctler_mode = 0;
const int config_bt_function = 0;

/* lib_btctrler_config.c: LE peripheral only */
const int config_btctler_le_roles = 0x06;  /* LE_ADV | LE_SLAVE */
const int config_le_gatt_client_num = 0;
const int config_le_gatt_server_num = 1;
const int config_btctler_le_hw_nums = 1;
const int CONFIG_TEST_DUT_CODE = 0;
const int CONFIG_TEST_FCC_CODE = 0;
const int CONFIG_LMP_NAME_REQ_ENABLE = 0;
const int CONFIG_LMP_PASSKEY_ENABLE = 0;
const int CONFIG_LMP_MASTER_ESCO_ENABLE = 0;
const int CONFIG_BREDR_INQUIRY = 0;
const int CONFIG_BTCTLER_FAST_CONNECT_ENABLE = 0;
const int CONFIG_BTCTLER_TWS_ENABLE = 0;
const int config_btctler_dual_a2dp = 0;
const int config_btctler_le_master_multilink = 0;
const int config_multilink_support_esco_enalbe = 0;
const int config_le_sm_support_enable = 0;
const uint64_t config_btctler_le_features = 0;
const int config_btctler_le_acl_packet_length = 27;
const int config_btctler_le_slave_conn_update_params = 0;
const int config_btctler_le_slave_multilink = 0;
const int config_btctler_classic_enable = 0;
const int config_vendor_le_bb = 0;

/* lib_btstack_config.c */
const int config_btstack_big_flash_mode_enable = 0;
const int config_le_hci_connection_num = 1;
const int config_le_sm_mini_pair_enable = 0;
const int config_stack_modules = 0;
const int config_btstask_auto_exit_sniff = 0;
const int config_delete_link_key = 0;
const int config_force_bt_pwr_tab_using_normal_level = 0;

/* WiFi-specific */
const int WIFI_RXDATA_ENABLE = 1;
const int WIFI_HAL_CONNECT_ENABLE = 1;
const int CONFIG_WIFI_USE_TLSF_MEM = 0;
const int wifi_ampdu_disable = 0;

/* power management */
const int config_low_power_enable = 0;

/* Classic BT feature stubs (not used, vendor libs reference them) */
const int ESCO_FORWARD_ENABLE = 0;
const int hid_conn_depend_on_dev_company = 0;
const int a2dp_mutual_support = 0;
const int adt_profile_support = 0;
const int more_addr_reconnect_support = 0;
const int more_avctp_cmd_support = 0;
const int more_hfp_cmd_support = 0;
const int sniff_support_reset_anchor_point = 0;
const int btstack_emitter_support = 0;
const int ble_disable_wait_enable = 0;

/* TWS stub markers (empty linker sections) */
const char tws_func_stub_begin = 0;
const char tws_func_stub_end = 0;
const char tws_sync_channel_begin = 0;
const char tws_sync_channel_end = 0;
const char sdp_record_item_begin = 0;
const char sdp_record_item_end = 0;

/* Log tag constants (vendor libs reference these for debug filtering) */
const char log_tag_const_c_TWS_LE = 0;
const char log_tag_const_d_BL = 0;
const char log_tag_const_d_HCI_STD = 0;
const char log_tag_const_e_BL = 0;
const char log_tag_const_e_LE_BB = 0;
const char log_tag_const_e_LL_TWS_ADV = 0;
const char log_tag_const_e_LMP = 0;
const char log_tag_const_i_BL = 0;
const char log_tag_const_i_HCI_LMP = 0;
const char log_tag_const_i_LE_BB = 0;
const char log_tag_const_i_LL = 0;
const char log_tag_const_i_LL_TWS_ADV = 0;
const char log_tag_const_i_LMP = 0;
const char log_tag_const_i_TWS_LMP = 0;
const char log_tag_const_v_LMP = 0;

/* BT VM interface (vendor libs call to save/load BT config) */
void *bt_vm_interface = 0;

/* lwIP stubs (vendor wifi lib calls these but we don't use lwIP in nonfree) */
int Init_LwIP(void *arg) { (void)arg; return 0; }
int Unint_LwIP(void) { return 0; }
int lwip_netif_set_up(int up) { (void)up; return 0; }
int lwip_dhcp_release_and_stop(void) { return 0; }
int lwip_renew(void) { return 0; }
int dhcps_init(void *arg) { (void)arg; return 0; }
int dhcps_uninit(void) { return 0; }

/* Misc SDK stubs */
int sdfile_get_disk_capacity(void) { return 0; }
int lbuf_dump(void) { return 0; }
