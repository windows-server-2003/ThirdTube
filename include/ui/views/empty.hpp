#pragma once
#include "view.hpp"

// for margin
struct EmptyView : public FixedSizeView {
public :
	using FixedSizeView::FixedSizeView;
	virtual ~EmptyView () {}
	
	void draw() const override {}
	void update(Hid_info key) override {}
};
