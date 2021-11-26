#pragma once
#include <vector>
#include <string>
#include <map>

struct YouTubeChannelSuccinct {
	std::string name;
	std::string url;
	std::string icon_url;
	std::string subscribers;
	std::string video_num;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string publish_date;
	std::string views_str;
	std::string author;
	std::string thumbnail_url;
};
struct YouTubePlaylistSuccinct {
	std::string url;
	std::string title;
	std::string video_count_str;
	std::string thumbnail_url;
};

struct YouTubeSuccinctItem {
	// TODO : use union or std::variant
	enum {
		VIDEO,
		CHANNEL,
		PLAYLIST
	} type;
	YouTubeVideoSuccinct video;
	YouTubeChannelSuccinct channel;
	YouTubePlaylistSuccinct playlist;
	
	YouTubeSuccinctItem () = default;
	YouTubeSuccinctItem (YouTubeVideoSuccinct video) : type(VIDEO), video(video) {}
	YouTubeSuccinctItem (YouTubeChannelSuccinct channel) : type(CHANNEL), channel(channel) {}
	YouTubeSuccinctItem (YouTubePlaylistSuccinct playlist) : type(PLAYLIST), playlist(playlist) {}
	
	std::string get_url() const { return type == VIDEO ? video.url : type == CHANNEL ? channel.url : playlist.url; }
	std::string get_thumbnail_url() const { return type == VIDEO ? video.thumbnail_url : type == CHANNEL ? channel.icon_url : playlist.thumbnail_url; }
	std::string get_name() const { return type == VIDEO ? video.title : type == CHANNEL ? channel.name : playlist.title; }
};


struct YouTubeSearchResult {
	std::string error;
	int estimated_result_num;
	std::vector<YouTubeSuccinctItem> results;
	
	std::string continue_token;
	std::string continue_key;
	
	bool has_continue() const { return continue_token != "" && continue_key != ""; }
};
YouTubeSearchResult youtube_parse_search(std::string url);
// takes the previous result, returns the new result with both old items and new items
YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result);


struct YouTubeVideoDetail {
	std::string error;
	std::string url;
	std::string title;
	std::string description;
	YouTubeChannelSuccinct author;
	std::string id;
	std::string succinct_thumbnail_url;
	std::string audio_stream_url;
	std::map<int, std::string> video_stream_urls; // first : video size (144p, 240p, 360p ...)
	std::string both_stream_url;
	int duration_ms;
	bool is_livestream;
	enum class LivestreamType {
		PREMIERE,
		LIVESTREAM,
	};
	LivestreamType livestream_type;
	bool is_upcoming;
	std::string playability_status;
	std::string playability_reason;
	int stream_fragment_len; // used only for livestreams
	std::string like_count_str;
	std::string dislike_count_str;
	std::string publish_date;
	std::string views_str;
	
	// caption-related data
	std::string caption_base_url;
	struct CaptionBaseLanguage {
		std::string name; // e.g. "English", "Japanese"
		std::string id; // e.g. "en", "ja"
		std::string base_url; // empty string for instances in caption_translated_languages
		bool is_translatable;
	};
	struct CaptionTranslationLanguage {
		std::string name;
		std::string id;
	};
	std::vector<CaptionBaseLanguage> caption_base_languages;
	std::vector<CaptionTranslationLanguage> caption_translation_languages;
	struct CaptionPiece {
		float start_time;
		float end_time;
		std::string content;
	};
	using Caption = std::vector<CaptionPiece>;
	std::map<std::pair<std::string, std::string>, Caption> caption_data;
	
	std::vector<YouTubeSuccinctItem> suggestions;
	struct Playlist {
		std::string id;
		std::string title;
		std::string author_name;
		int total_videos;
		std::vector<YouTubeVideoSuccinct> videos;
		int selected_index;
	};
	Playlist playlist;
	
	struct Comment {
		YouTubeChannelSuccinct author;
		std::string content;
		std::string id;
		std::string publish_date;
		std::string upvotes_str;
		
		int reply_num;
		std::vector<Comment> replies;
		std::string continue_key; // innertube key, equals to YouTubeChannelDetail.continue_key
		std::string replies_continue_token;
		bool has_more_replies() const { return replies_continue_token != ""; }
	};
	std::vector<Comment> comments;
	
