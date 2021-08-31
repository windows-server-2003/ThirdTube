#pragma once
#include <functional>
#include <vector>
#include <string>
#include "ui/ui_common.hpp"
#include "../view.hpp"
#include "system/util/string_resource.hpp"

#define COMMENT_ICON_SIZE 48

struct CommentView : public FixedWidthView {
private :
	std::vector<std::string> content_lines;
	
	float content_x_pos() const { return x0 + SMALL_MARGIN * 2 + COMMENT_ICON_SIZE; }
	bool is_inside_author_icon(float x, float y) const {
		return x >= x0 && x < std::min<float>(x1, x0 + COMMENT_ICON_SIZE + SMALL_MARGIN) && y >= y0 && y < y0 + COMMENT_ICON_SIZE;
	}
	bool is_inside_show_more_button(float x, float y) const {
		int show_more_y = y0 + (lines_shown + 1) * DEFAULT_FONT_INTERVAL + SMALL_MARGIN;
		return lines_shown < content_lines.size() && x >= content_x_pos() && x < std::min<float>(x1, content_x_pos() + Draw_get_width(LOCALIZED(SHOW_MORE), 0.5, 0.5)) &&
			y >= show_more_y && y < show_more_y + DEFAULT_FONT_INTERVAL;
	}
		
public :
	using CallBackFuncType = std::function<void (const CommentView &)>;
	
	size_t lines_shown = 0; // 3 + 50n
	bool icon_holding = false;
	bool show_more_holding = false;
	CallBackFuncType on_author_icon_pressed_func;
	
	std::string author_name;
	std::string author_icon_url;
	std::string author_url;
	int author_icon_handle = -1;
	
	CallBackFuncType on_icon_select;
	
	using FixedWidthView::FixedWidthView;
	virtual ~CommentView () {}
	
	float get_height() const override {
		float left_height = COMMENT_ICON_SIZE + SMALL_MARGIN;
		float right_height = DEFAULT_FONT_INTERVAL * (1 + lines_shown) + SMALL_MARGIN;
		if (lines_shown < content_lines.size()) right_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL; // "Show more"
		return std::max(left_height, right_height) + SMALL_MARGIN; // add margin between comments
	}
	void on_scroll() override {
		icon_holding = false;
		show_more_holding = false;
	}
	
	CommentView *set_author_info(const std::string &author_name, const std::string &author_url, const std::string &author_icon_url) {
		this->author_name = author_name;
		this->author_url = author_url;
		this->author_icon_url = author_icon_url;
		return this;
	}
	CommentView *set_content_lines(const std::vector<std::string> &content_lines) { // mandatory
		this->content_lines = content_lines;
		this->lines_shown = std::min<size_t>(3, content_lines.size());
		return this;
	}
	CommentView *set_on_author_icon_pressed(CallBackFuncType on_author_icon_pressed_func) {
		this->on_author_icon_pressed_func = on_author_icon_pressed_func;
		return this;
	}
	
	void draw() const override;
	void update(Hid_info key) override;
};
