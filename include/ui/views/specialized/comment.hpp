#pragma once
#include <functional>
#include <vector>
#include <string>
#include "ui/ui_common.hpp"
#include "../view.hpp"

#define COMMENT_ICON_SIZE 48

struct CommentView : public FixedWidthView {
private :
	std::vector<std::string> content_lines;
public :
	using CallBackFuncType = std::function<void (const CommentView &)>;
	
	bool holding = false;
	CallBackFuncType on_author_icon_pressed_func;
	
	std::string author_name;
	std::string author_icon_url;
	std::string author_url;
	int author_icon_handle = -1;
	
	CallBackFuncType on_icon_select;
	
	using FixedWidthView::FixedWidthView;
	virtual ~CommentView () {}
	
	float get_height() const override { return std::max<int>(COMMENT_ICON_SIZE + SMALL_MARGIN, DEFAULT_FONT_INTERVAL * (1 + content_lines.size())) + SMALL_MARGIN; }
	
	CommentView *set_author_info(const std::string &author_name, const std::string &author_url, const std::string &author_icon_url) {
		this->author_name = author_name;
		this->author_url = author_url;
		this->author_icon_url = author_icon_url;
		return this;
	}
	CommentView *set_content_lines(const std::vector<std::string> &content_lines) { // mandatory
		this->content_lines = content_lines;
		return this;
	}
	CommentView *set_on_author_icon_pressed(CallBackFuncType on_author_icon_pressed_func) {
		this->on_author_icon_pressed_func = on_author_icon_pressed_func;
		return this;
	}
	
	void draw() const override;
	void update(Hid_info key) override;
};
