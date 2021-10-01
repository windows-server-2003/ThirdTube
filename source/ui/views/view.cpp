#include <functional>
#include "ui/views/view.hpp"
#include "ui/colors.hpp"

const std::function<u32 (const View &)> View::STANDARD_BACKGROUND = [] (const View &view) {
	int darkness = std::min<int>(0xFF, 0xD0 + 0x30 * (1 - view.touch_darkness));
	if (var_night_mode) darkness = 0xFF - darkness;
	return COLOR_GRAY(darkness);
};
