#include "headers.hpp"

void Logger::init() {
	osTickCounterStart(&stopwatch);
}
void Logger::log(LogLevel level, const std::string &str) {
	content_lock.lock();
	// get time
	osTickCounterUpdate(&stopwatch);
	acc_time += osTickCounterRead(&stopwatch) / 1000;
	
	char time_str[32] = { 0 };
	snprintf(time_str, 32, "[%.5f]", acc_time);
	
	if (draw_offset_y + DRAW_LINES <= (int) logs.size()) draw_offset_y++;
	logs.push_back({acc_time, level, std::string(time_str) + " " + str.substr(0, 120)});
	if (logs.size() > MAX_BUFFERED_LINES) logs.pop_front(), draw_offset_y--;
	if (draw_enabled) var_need_reflesh = true;
	content_lock.unlock();
}
void Logger::update(Hid_info key) {
	if (draw_enabled) { // move only if drawing is enabled
		content_lock.lock();
		float draw_offset_x_old = draw_offset_x;
		int draw_offset_y_old = draw_offset_y;
		if (key.h_c_up)    draw_offset_y = std::max(0, draw_offset_y - 1);
		if (key.h_c_down)  draw_offset_y = std::min((int) logs.size() - 1, draw_offset_y + 1);
		if (key.h_c_left)  draw_offset_x = std::max(0.0f, draw_offset_x - XSCROLL_SPEED);
		if (key.h_c_right) draw_offset_x = std::min(XSCROLL_MAX, draw_offset_x + XSCROLL_SPEED);
		if (draw_offset_x != draw_offset_x_old || draw_offset_y != draw_offset_y_old) var_need_reflesh = true;
		content_lock.unlock();
	}
}

void Logger::draw() {
	if (draw_enabled) {
		content_lock.lock();
		for (int i = 0; i < DRAW_LINES && draw_offset_y + i < (int) logs.size(); i++) {
			auto &cur_log = logs[draw_offset_y + i];
			Draw(cur_log.str, -draw_offset_x, (1 + i) * draw_y_interval, font_size, font_size, get_log_color(cur_log.level));
		}
		content_lock.unlock();
	}
}
size_t Logger::get_memory_consumption() {
	content_lock.lock();
	size_t res = 0;
	res += sizeof(Logger);
	res += sizeof(logs[0]) * logs.size();
	for (auto &i : logs) res += i.str.size();
	content_lock.unlock();
	return res;
}
Logger logger; // global logger
