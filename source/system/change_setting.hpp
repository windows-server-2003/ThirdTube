#pragma once
#include "types.hpp"

Result_with_string Util_cset_set_screen_brightness(bool top_screen, bool bottom_screen, int brightness);

Result_with_string Util_cset_set_wifi_state(bool wifi_state);

Result_with_string Util_cset_set_screen_state(bool top_screen, bool bottom_screen, bool state);
