#pragma once
#include "view.hpp"

// for margin
struct EmptyView : public FixedSizeView {
public :
	EmptyView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~EmptyView () {}
	
	void draw() const override {}
	void update(Hid_info key) override {}
};
