#pragma once
#include <vector>
#include "view.hpp"
#include "../ui_common.hpp"

struct SelectorView : public FixedSizeView {
private :
	UI::FlexibleString<SelectorView> title;
	
	static constexpr double MARGIN_BUTTON_RATIO = 5.0;
public :
	using CallBackFuncType = std::function<void (const SelectorView &value)>;
	int holding_button = -1;
	
	int selected_button = 0;
	int changed_num = 0;
	
	int button_num;
	std::vector<UI::FlexibleString<SelectorView> > button_texts;
	
	inline double get_title_height() const { return (std::string) title != "" ? DEFAULT_FONT_INTERVAL : 0; }
	inline double unit_num() const { return button_num * MARGIN_BUTTON_RATIO + (button_num + 1); }
	inline double margin_x_size() const { return (x1 - x0) / unit_num(); }
	inline double button_x_size() const { return margin_x_size() * MARGIN_BUTTON_RATIO; }
	inline double button_x_left(int button_id) const { return x0 + margin_x_size() * (1 + button_id * (1 + MARGIN_BUTTON_RATIO)); }
	inline double button_x_right(int button_id) const { return x0 + margin_x_size() * (button_id + 1) * (1 + MARGIN_BUTTON_RATIO); }
	inline double button_y_pos() const { return y0 + get_title_height() + (y1 - y0 - get_title_height()) * 0.1; }
	inline double button_y_size() const { return (y1 - y0 - get_title_height()) * 0.8; }
	inline double get_button_id_from_x(double x) const {
		int id = (x - x0) / margin_x_size() / (1 + MARGIN_BUTTON_RATIO);
		if (id < 0 || id >= button_num) return -1;
		double remainder = (x - x0) - id * margin_x_size() * (1 + MARGIN_BUTTON_RATIO);
		if (remainder >= margin_x_size() && remainder <= margin_x_size() * (1 + MARGIN_BUTTON_RATIO)) return id;
		return -1;
	}
	
	CallBackFuncType on_change_func;
	
	SelectorView (double x0, double y0, double width, double height) : View(x0, y0), FixedSizeView(x0, y0, width, height) {}
	virtual ~SelectorView () {}
	
	void reset_holding_status_() override {
		holding_button = -1;
	}
	
	SelectorView *set_texts(const std::vector<UI::FlexibleString<SelectorView> > &button_texts, int init_selected) { // mandatory
		this->button_num = button_texts.size();
		this->button_texts = button_texts;
		this->selected_button = init_selected;
		return this;
	}
	/*
	SelectorView *set_texts(const std::vector<std::function<std::string (const SelectorView &)> > &button_texts, int init_selected) { // mandatory
		this->button_num = button_texts.size();
		this->button_texts.resize(button_num);
		for (int i = 0; i < button_num; i++) this->button_texts[i] = UI::FlexibleString<SelectorView>(button_texts[i], *this);
		this->selected_button = init_selected;
		return this;
	}*/
	SelectorView *set_title(const std::string &title) {
		this->title = UI::FlexibleString<SelectorView>(title);
		return this;
	}
	SelectorView *set_title(std::function<std::string (const SelectorView &)> title_func) {
		this->title = UI::FlexibleString<SelectorView>(title_func, *this);
		return this;
	}
	SelectorView *set_on_change(CallBackFuncType on_change_func) {
		this->on_change_func = on_change_func;
		return this;
	}
	
	void draw_() const override {
		Draw(title, x0 + SMALL_MARGIN, y0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, button_x_left(selected_button), button_y_pos(), button_x_size(), button_y_size());
		for (int i = 0; i < button_num; i++)
			Draw_x_centered(button_texts[i], button_x_left(i), button_x_right(i), button_y_pos(), 0.5, 0.5, DEFAULT_TEXT_COLOR);
	}
	void update_(Hid_info key) override {
		if (key.p_touch && key.touch_y >= button_y_pos() && key.touch_y < button_y_pos() + button_y_size())
			holding_button = get_button_id_from_x(key.touch_x);
		
		int current_holding_button = get_button_id_from_x(key.touch_x);
		if (key.touch_y != -1 && (key.touch_y < button_y_pos() || key.touch_y >= button_y_pos() + button_y_size())) holding_button = -1;
		
		if (holding_button != -1 && current_holding_button != -1 && holding_button != current_holding_button) holding_button = -1;
		if (holding_button != -1 && key.touch_x == -1) {
			selected_button = holding_button;
			var_need_reflesh = true;
			if (on_change_func) on_change_func(*this);
			changed_num++;
			holding_button = -1;
		}
	}
};
