#pragma once
#include <string>
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct TextView : public FixedSizeView {
	UI::FlexibleString<TextView> text;
	double font_size = 0.5;
	double interval = DEFAULT_FONT_INTERVAL;
	bool x_centered = false;
	bool y_centered = true;
public :
	TextView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~TextView () {}
	
	TextView *set_text(UI::FlexibleString<TextView> text) {
		this->text = text;
		return this;
	}
	TextView *set_font_size(double font_size, double interval) {
		this->font_size = font_size;
		this->interval = interval;
		return this;
	}
	TextView *set_x_centered(bool x_centered) {
		this->x_centered = x_centered;
		return this;
	}
	TextView *set_y_centered(bool y_centered) {
		this->y_centered = y_centered;
		return this;
	}
	
	void draw() const override {
		int y = y_centered ? (int)((y0 + y1 - interval) / 2) : y0;
		if (x_centered) Draw_x_centered(text, x0 + SMALL_MARGIN, x1 - SMALL_MARGIN, y, font_size, font_size, DEFAULT_TEXT_COLOR);
		else Draw(text, x0 + SMALL_MARGIN, y, font_size, font_size, DEFAULT_TEXT_COLOR);
	}
	void update(Hid_info key) override {}
};
