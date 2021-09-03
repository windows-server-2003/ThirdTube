#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct HorizontalRuleView : public FixedSizeView {
public :
	HorizontalRuleView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~HorizontalRuleView () {}
	
	void draw_() const override {
		int y = (int)((y0 + y1) / 2);
		Draw_line(x0 + SMALL_MARGIN, y, DEFAULT_TEXT_COLOR, x1 - SMALL_MARGIN, y, DEFAULT_TEXT_COLOR, 1);
	}
	void update_(Hid_info key) override {}
};
