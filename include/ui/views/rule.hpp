#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct RuleView : public FixedSizeView {
private :
	std::function<u32 ()> get_color = [] () { return DEFAULT_TEXT_COLOR; };
	int margin = SMALL_MARGIN;
	bool is_vertical = false;
	
public :
	RuleView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~RuleView () {}
	
	RuleView *set_get_color(std::function<u32 ()> get_color) {
		this->get_color = get_color;
		return this;
	}
	RuleView *set_margin(int margin) {
		this->margin = margin;
		return this;
	}
	RuleView *set_is_vertical(bool is_vertical) {
		this->is_vertical = is_vertical;
		return this;
	}
	
	void draw_() const override {
		if (is_vertical) {
			int x = (int) ((x0 + x1) / 2);
			Draw_line(x, y0 + margin, get_color(), x, y1 - margin, get_color(), 1);
		} else {
			int y = (int)((y0 + y1) / 2);
			Draw_line(x0 + margin, y, get_color(), x1 - margin, y, get_color(), 1);
		}
	}
	void update_(Hid_info key) override {}
};
