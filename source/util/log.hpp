#pragma once
#include "definitions.hpp"
#include "system/libctru_wrapper.hpp"
#include "ui/colors.hpp"
#include <deque>
#include <string>
#include <cinttypes>

enum class LogLevel {
	INFO,
	CAUTION,
	WARNING,
	ERROR,
};


class Logger {
	// Drawing related
public :
	bool draw_enabled = false;
	const size_t MAX_BUFFERED_LINES = 1024;
private :
	float draw_offset_x = 0;
	int draw_offset_y = 0; // line num
	static constexpr int draw_y_interval = 10;
	static constexpr int DRAW_LINES = 23;
	static constexpr float font_size = 0.4;
	static constexpr float XSCROLL_SPEED = 5;
	static constexpr float XSCROLL_MAX = 300;
	
	// Timing
	TickCounter stopwatch;
	double acc_time = 0; // accumulated time from the initialization
	
	// Content
	struct Log {
		double time;
		LogLevel level;
		std::string str;
	};
	Mutex content_lock;
	std::deque<Log> logs;
	
	u32 get_log_color(LogLevel level) {
		if (level == LogLevel::INFO) return 0xFFBBBB00; // aqua
		if (level == LogLevel::CAUTION) return 0xFF00C5FF; // yellow
		if (level == LogLevel::WARNING) return 0xFF0078FF; // orange
		if (level == LogLevel::ERROR) return 0xFF0000FF; // red
		my_assert(0);
		return 0;
	}
	std::string to_hex(u32 code) {
		char res[32] = { 0 };
		snprintf(res, 32, "%" PRIx32, code);
		return res;
	}
public :
	void init();
	void log(LogLevel level, const std::string &str);
	void log(LogLevel level, const std::string &str, u32 code) { log(level, str + " " + to_hex(code)); }
	void log(LogLevel level, const std::string &module_name, const std::string &str) { log(level, "[" + module_name + "]" + str); }
	void log(LogLevel level, const std::string &module_name, const std::string &str, u32 code) { log(level, "[" + module_name + "]" + str, code); }
#	define DEFINE_VARIANT(func_name, log_level) template<typename... T> void func_name(T&&... args) { log(LogLevel::log_level, std::forward<T>(args)...); }
	DEFINE_VARIANT(info, INFO);
	DEFINE_VARIANT(caution, CAUTION);
	DEFINE_VARIANT(warning, WARNING);
	DEFINE_VARIANT(error, ERROR);
	
	void update(Hid_info key); // called per frame
	void draw(); // draw logs if `draw_enabled`
	size_t get_memory_consumption();
};
extern Logger logger;
