#pragma once
#include "view.hpp"

struct ViewReferenceView : public View {
public :
	ViewReferenceView () : View(0, 0) {}
	virtual ~ViewReferenceView () {}
	
	View *subview = NULL;
	
	float get_height() const override { return subview ? subview->get_height() : 0; }
	float get_width() const override { return subview ? subview->get_width() : 0; }
	void on_scroll() override { if (subview) subview->on_scroll(); }
	void reset_holding_status_() override { if (subview) subview->reset_holding_status(); }
	
	void recursive_delete_subviews() override {
		if (subview) subview->recursive_delete_subviews();
		delete subview;
		subview = NULL;
	}
	
	// direct access to `views` is also allowed
	ViewReferenceView *set_subview(View *subview) {
		this->subview = subview;
		return this;
	}
	
	void draw_() const override { if (subview) subview->draw(); }
	void update_(Hid_info key) override { if (subview) subview->update(key); }
};

