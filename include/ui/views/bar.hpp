#pragma once
#include "view.hpp"
#include "../ui_common.hpp"

struct BarView : public View {
private :
	UI::FlexibleString<BarView> title;
public :
	using CallBackFuncType = std::function<void (const BarView &value)>;
	bool holding = false;
	
	double low;
	double high;
	double value;
	
	inline double bar_x0() const { return x0 + (x1 - x0) * 0.1; }
	inline double bar_x1() const { return x0 + (x1 - x0) * 0.9; }
	inline double bar_len() const { return (x1 - x0) * 0.8; }
	inline double get_x_from_value(double value) const { return bar_x0() + bar_len() * (value - low) / (high - low); }
	inline double get_value_from_x(double x) const { return std::max(low, std::min(high, low + (high - low) * (x - bar_x0()) / bar_len())); }
	inline double bar_y() const { return (int) ((y0 + DEFAULT_FONT_INTERVAL + y1) / 2); }
	
	CallBackFuncType on_release_func;
	CallBackFuncType while_holding_func;
	
	using View::View;
	BarView *set_values(double low, double high, double init_val) { // mandatory
		this->low = low;
		this->high = high;
		this->value = init_val;
		return this;
	}
	BarView *set_title(const std::string &title) {
		this->title = UI::FlexibleString<BarView>(title);
		return this;
	}
	BarView *set_title(std::function<std::string (const BarView &)> title_func) {
		this->title = UI::FlexibleString<BarView>(title_func, *this);
		return this;
	}
	BarView *set_on_release(CallBackFuncType on_release_func) {
		this->on_release_func = on_release_func;
		return this;
	}
	BarView *set_while_holding(CallBackFuncType while_holding_func) {
		this->while_holding_func = while_holding_func;
		return this;
	}
	
	void draw() const override {
		Draw(title, x0 + SMALL_MARGIN, y0, 0.5, 0.5, DEFAULT_TEXT_COLOR);
		Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, bar_x0(), bar_y() - 1, bar_len(), 3);
		Draw_texture(var_square_image[0], DEF_DRAW_WEAK_AQUA, bar_x0(), bar_y() - 1, get_x_from_value(value) - bar_x0(), 3);
		C2D_DrawCircleSolid(get_x_from_value(value), bar_y(), 0, holding ? 6 : 4, DEF_DRAW_WEAK_AQUA);
	}
	void update(Hid_info key) override {
		if (key.p_touch && key.touch_y >= bar_y() - 5 && key.touch_y <= bar_y() + 5 && key.touch_x >= bar_x0() - 5 && key.touch_x <= bar_x1() + 5) 
			holding = true;
		if (holding && key.touch_x == -1) {
			var_need_reflesh = true;
			if (on_release_func) on_release_func(*this);
			holding = false;
		}
		if (holding) value = get_value_from_x(key.touch_x);
		if (holding && while_holding_func) while_holding_func(*this);
	}
};
