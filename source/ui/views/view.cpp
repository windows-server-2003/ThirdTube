#include <functional>
#include "ui/views/view.hpp"
#include "ui/colors.hpp"
#include "variables.hpp"

const std::function<u32 (const View &)> View::STANDARD_BACKGROUND = [] (const View &view) {
	int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
	if (var_night_mode) darkness = 0xFF - darkness;
	if (view.touch_darkness > 0 && view.touch_darkness < 1) var_need_reflesh = true;
	return COLOR_GRAY(darkness);
};
