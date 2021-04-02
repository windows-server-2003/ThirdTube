#pragma once

void Util_hid_init(void);

void Util_hid_exit(void);

void Util_hid_query_key_state(Hid_info* out_key_state);

void Util_hid_key_flag_reset(void);

void Util_hid_scan_hid_thread(void* arg);
