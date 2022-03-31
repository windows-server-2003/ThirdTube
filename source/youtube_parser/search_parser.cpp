#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

static bool parse_searched_item(RJson content, std::vector<YouTubeSuccinctItem> &res) {
	if (content.has_key("compactVideoRenderer")) {
		auto video_renderer = content["compactVideoRenderer"];
		YouTubeVideoSuccinct cur_result;
		std::string video_id = video_renderer["videoId"].string_value();
		cur_result.url = "https://m.youtube.com/watch?v=" + video_id;
		cur_result.title = get_text_from_object(video_renderer["title"]);
		cur_result.duration_text = get_text_from_object(video_renderer["lengthText"]);
		cur_result.publish_date = get_text_from_object(video_renderer["publishedTimeText"]);
		cur_result.views_str = get_text_from_object(video_renderer["shortViewCountText"]);
		cur_result.author = get_text_from_object(video_renderer["shortBylineText"]);
		cur_result.thumbnail_url = "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
		/*
		{ // extract thumbnail url
			int max_width = -1;
			for (auto thumbnail : video_renderer["thumbnail"]["thumbnails"].array_items()) {
				if (thumbnail["url"].string_value().find("webp") != std::string::npos) continue; // we want jpeg thumbnail
				int cur_width = thumbnail["width"].int_value();
				if (cur_width > 256) continue; // too large
				if (max_width < cur_width) {
					max_width = cur_width;
					cur_result.thumbnail_url = thumbnail["url"].string_value();
				}
			}
		}*/
		res.push_back(YouTubeSuccinctItem(cur_result));
		return true;
	} else if (content.has_key("compactChannelRenderer")) {
		auto channel_renderer = content["compactChannelRenderer"];
		YouTubeChannelSuccinct cur_result;
		cur_result.id = channel_renderer["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
		// !!! cur_result.url = "https://m.youtube.com/channel/" + channel_renderer["channelId"].string_value();
		cur_result.name = get_text_from_object(channel_renderer["displayName"]);
		cur_result.subscribers = get_text_from_object(channel_renderer["subscriberCountText"]);
		cur_result.video_num = get_text_from_object(channel_renderer["videoCountText"]);
		
		{
			constexpr int target_height = 70;
			int min_distance = 100000;
			std::string best_icon;
			for (auto icon : channel_renderer["thumbnail"]["thumbnails"].array_items()) {
				int cur_height = icon["height"].int_value();
				if (cur_height >= 256) continue; // too large
				if (min_distance > std::abs(target_height - cur_height)) {
					min_distance = std::abs(target_height - cur_height);
					best_icon = icon["url"].string_value();
				}
			}
			cur_result.icon_url = best_icon;
			if (cur_result.icon_url.substr(0, 2) == "//") cur_result.icon_url = "https:" + cur_result.icon_url;
			debug(cur_result.icon_url);
		}
		
		res.push_back(YouTubeSuccinctItem(cur_result));
		return true;
	} else if (content.has_key("compactRadioRenderer") || content.has_key("compactPlaylistRenderer")) {
		auto playlist_renderer = content.has_key("compactRadioRenderer") ? content["compactRadioRenderer"] : content["compactPlaylistRenderer"];
		
		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items())
			if (thumbnail["url"].string_value().find("/default.jpg") != std::string::npos)
				cur_list.thumbnail_url = thumbnail["url"].string_value();
		
		cur_list.url = convert_url_to_mobile(playlist_renderer["shareUrl"].string_value());
		if (!starts_with(cur_list.url, "https://m.youtube.com/watch", 0)) {
			if (starts_with(cur_list.url, "https://m.youtube.com/playlist?", 0)) {
				auto params = parse_parameters(cur_list.url.substr(std::string("https://m.youtube.com/playlist?").size(), cur_list.url.size()));
				auto playlist_id = params["list"];
				auto video_id = get_video_id_from_thumbnail_url(cur_list.thumbnail_url);
				cur_list.url = "https://m.youtube.com/watch?v=" + video_id + "&list=" + playlist_id;
			} else {
				debug("unknown playlist url");
				return false;
			}
		}
		
		res.push_back(YouTubeSuccinctItem(cur_list));
	}
	return false;
}

YouTubeSearchResult youtube_parse_search(std::string url) {
	YouTubeSearchResult res;
	
	std::string query_word;
	auto pos = url.find("?search_query=");
	if (pos == std::string::npos) pos = url.find("&search_query=");
	if (pos != std::string::npos) {
		size_t head = pos + std::string("?search_query=").size();
		while (head < url.size() && url[head] != '&') query_word.push_back(url[head++]);
	}
	{ // decode back %?? url encoding because the API parameter doesn't seem to need it
		std::string new_query_word;
		for (size_t i = 0; i < query_word.size(); ) {
			auto hex_to_int = [] (char c) {
				if (isdigit(c)) return c - '0';
				if (isupper(c)) return c - 'A' + 10;
				if (islower(c)) return c - 'a' + 10;
				return 0;
			};
			if (query_word[i] == '%' && i + 2 < query_word.size()) {
				new_query_word.push_back(hex_to_int(query_word[i + 1]) * 16 + hex_to_int(query_word[i + 2]));
				i += 3;
			} else new_query_word.push_back(query_word[i++]);
		}
		query_word = new_query_word;
	}
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00"}}, "query": ")"
		+ query_word + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("search"), post_content); },
		[&] (Document &, RJson yt_result) {
			res.estimated_result_num = std::stoll(yt_result["estimatedResults"].string_value());
			res.continue_token = "";
			for (auto i : yt_result["contents"]["sectionListRenderer"]["contents"].array_items()) {
				if (i.has_key("itemSectionRenderer"))
					for (auto j : i["itemSectionRenderer"]["contents"].array_items()) parse_searched_item(j, res.results);
				if (i.has_key("continuationItemRenderer"))
					res.continue_token = i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			}
			if (res.continue_token == "") debug("failed to get next continue token");
		},
		[&] (const std::string &error) {
			res.error = "[se] " + error;
			debug(res.error);
		}
	);
	
	return res;
}
YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result) {
	YouTubeSearchResult new_result = prev_result;
	
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") {
		new_result.error = "innertube key empty";
		return new_result;
	}
	if (prev_result.continue_token == "") {
		new_result.error = "continue token empty";
		return new_result;
	}
	
	// POST to get more results
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
		+ prev_result.continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("search"), post_content); },
		[&] (Document &, RJson yt_result) {
			new_result.estimated_result_num = std::stoll(yt_result["estimatedResults"].string_value());
			new_result.continue_token = "";
			for (auto i : yt_result["onResponseReceivedCommands"].array_items()) {
				for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (j.has_key("itemSectionRenderer")) {
						for (auto item : j["itemSectionRenderer"]["contents"].array_items()) parse_searched_item(item, new_result.results);
					} else if (j.has_key("continuationItemRenderer"))
						new_result.continue_token = j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				}
			}
			if (new_result.continue_token == "") debug("failed to get next continue token");
		},
		[&] (const std::string &error) {
			new_result.error = "[se+] " + error;
			debug(new_result.error);
		}
	);
	
	return new_result;
}
