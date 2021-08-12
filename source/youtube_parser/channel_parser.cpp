#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

static Json get_initial_data(const std::string &html) {
	Json res;
	if (fast_extract_initial(html, "ytInitialData", res)) return res;
	res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialData regexes"}}};
	return res;
}

YouTubeChannelDetail youtube_parse_channel_page(std::string url) {
	YouTubeChannelDetail res;
	
	url = convert_url_to_mobile(url);
	
	// append "/videos" at the end of the url
	{
		bool ok = false;
		for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/"}) {
			if (url.substr(0, pattern.size()) == pattern) {
				url = url.substr(pattern.size(), url.size());
				auto next_slash = std::find(url.begin(), url.end(), '/');
				url = pattern + std::string(url.begin(), next_slash) + "/videos";
				ok = true;
				break;
			}
		}
		if (!ok) {
			res.error = "invalid URL : " + url;
			return res;
		}
	}
	
	std::string html = http_get(url);
	if (!html.size()) {
		res.error = "failed to download video page";
		return res;
	}
	
	std::string channel_name = "stub channel name";
	
	auto initial_data = get_initial_data(html);
	
	auto metadata_renderer = initial_data["metadata"]["channelMetadataRenderer"];
	res.name = metadata_renderer["title"].string_value();
	res.url = "https://m.youtube.com/channel/" + metadata_renderer["externalId"].string_value();
	res.description = metadata_renderer["description"].string_value();
	
	for (auto tab : initial_data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		if (tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"] != Json()) {
			for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) if (i["itemSectionRenderer"] != Json()) {
				for (auto content : i["itemSectionRenderer"]["contents"].array_items()) {
					if (content["compactVideoRenderer"] != Json()) {
						auto video_renderer = content["compactVideoRenderer"];
						YouTubeVideoSuccinct cur_video;
						std::string video_id = video_renderer["videoId"].string_value();
						cur_video.url = "https://m.youtube.com/watch?v=" + video_id;
						cur_video.title = get_text_from_object(video_renderer["title"]);
						cur_video.duration_text = get_text_from_object(video_renderer["lengthText"]);
						cur_video.publish_date = get_text_from_object(video_renderer["publishedTimeText"]);
						cur_video.views_str = get_text_from_object(video_renderer["shortViewCountText"]);
						cur_video.author = channel_name;
						cur_video.thumbnail_url = "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
						res.videos.push_back(cur_video);
					} else if (content["continuationItemRenderer"] != Json()) {
						res.continue_token = content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
					} else debug("unknown item found in channel videos");
				}
			}
		}
	}
	{ // top banner
		for (auto banner : initial_data["header"]["c4TabbedHeaderRenderer"]["banner"]["thumbnails"].array_items()) {
			int cur_width = banner["width"].int_value();
			if (cur_width == 1060) {
				res.banner_url = banner["url"].string_value();
			}
			/*
			if (cur_width > 1024) continue;
			if (max_width < cur_width) {
				max_width = cur_width;
				res.banner_url = banner["url"].string_value();
			}*/
		}
		if (res.banner_url.substr(0, 2) == "//") res.banner_url = "https:" + res.banner_url;
	}
	{ // icon
		int max_width = -1;
		for (auto icon : initial_data["header"]["c4TabbedHeaderRenderer"]["avatar"]["thumbnails"].array_items()) {
			int cur_width = icon["width"].int_value();
			if (cur_width > 1024) continue;
			if (max_width < cur_width) {
				max_width = cur_width;
				res.icon_url = icon["url"].string_value();
			}
		}
		if (res.icon_url.substr(0, 2) == "//") res.icon_url = "https:" + res.icon_url;
	}
	// debug(res.banner_url);
	// debug(res.icon_url);
	// debug(res.description);
	
	{
		const std::string prefix = "\"INNERTUBE_API_KEY\":\"";
		auto pos = html.find(prefix);
		if (pos != std::string::npos) {
			pos += prefix.size();
			while (pos < html.size() && html[pos] != '"') res.continue_key.push_back(html[pos++]);
		}
		if (res.continue_key == "") {
			debug("INNERTUBE_API_KEY not found");
			res.error = "INNERTUBE_API_KEY not found";
		}
	}
	
	return res;
}

YouTubeChannelDetail youtube_channel_page_continue(const YouTubeChannelDetail &prev_result) {
	YouTubeChannelDetail new_result = prev_result;
	
	if (prev_result.continue_key == "") {
		new_result.error = "continue key empty";
		return new_result;
	}
	if (prev_result.continue_token == "") {
		new_result.error = "continue token empty";
		return new_result;
	}
	
	Json yt_result;
	{
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
			+ prev_result.continue_token + "\"}";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		
		std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + prev_result.continue_key;
		
		std::string received_str = http_post_json(post_url, post_content);
		if (received_str != "") {
			std::string json_err;
			yt_result = Json::parse(received_str, json_err);
			if (json_err != "") {
				debug("[post] json parsing failed : " + json_err);
				new_result.error = "[post] json parsing failed";
				return new_result;
			}
		}
	}
	if (yt_result == Json()) {
		debug("[continue] failed (json empty)");
		new_result.error = "received json empty";
		return new_result;
	}
	new_result.continue_token = "";
	
	for (auto i : yt_result["onResponseReceivedActions"].array_items()) if (i["appendContinuationItemsAction"] != Json()) {
		for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
			if (j["compactVideoRenderer"] != Json()) {
				auto video_renderer = j["compactVideoRenderer"];
				YouTubeVideoSuccinct cur_video;
				std::string video_id = video_renderer["videoId"].string_value();
				cur_video.url = "https://m.youtube.com/watch?v=" + video_id;
				cur_video.title = get_text_from_object(video_renderer["title"]);
				cur_video.duration_text = get_text_from_object(video_renderer["lengthText"]);
				cur_video.publish_date = get_text_from_object(video_renderer["publishedTimeText"]);
				cur_video.views_str = get_text_from_object(video_renderer["shortViewCountText"]);
				cur_video.author = new_result.name;
				cur_video.thumbnail_url = "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
				new_result.videos.push_back(cur_video);
			} else if (j["continuationItemRenderer"] != Json()) {
				new_result.continue_token = j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			}
		}
	}
	if (new_result.continue_token == "") debug("failed to get next continue token");
	return new_result;
}
