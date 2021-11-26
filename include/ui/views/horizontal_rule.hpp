#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct HorizontalRuleView : public FixedSizeView {
private :
	std::function<u32 ()> get_color = [] () { return DEFAULT_TEXT_COLOR; };
public :
	HorizontalRuleView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~HorizontalRuleView () {}
	
	HorizontalRuleView *set_get_color(std::function<u32 ()> get_color) {
		this->get_color = get_color;
		return this;
	}
	
	void draw_() const override {
		int y = (int)((y0 + y1) / 2);
		Draw_line(x0 + SMALL_MARGIN, y, get_color(), x1 - SMALL_MARGIN, y, get_color(), 1);
	}
	void update_(Hid_info key) override {}
};