	std::string continue_key; // innertube key
	std::string suggestions_continue_token;
	std::string comment_continue_token;
	int comment_continue_type; // -1 : unavailable, 0 : using watch_comments, 1 : using innertube
	bool comments_disabled;
	
	bool has_next_video() const {
		if (playlist.videos.size() && playlist.selected_index != (int) playlist.videos.size() - 1) return true;
		for (auto suggestion : suggestions) if (suggestion.type == YouTubeSuccinctItem::VIDEO) return true;
		return false;
	}
	YouTubeVideoSuccinct get_next_video() const {
		if (playlist.videos.size() && playlist.selected_index != (int) playlist.videos.size() - 1) return playlist.videos[std::max(0, playlist.selected_index + 1)];
		for (auto suggestion : suggestions) if (suggestion.type == YouTubeSuccinctItem::VIDEO) return suggestion.video;
		else return YouTubeVideoSuccinct();
	}
	bool has_more_suggestions() const { return continue_key != "" && suggestions_continue_token != ""; }
	bool has_more_comments() const { return comment_continue_type != -1; }
	bool needs_timestamp_adjusting() const { return is_livestream && livestream_type == LivestreamType::PREMIERE; }
	bool is_playable() const { return playability_status == "OK" && (both_stream_url != "" || (audio_stream_url != "" && video_stream_urls.size())); }
};
// this function does not load comments; call youtube_video_page_load_more_comments() if necessary
YouTubeVideoDetail youtube_parse_video_page(std::string url);
YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result);
YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result);
YouTubeVideoDetail::Comment youtube_video_page_load_more_replies(const YouTubeVideoDetail::Comment &comment);
YouTubeVideoDetail youtube_video_page_load_caption(const YouTubeVideoDetail &prev_result, const std::string &base_lang_id, const std::string &translation_lang_id);



struct YouTubeChannelDetail {
	std::string id;
	std::string error;
	std::string name;
	std::string url;
	std::string url_original;
	std::string icon_url;
	std::string banner_url;
	std::string description;
	std::string subscriber_count_str;
	std::vector<YouTubeVideoSuccinct> videos;
	
	std::string innertube_key;
	std::string continue_token;
	std::string playlist_tab_browse_id;
	std::string playlist_tab_params;
	
	std::vector<std::pair<std::string, std::vector<YouTubePlaylistSuccinct> > > playlists; // {category title, list of playlists}
	
	struct CommunityPost {
		std::string author_name;
		std::string author_icon_url;
		std::string time;
		std::string upvotes_str;
		std::string message;
		std::string image_url; // "" if no image
		std::string poll_total_votes;
		std::vector<std::string> poll_choices;
		YouTubeVideoSuccinct video;
	};
	std::vector<CommunityPost> community_posts;
	bool community_loaded = false;
	std::string community_continuation_token;
	
	bool has_continue() const { return innertube_key != "" && continue_token != ""; }
	bool has_playlist_to_load() const { return innertube_key != "" && playlist_tab_browse_id != "" && playlist_tab_params != ""; }
	bool has_community_posts_to_load() const { return !community_loaded || (innertube_key != "" && community_continuation_token != ""); }
};
YouTubeChannelDetail youtube_parse_channel_page(std::string url);
// takes the previous result, returns the new result with both old items and new items
YouTubeChannelDetail youtube_channel_page_continue(const YouTubeChannelDetail &prev_result);
YouTubeChannelDetail youtube_channel_load_playlists(const YouTubeChannelDetail &prev_result);
YouTubeChannelDetail youtube_channel_load_community(const YouTubeChannelDetail &prev_result);

void youtube_change_content_language(std::string language_code);

// util function
std::string youtube_get_video_thumbnail_url_by_id(const std::string &id);
std::string youtube_get_video_url_by_id(const std::string &id);
std::string get_video_id_from_thumbnail_url(const std::string &url);
bool youtube_is_valid_video_id(const std::string &id);
bool is_youtube_url(const std::string &url);
bool is_youtube_thumbnail_url(const std::string &url);

enum class YouTubePageType {
	VIDEO,
	CHANNEL,
	SEARCH,
	INVALID
};
YouTubePageType youtube_get_page_type(std::string url);


