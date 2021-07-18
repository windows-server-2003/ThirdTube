#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

static Json get_initial_data(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialData regexes"}}};
	return res;
}

static bool parse_searched_item(Json content, std::vector<YouTubeSearchResult::Item> &res) {
	if (content["compactVideoRenderer"] != Json()) {
		auto video_renderer = content["compactVideoRenderer"];
		YouTubeVideoSuccinct cur_result;
		std::string video_id = video_renderer["videoId"].string_value();
		cur_result.url = "https://m.youtube.com/watch?v=" + video_id;
		cur_result.title = get_text_from_object(video_renderer["title"]);
		cur_result.duration_text = get_text_from_object(video_renderer["lengthText"]);
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
		res.push_back(YouTubeSearchResult::Item(cur_result));
		return true;
	} else if (content["compactChannelRenderer"] != Json()) {
		auto channel_renderer = content["compactChannelRenderer"];
		YouTubeChannelSuccinct cur_result;
		cur_result.name = get_text_from_object(channel_renderer["displayName"]);
		cur_result.url = "https://m.youtube.com/channel/" + channel_renderer["channelId"].string_value();
		
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
		}
		
		res.push_back(YouTubeSearchResult::Item(cur_result));
		return true;
	}
	return false;
}

YouTubeSearchResult youtube_parse_search(std::string url) {
	YouTubeSearchResult res;
	
	url = convert_url_to_mobile(url);
	
	std::string html = http_get(url);
	if (!html.size()) {
		res.error = "failed to download video page";
		return res;
	}
	
	auto initial_data = get_initial_data(html);
	res.estimated_result_num = std::stoll(initial_data["estimatedResults"].string_value());
	
	for (auto i : initial_data["contents"]["sectionListRenderer"]["contents"].array_items()) {
		if (i["itemSectionRenderer"] != Json()) {
			for (auto j : i["itemSectionRenderer"]["contents"].array_items()) {
				parse_searched_item(j, res.results);
			}
		}
		if (i["continuationItemRenderer"] != Json()) {
			res.continue_token = i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
		}
	}
	{
		std::regex pattern(std::string(R"***("INNERTUBE_API_KEY":"([\w-]+)")***"));
		std::smatch match_result;
		if (std::regex_search(html, match_result, pattern)) {
			res.continue_key = match_result[1].str();
		} else {
			debug("INNERTUBE_API_KEY not found");
			res.error = "INNERTUBE_API_KEY not found";
			return res;
		}
	}
	
	/*
	for (auto i : res.results) {
		debug(i.title + " " + i.author + " " + i.duration_text + " " + i.url + " " + i.thumbnail_url);
	}
	debug(res.estimated_result_num);*/
	return res;
}
YouTubeSearchResult youtube_continue_search(const YouTubeSearchResult &prev_result) {
	YouTubeSearchResult new_result = prev_result;
	
	if (prev_result.continue_key == "") {
		new_result.error = "continue key empty";
		return new_result;
	}
	if (prev_result.continue_token == "") {
		new_result.error = "continue token empty";
		return new_result;
	}
	
	// POST to get more results
	Json yt_result;
	{
		std::string post_content = R"({"context": {"client": {"hl": "ja", "gl": "JP", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
			+ prev_result.continue_token + "\"}";
		
		std::string post_url = "https://m.youtube.com/youtubei/v1/search?key=" + prev_result.continue_key;
		
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
	
	new_result.estimated_result_num = stoll(yt_result["estimatedResults"].string_value());
	new_result.continue_token = "";
	for (auto i : yt_result["onResponseReceivedCommands"].array_items()) if (i["appendContinuationItemsAction"] != Json()) {
		for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
			if (j["itemSectionRenderer"] != Json()) {
				for (auto item : j["itemSectionRenderer"]["contents"].array_items()) {
					parse_searched_item(item, new_result.results);
				}
			} else if (j["continuationItemRenderer"] != Json()) {
				new_result.continue_token = j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			}
		}
	}
	if (new_result.continue_token == "") debug("failed to get next continue token");
	return new_result;
}

#ifdef _WIN32
int main() {
	std::string url;
	std::cin >> url;
	auto result = youtube_parse_search(url);
	
	auto out = [&] (YouTubeSearchResult::Item item) {
		if (item.type == YouTubeSearchResult::Item::VIDEO) {
			std::cerr << "video " << item.video.title << " " << item.video.url << std::endl;
		} else {
			std::cerr << "channel " << item.channel.name << std::endl;
		}
	};
	
	for (auto i : result.results) out(i);
	
	/*
	for (int i = 0; i < 3; i++) {
		std::cerr << std::endl;
		std::cerr << i << std::endl;
		auto new_result = youtube_continue_search(result);
		
		for (int j = result.results.size(); j < (int) new_result.results.size(); j++) {
			out(new_result.results[j]);
		}
		result = new_result;
	}*/
	return 0;
}
#endif

