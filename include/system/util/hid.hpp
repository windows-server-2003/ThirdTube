#pragma once

void Util_hid_update_key_state(); // should be called exatly once every frame
void Util_hid_query_key_state(Hid_info* out_key_state);

