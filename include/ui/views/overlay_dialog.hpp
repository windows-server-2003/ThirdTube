#pragma once
#include "overlay.hpp"

// for margin
struct OverlayDialogView : public OverlayView {
public :
	static constexpr int DEFAULT_DIALOG_WIDTH = 240;
	static constexpr int DEFAULT_DIALOG_MESSAGE_HEIGHT = 30;
	static constexpr int DEFAULT_DIALOG_BUTTON_HEIGHT = 25;
	using CallBackFuncType = std::function<bool (OverlayDialogView &, int)>;
	
	CallBackFuncType on_button_pressed;
	
	VerticalListView *get_main_vertical_list_view() { return dynamic_cast<VerticalListView *>(subview); }
	TextView *get_message_view() { return dynamic_cast<TextView *>(get_main_vertical_list_view()->views[0]); }
	HorizontalListView *get_buttons_view() { return dynamic_cast<HorizontalListView *>(get_main_vertical_list_view()->views[2]); }
	
	OverlayDialogView (double x0, double y0, double width, double height) : View(x0, y0), OverlayView(x0, y0, width, height) {
		subview = (new VerticalListView(0, 0, DEFAULT_DIALOG_WIDTH))
			->set_views({
				(new TextView(0, 0, DEFAULT_DIALOG_WIDTH, DEFAULT_DIALOG_MESSAGE_HEIGHT))
					->set_x_alignment(TextView::XAlign::CENTER)
					->set_text_offset(0, -1),
				(new RuleView(0, 0, DEFAULT_DIALOG_WIDTH, 1))
					->set_get_color([] () { return LIGHT2_BACK_COLOR; })
					->set_margin(0),
				(new HorizontalListView(0, 0, DEFAULT_DIALOG_BUTTON_HEIGHT))
			})
			->set_draw_order({0, 2, 1})
			->set_get_background_color([] (const View &) { return DEFAULT_BACK_COLOR; });
		set_on_cancel([] (OverlayView &view) {
			view.set_is_visible(false);
			var_need_reflesh = true;
		});
	}
	template<typename T> OverlayDialogView *set_buttons(const std::vector<T> &button_strs, CallBackFuncType on_button_pressed) {
		this->on_button_pressed = on_button_pressed;
		
		HorizontalListView *buttons_view = get_buttons_view();
		buttons_view->recursive_delete_subviews();
		
		size_t n = button_strs.size();
		for (size_t i = 0; i < button_strs.size(); i++) {
			int width = (DEFAULT_DIALOG_WIDTH + 1) / n;
			if (i < (DEFAULT_DIALOG_WIDTH + 1) % n) width++;
			
			buttons_view->views.push_back(
				(new TextView(0, 0, width - 1, DEFAULT_DIALOG_BUTTON_HEIGHT))
					->set_text(button_strs[i])
					->set_text_offset(0, -1)
					->set_x_alignment(TextView::XAlign::CENTER)
					->set_get_background_color(View::STANDARD_BACKGROUND)
					->set_on_view_released([this, i] (View &) {
						bool close = true;
						if (this->on_button_pressed && !this->on_button_pressed(*this, i)) close = false;
						if (close) this->set_is_visible(false);
						var_need_reflesh = true;
					})
			);
			if (i + 1 < button_strs.size()) buttons_view->views.push_back(
				(new RuleView(0, 0, 1, DEFAULT_DIALOG_BUTTON_HEIGHT))
					->set_get_color([] () { return LIGHT2_BACK_COLOR; })
					->set_margin(0)
					->set_is_vertical(true)
			);
		}
		return this;
	}
	virtual ~OverlayDialogView () {}
};
