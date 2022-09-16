#pragma once
#include "view.hpp"

// for margin
struct EmptyView : public FixedSizeView {
public :
	EmptyView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~EmptyView () {}
	
	void draw_() const override {}
	void update_(Hid_info key) override {}
};
