#pragma once
#include <string>
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct TextView : public FixedSizeView {
	std::function<u32 ()> get_text_color = [] () { return DEFAULT_TEXT_COLOR; };
	std::vector<UI::FlexibleString<TextView> > text;
	double font_size = 0.5;
	double interval = DEFAULT_FONT_INTERVAL;
public :
	TextView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~TextView () {}
	
	float text_x_offset = 0;
	float text_y_offset = 0;
	
	enum class XAlign {
		LEFT,
		CENTER,
		RIGHT
	};
	XAlign x_align = XAlign::LEFT;
	enum class YAlign {
		UP,
		CENTER
	};
	YAlign y_align = YAlign::CENTER;
	
	TextView *set_text(UI::FlexibleString<TextView> text) {
		this->text = { text };
		return this;
	}
	template<class T> TextView *set_text_lines(std::vector<T> text) {
		this->text.clear();
		for (auto i : text) this->text.push_back(i);
		return this;
	}
	TextView *set_font_size(double font_size, double interval) {
		this->font_size = font_size;
		this->interval = interval;
		return this;
	}
	TextView *set_x_alignment(XAlign x_align) {
		this->x_align = x_align;
		return this;
	}
	TextView *set_y_alignment(YAlign y_align) {
		this->y_align = y_align;
		return this;
	}
	TextView *set_text_offset(float text_x_offset, float text_y_offset) {
		this->text_x_offset = text_x_offset;
		this->text_y_offset = text_y_offset;
		return this;
	}
	TextView *set_get_text_color(std::function<u32 ()> get_text_color) {
		this->get_text_color = get_text_color;
		return this;
	}
	
	void draw_() const override {
		int y_start = y_align == YAlign::CENTER ? (int)((y0 + y1 - interval * text.size()) / 2) : y0;
		for (size_t i = 0; i < text.size(); i++) {
			float cur_y = y_start + i * interval + text_y_offset;
			if (cur_y > 240 || cur_y < -50) continue;
			if (x_align == XAlign::LEFT)
				Draw(text[i], x0 + SMALL_MARGIN + text_x_offset, cur_y, font_size, font_size, get_text_color());
			else if (x_align == XAlign::CENTER)
				Draw_x_centered(text[i], x0 + SMALL_MARGIN + text_x_offset, x1 - SMALL_MARGIN + text_x_offset, cur_y, font_size, font_size, get_text_color());
			else 
				Draw_right(text[i], x1 - SMALL_MARGIN + text_x_offset, cur_y, font_size, font_size, get_text_color());
		}
	}
	void update_(Hid_info key) override {}
};
