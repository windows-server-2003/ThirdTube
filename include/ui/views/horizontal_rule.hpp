#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct HorizontalRuleView : public View {
public :
	using View::View;
	
	void draw() const override {
		int y = (int)((y0 + y1) / 2);
		Draw_line(x0 + SMALL_MARGIN, y, DEF_DRAW_BLACK, x1 - SMALL_MARGIN, y, DEF_DRAW_BLACK, 1);
	}
	void update(Hid_info key) override {}
};
