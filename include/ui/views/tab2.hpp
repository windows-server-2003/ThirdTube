#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

// Tab selector at the top, unfixed height

struct Tab2View : public FixedWidthView {
private :
	std::vector<UI::FlexibleString<Tab2View> > tab_texts;
public :
	using CallBackFuncType = std::function<void (const Tab2View &value)>;
	Tab2View (double x0, double y0, double width) : View(x0, y0), FixedWidthView(x0, y0, width) {}
	virtual ~Tab2View () {}
	
	std::vector<View *> views;
	double tab_selector_height = 20;
	double tab_selector_selected_line_height = 3;
	int selected_tab = 0;
	int tab_holding = -1;
	
	int get_tab_num() const { return std::min(tab_texts.size(), views.size()); }
	float tab_width() const { return (x1 - x0) / get_tab_num(); }
	float tab_pos_x(int tab) const { return x0 + (x1 - x0) * tab / get_tab_num(); }
	
	Tab2View *set_tab_texts(const std::vector<UI::FlexibleString<Tab2View> > &tab_texts) {
		this->tab_texts = tab_texts;
		return this;
	}
	Tab2View *set_views(const std::vector<View *> &views, int selected_tab = 0) {
		this->views = views;
		this->selected_tab = selected_tab;
		return this;
	}
	void on_scroll() override {
		tab_holding = -1;
		views[selected_tab]->on_scroll();
	}
	void reset_holding_status_() override {
		tab_holding = -1;
		views[selected_tab]->reset_holding_status();
	}
	void recursive_delete_subviews() override {
		for (auto view : views) {
			view->recursive_delete_subviews();
			delete view;
		}
		views.clear();
	}
	float get_height() const override {
		return tab_selector_height + views[selected_tab]->get_height();
	}
	
	
	void draw_() const override {
		int tab_num = get_tab_num();
		if (!tab_num) return;
		
		views[selected_tab]->draw(x0, y0 + tab_selector_height);
		
		Draw_texture(var_square_image[0], LIGHT1_BACK_COLOR, x0, y0, x1 - x0, tab_selector_height);
		Draw_texture(var_square_image[0], LIGHT2_BACK_COLOR, tab_pos_x(selected_tab), y0, tab_width(), tab_selector_height);
		Draw_texture(var_square_image[0], LIGHT3_BACK_COLOR, tab_pos_x(selected_tab), y0, tab_width(), tab_selector_selected_line_height);
		for (int i = 0; i < tab_num; i++) {
			float y = y0 + (tab_selector_height - Draw_get_height(tab_texts[i], 0.5, 0.5)) / 2;
			if (i == selected_tab) y -= 2;
			else y -= 3;
			Draw_x_centered(tab_texts[i], tab_pos_x(i), tab_pos_x(i + 1), y, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		}
	}
	void update_(Hid_info key) override {
		int tab_holded = -1;
		if (key.touch_y >= y0 && key.touch_y < y0 + tab_selector_height)
			tab_holded = std::max(0, std::min<int>(get_tab_num() - 1, (key.touch_x - x0) * get_tab_num() / (x1 - x0)));
		
		if (key.p_touch) tab_holding = tab_holded;
		if (key.touch_x == -1 && tab_holding != -1) selected_tab = tab_holding, var_need_reflesh = true;
		if (tab_holded != tab_holding) tab_holding = -1;
		
		views[selected_tab]->update(key, x0, y0 + tab_selector_height);
	}
};
