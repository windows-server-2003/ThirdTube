#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct HorizontalRuleView : public FixedSizeView {
public :
	using FixedSizeView::FixedSizeView;
	virtual ~HorizontalRuleView () {}
	
	void draw() const override {
		int y = (int)((y0 + y1) / 2);
		Draw_line(x0 + SMALL_MARGIN, y, DEFAULT_TEXT_COLOR, x1 - SMALL_MARGIN, y, DEFAULT_TEXT_COLOR, 1);
	}
	void update(Hid_info key) override {}
};
