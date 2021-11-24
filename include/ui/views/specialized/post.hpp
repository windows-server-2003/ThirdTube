#pragma once
#include <functional>
#include <vector>
#include <string>
#include "youtube_parser/parser.hpp"
#include "ui/ui_common.hpp"
#include "../view.hpp"
#include "succinct_video.hpp"
#include "system/util/string_resource.hpp"
#include "variables.hpp"

#define POST_ICON_SIZE 48
#define REPLY_ICON_SIZE 32
#define COMMUNITY_IMAGE_SIZE (var_community_image_size)


// used for comments and community posts
struct PostView : public FixedWidthView {
private :
	std::string author_name;
	std::string time_str;
	std::string upvote_str;
	std::vector<std::string> content_lines;
	bool icon_holding = false;
	bool show_more_holding = false;
	bool fold_replies_holding = false;
	bool show_more_replies_holding = false;
	
	static inline bool in_range(float x, float l, float r) { return x >= l && x < r; }
	
	// position-related functions
	inline float get_icon_size() const { return is_reply ? REPLY_ICON_SIZE : POST_ICON_SIZE; }
	float content_x_pos() const { return x0 + SMALL_MARGIN * 2 + get_icon_size(); }
	float left_height() const { return get_icon_size() + SMALL_MARGIN; }
	float right_height() const {
		float res = DEFAULT_FONT_INTERVAL * (1 + lines_shown);
		if (lines_shown < content_lines.size()) res += SMALL_MARGIN + DEFAULT_FONT_INTERVAL; // "Show more"
		return res;
	}
public :
	using GetYTCommentObjectFuncType = std::function<YouTubeVideoDetail::Comment &(const PostView &)>;
	using CallBackFuncType = std::function<void (const PostView &)>;
	using CallBackFuncTypeModifiable = std::function<void (PostView &)>;
	
	size_t lines_shown = 0; // 3 + 50n
	size_t replies_shown = 0;
	bool is_reply = false;
	volatile bool is_loading_replies = false;
	
	std::function<bool ()> get_has_more_replies;
	CallBackFuncType on_author_icon_pressed_func;
	CallBackFuncTypeModifiable on_load_more_replies_pressed_func;
	
	int author_icon_handle = -1;
	std::string author_icon_url;
	
	// for images attached to community posts
	int additional_image_handle = -1;
	std::string additional_image_url;
	// for video preview in community posts
	SuccinctVideoView *additional_video_view = NULL;
	
	void cancel_all_thumbnail_requests();
	
	std::vector<PostView *> replies;
	
	PostView (double x0, double y0, double width) : View(x0, y0), FixedWidthView(x0, y0, width) {}
	virtual ~PostView () {}
	
	void recursive_delete_subviews() override {
		for (auto reply_view : replies) {
			reply_view->recursive_delete_subviews();
			delete reply_view;
		}
		replies.clear();
		if (additional_video_view) {
			additional_video_view->recursive_delete_subviews();
			delete additional_video_view;
			additional_video_view = NULL;
		}
		replies_shown = 0;
	}
	void reset_holding_status_() override {
		icon_holding = false;
		show_more_holding = false;
		fold_replies_holding = false;
		show_more_replies_holding = false;
		for (auto reply_view : replies) reply_view->reset_holding_status();
		if (additional_video_view) additional_video_view->reset_holding_status();
	}
	void on_scroll() override {
		icon_holding = false;
		show_more_holding = false;
		fold_replies_holding = false;
		show_more_replies_holding = false;
		for (auto reply_view : replies) reply_view->on_scroll();
		if (additional_video_view) additional_video_view->on_scroll();
	}
	float get_height() const override {
		float main_height = std::max(left_height(), right_height());
		if (additional_image_url != "") main_height += SMALL_MARGIN * 2 + COMMUNITY_IMAGE_SIZE;
		if (additional_video_view) main_height += additional_video_view->get_height() + SMALL_MARGIN * 2;
		main_height += 16 + SMALL_MARGIN * 2;
		float reply_height = 0;
		if (replies_shown) reply_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL + SMALL_MARGIN; // fold replies
		for (size_t i = 0; i < replies_shown; i++) reply_height += replies[i]->get_height();
		if (get_has_more_replies() || replies_shown < replies.size()) reply_height += SMALL_MARGIN + DEFAULT_FONT_INTERVAL; // load more replies
		
		return main_height + reply_height + SMALL_MARGIN; // add margin between comments
	}
	float get_self_height() { return std::max(left_height(), right_height()); }
	
	std::vector<std::pair<float, PostView *> > get_reply_pos_list() {
		std::vector<std::pair<float, PostView *> > res;
		float cur_y = std::max(left_height(), right_height());
		if (replies_shown) cur_y += SMALL_MARGIN + DEFAULT_FONT_INTERVAL + SMALL_MARGIN; // fold replies
		for (size_t i = 0; i < replies_shown; i++) {
			res.push_back({cur_y, replies[i]});
			cur_y += replies[i]->get_height();
		}
		return res;
	}
	
	PostView *set_author_name(const std::string &name) { // mandatory
		this->author_name = name;
		return this;
	}
	PostView *set_time_str(const std::string &time_str) {
		this->time_str = time_str;
		return this;
	}
	PostView *set_upvote_str(const std::string &upvote_str) {
		this->upvote_str = upvote_str;
		return this;
	}
	PostView *set_author_icon_url(const std::string &icon_url) { // mandatory
		this->author_icon_url = icon_url;
		return this;
	}
	PostView *set_additional_image_url(const std::string &additional_image_url) {
		this->additional_image_url = additional_image_url;
		return this;
	}
	PostView *set_content_lines(const std::vector<std::string> &content_lines) { // mandatory
		this->content_lines = content_lines;
		this->lines_shown = std::min<size_t>(3, content_lines.size());
		return this;
	}
	PostView *set_has_more_replies(const std::function<bool ()> &get_has_more_replies) { // mandatory
		this->get_has_more_replies = get_has_more_replies;
		return this;
	}
	PostView *set_on_author_icon_pressed(CallBackFuncType on_author_icon_pressed_func) {
		this->on_author_icon_pressed_func = on_author_icon_pressed_func;
		return this;
	}
	PostView *set_on_load_more_replies_pressed(CallBackFuncTypeModifiable on_load_more_replies_pressed_func) {
		this->on_load_more_replies_pressed_func = on_load_more_replies_pressed_func;
		return this;
	}
	PostView *set_is_reply(bool is_reply) {
		this->is_reply = is_reply;
		return this;
	}
	
	void draw_() const override;
	void update_(Hid_info key) override;
};
