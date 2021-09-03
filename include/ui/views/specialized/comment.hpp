#pragma once
#include <functional>
#include <vector>
#include <string>
#include "youtube_parser/parser.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"
#include "system/util/string_resource.hpp"

#define COMMENT_ICON_SIZE 48
#define REPLY_ICON_SIZE 32


struct CommentView : public FixedWidthView {
private :
	std::vector<std::string> content_lines;
	bool icon_holding = false;
	bool show_more_holding = false;
	bool fold_replies_holding = false;
	bool show_more_replies_holding = false;
	
	static inline bool in_range(float x, float l, float r) { return x >= l && x < r; }
	
	// position-related functions
	inline float get_icon_size() const { return is_reply ? REPLY_ICON_SIZE : COMMENT_ICON_SIZE; }
	float content_x_pos() const { return x0 + SMALL_MARGIN * 2 + get_icon_size(); }
	float left_height() const { return get_icon_size() + SMALL_MARGIN; }
	float right_height() const {
		float res = DEFAULT_FONT_INTERVAL * (1 + lines_shown);
		if (lines_shown < content_lines.size()) res += SMALL_MARGIN + DEFAULT_FONT_INTERVAL; // "Show more"
		return res;
	}
public :
	using GetYTCommentObjectFuncType = std::function<YouTubeVideoDetail::Comment &(const CommentView &)>;
	using CallBackFuncType = std::function<void (const CommentView &)>;
	using CallBackFuncTypeModifiable = std::function<void (CommentView &)>;
	
	size_t lines_shown = 0; // 3 + 50n
	size_t replies_shown = 0;
	bool is_reply = false;
	volatile bool is_loading_replies = false;
	
	GetYTCommentObjectFuncType get_yt_comment_object_func;
	YouTubeVideoDetail::Comment &get_yt_comment_object() const { return get_yt_comment_object_func(*this); }
	CallBackFuncType on_author_icon_pressed_func;
	CallBackFuncTypeModifiable on_load_more_replies_pressed_func;
	
	int author_icon_handle = -1;
	
	std::vector<CommentView *> replies;
	
	CommentView (double x0, double y0, double width) : View(x0, y0), FixedWidthView(x0, y0, width) {}
	virtual ~CommentView () {}
	
	void recursive_delete_subviews() override {
		for (auto reply_view : replies) {
			reply_view->recursive_delete_subviews();
			delete reply_view;
		}
		replies.clear();
		replies_shown = 0;
	}
	float get_height() const override {
		float main_height = std::max(left_height(), right_height());
		float reply_height = 0;
		if (replies_shown) reply_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL + SMALL_MARGIN; // fold replies
		for (size_t i = 0; i < replies_shown; i++) reply_height += replies[i]->get_height();
		if (get_yt_comment_object().has_more_replies() || replies_shown < replies.size()) reply_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL; // load more replies
		
		return main_height + reply_height + SMALL_MARGIN; // add margin between comments
	}
	float get_self_height() { return std::max(left_height(), right_height()); }
	void on_scroll() override {
		icon_holding = false;
		show_more_holding = false;
		fold_replies_holding = false;
		show_more_replies_holding = false;
		for (auto reply_view : replies) reply_view->on_scroll();
	}
	
	std::vector<std::pair<float, CommentView *> > get_reply_pos_list() {
		std::vector<std::pair<float, CommentView *> > res;
		float cur_y = std::max(left_height(), right_height());
		if (replies_shown) cur_y += SMALL_MARGIN + DEFAULT_FONT_INTERVAL + SMALL_MARGIN; // fold replies
		for (size_t i = 0; i < replies_shown; i++) {
			res.push_back({cur_y, replies[i]});
			cur_y += replies[i]->get_height();
		}
		return res;
	}
	
	CommentView *set_content_lines(const std::vector<std::string> &content_lines) { // mandatory
		this->content_lines = content_lines;
		this->lines_shown = std::min<size_t>(3, content_lines.size());
		return this;
	}
	CommentView *set_get_yt_comment_object(GetYTCommentObjectFuncType get_yt_comment_object_func) { // mandatory
		this->get_yt_comment_object_func = get_yt_comment_object_func;
		return this;
	}
	CommentView *set_on_author_icon_pressed(CallBackFuncType on_author_icon_pressed_func) {
		this->on_author_icon_pressed_func = on_author_icon_pressed_func;
		return this;
	}
	CommentView *set_on_load_more_replies_pressed(CallBackFuncTypeModifiable on_load_more_replies_pressed_func) {
		this->on_load_more_replies_pressed_func = on_load_more_replies_pressed_func;
		return this;
	}
	CommentView *set_is_reply(bool is_reply) {
		this->is_reply = is_reply;
		return this;
	}
	
	void draw_() const override;
	void update_(Hid_info key) override;
};
