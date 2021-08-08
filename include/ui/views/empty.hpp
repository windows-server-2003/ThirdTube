#pragma once
#include "view.hpp"

// for margin
struct EmptyView : public View {
public :
	using View::View;
	void draw() const override {}
	void update(Hid_info key) override {}
};
