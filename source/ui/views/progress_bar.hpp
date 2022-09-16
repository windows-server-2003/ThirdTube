#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// simple horizontal line
struct ProgressBarView : public FixedSizeView {
	std::function<u32 ()> get_color = [] () { return DEFAULT_TEXT_COLOR; };
	double progress = 0;
	double progress_displayed = 0;
	double progress_displayed_change_speed = 0.05;
public :
	ProgressBarView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~ProgressBarView () {}
	
	ProgressBarView *set_progress(double progress) {
		this->progress = progress;
		return this;
	}
	ProgressBarView *set_get_color(std::function<u32 ()> get_color) {
		this->get_color = get_color;
		return this;
	}
	
	void draw_() const override {
		Draw_line(x0 + SMALL_MARGIN, y0, get_color(), x1 - SMALL_MARGIN, y0, get_color(), 1);
		Draw_line(x0 + SMALL_MARGIN, y0, get_color(), x0 + SMALL_MARGIN, y1, get_color(), 1);
		Draw_line(x0 + SMALL_MARGIN, y1 - 1, get_color(), x1 - SMALL_MARGIN, y1 - 1, get_color(), 1);
		Draw_line(x1 - SMALL_MARGIN, y0, get_color(), x1 - SMALL_MARGIN, y1, get_color(), 1);
		double bar_width = get_width() - SMALL_MARGIN * 2;
		int width = std::min(bar_width, (bar_width + SMALL_MARGIN) * progress_displayed);
		Draw_texture(var_square_image[0], get_color(), (int) x0 + SMALL_MARGIN, (int) y0, width, (int) get_height());
	}
	void update_(Hid_info key) override {
		if (progress != progress_displayed) var_need_reflesh = true;
		progress_displayed = std::min(progress, progress_displayed + (progress - progress_displayed) * progress_displayed_change_speed);
	}
};
