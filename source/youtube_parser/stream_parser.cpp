#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"
#include "cipher.hpp"

static Json get_initial_data(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialData regexes"}}};
	return res;
}
static Json initial_player_response(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"window\\[['\\\"]ytInitialPlayerResponse['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialPlayerResponse\\s*=\\s*['\\{]"
	});
	if (res == Json()) return Json::object{{{"Error", "did not match any of the ytInitialPlayerResponse regexes"}}};
	return res;
}
static Json get_ytplayer_config(const std::string &html) {
	auto res = get_succeeding_json_regexes(html, {
		"ytplayer\\.config\\s*=\\s*['\\{]",
		"ytInitialPlayerResponse\\s*=\\s*['\\{]"
	});
	if (res != Json()) return res;
	debug("setConfig handling");
	res = get_succeeding_json_regexes(html, {"yt\\.setConfig\\(.*['\\\"]PLAYER_CONFIG['\\\"]:\\s*['\\{]"});
	if (res != Json()) return res;
	return Json::object{{{"Error", "failed to get ytplayer config"}}};
}

static std::map<std::string, yt_cipher_transform_procedure> transform_proc_cache;
static bool extract_stream(YouTubeVideoDetail &res, const std::string &html) {
	Json player_response = initial_player_response(html);
	Json player_config = get_ytplayer_config(html);
	
	// extract stream formats
	std::vector<Json> formats;
	for (auto i : player_response["streamingData"]["formats"].array_items()) formats.push_back(i);
	for (auto i : player_response["streamingData"]["adaptiveFormats"].array_items()) formats.push_back(i);
	
	// for obfuscated signatures
	std::string js_url;
	for (auto &i : formats) { // handle decipher
		if (i["url"] != Json()) continue;
		// annoying
		// get the url of base js
		if (!js_url.size()) {
			// get base js url
			if (player_config["assets"]["js"] != Json()) js_url = player_config["assets"]["js"] != Json();
			else {
				std::regex pattern = std::regex(std::string("(/s/player/[\\w]+/[\\w-\\.]+/base\\.js)"));
				std::smatch match_res;
				if (std::regex_search(html, match_res, pattern)) js_url = match_res[1].str();
				else {
					debug("could not find base js url");
					return false;
				}
			}
			js_url = "https://www.youtube.com" + js_url;
		}
		// get transform procedure
		yt_cipher_transform_procedure transform_procedure;
		if (!transform_proc_cache.count(js_url)) {
			std::string js_content = http_get(js_url);
			if (!js_content.size()) {
				debug("base js download failed");
				return false;
			}
			transform_proc_cache[js_url] = transform_procedure = yt_get_transform_plan(js_content);
			if (!transform_procedure.size()) return false; // failed to get transform plan
			// for (auto i : transform_procedure) debug(std::to_string(i.first) + " " + std::to_string(i.second));
		} else transform_procedure = transform_proc_cache[js_url];
		
		auto cipher_params = parse_parameters(i["cipher"] != Json() ? i["cipher"].string_value() : i["signatureCipher"].string_value());
		auto tmp = i.object_items();
		tmp["url"] = Json(cipher_params["url"] + "&" + cipher_params["sp"] + "=" + yt_deobfuscate_signature(cipher_params["s"], transform_procedure));
		i = Json(tmp);
	}
	
	std::vector<Json> audio_formats, video_formats;
	for (auto i : formats) {
		auto mime_type = i["mimeType"].string_value();
		if (mime_type.substr(0, 5) == "video") {
			// H.264 is virtually the only playable video codec
			if (mime_type.find("avc1") != std::string::npos) video_formats.push_back(i);
		} else if (mime_type.substr(0, 5) == "audio") {
			// We can modify ffmpeg to support opus
			if (mime_type.find("mp4a") != std::string::npos) audio_formats.push_back(i);
		} else {} // ???
	}
	// audio
	{
		int max_bitrate = -1;
		for (auto i : audio_formats) {
			int cur_bitrate = i["bitrate"].int_value();
			if (max_bitrate < cur_bitrate) {
				max_bitrate = cur_bitrate;
				res.audio_stream_url = i["url"].string_value();
				res.audio_stream_len = stoll(i["contentLength"].string_value());
			}
		}
	}
	// video
	{
		std::vector<int> recommended_itags = {134, 134, 160, 133};
		u8 found = recommended_itags.size();
		for (auto i : video_formats) {
			int cur_itag = i["itag"].int_value();
			for (size_t j = 0; j < recommended_itags.size(); j++) if (cur_itag == recommended_itags[j] && found > j) {
				found = j;
				res.video_stream_url = i["url"].string_value();
				res.video_stream_len = stoll(i["contentLength"].string_value());
			}
		}
		if (found == recommended_itags.size() && video_formats.size()) { // recommended resolution not found, pick random one
			res.video_stream_url = video_formats[0]["url"].string_value();
			res.video_stream_len = stoll(video_formats[0]["contentLength"].string_value());
		}
		// search for itag 18
		for (auto i : video_formats) if (i["itag"].int_value() == 18) {
			res.both_stream_url = i["url"].string_value();
			res.both_stream_len = stoll(i["contentLength"].string_value());
		}
	}
	return true;
}

static void extract_owner(Json slimOwnerRenderer, YouTubeVideoDetail &res) {
	res.author.name = slimOwnerRenderer["channelName"].string_value();
	res.author.url = slimOwnerRenderer["channelUrl"].string_value();
	
	std::string icon_48;
	int max_width = -1;
	std::string icon_largest;
	for (auto icon : slimOwnerRenderer["thumbnail"]["thumbnails"].array_items()) {
		if (icon["height"].int_value() >= 256) continue; // too large
		if (icon["height"].int_value() == 48) {
			icon_48 = icon["url"].string_value();
		}
		if (max_width < icon["height"].int_value()) {
			max_width = icon["height"].int_value();
			icon_largest = icon["url"].string_value();
		}
	}
	res.author.icon_url = icon_48 != "" ? icon_48 : icon_largest;
	if (res.author.icon_url.substr(0, 2) == "//") res.author.icon_url = "https:" + res.author.icon_url;
}

static void extract_item(Json content, YouTubeVideoDetail &res) {
	auto get_video_from_renderer = [&] (Json video_renderer) {
		YouTubeVideoSuccinct cur_video;
		std::string video_id = video_renderer["videoId"].string_value();
		cur_video.url = "https://m.youtube.com/watch?v=" + video_id;
		cur_video.title = get_text_from_object(video_renderer["headline"]);
		cur_video.duration_text = get_text_from_object(video_renderer["lengthText"]);
		cur_video.author = get_text_from_object(video_renderer["shortBylineText"]);
		cur_video.thumbnail_url = "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
		return cur_video;
	};
	if (content["slimVideoMetadataRenderer"] != Json()) {
		Json metadata_renderer = content["slimVideoMetadataRenderer"];
		res.title = get_text_from_object(metadata_renderer["title"]);
		res.description = get_text_from_object(metadata_renderer["description"]);
		extract_owner(metadata_renderer["owner"]["slimOwnerRenderer"], res);
	} else if (content["compactAutoplayRenderer"] != Json()) {
		for (auto j : content["compactAutoplayRenderer"]["contents"].array_items()) if (j["videoWithContextRenderer"] != Json())
			res.suggestions.push_back(get_video_from_renderer(j["videoWithContextRenderer"]));
	} else if (content["videoWithContextRenderer"] != Json())
		res.suggestions.push_back(get_video_from_renderer(content["videoWithContextRenderer"]));
	else if (content["continuationItemRenderer"] != Json())
		res.suggestions_continue_token = content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
}

static void extract_metadata(YouTubeVideoDetail &res, const std::string &html) {
	Json initial_data = get_initial_data(html);
	
	{
		auto contents = initial_data["contents"]["singleColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content["itemSectionRenderer"] != Json()) {
				for (auto i : content["itemSectionRenderer"]["contents"].array_items()) extract_item(i, res);
			} else if (content["slimVideoMetadataSectionRenderer"] != Json()) {
				for (auto i : content["slimVideoMetadataSectionRenderer"]["contents"].array_items()) {
					if (i["slimVideoInformationRenderer"] != Json()) res.title = get_text_from_object(i["slimVideoInformationRenderer"]["title"]);
					if (i["slimOwnerRenderer"] != Json()) extract_owner(i["slimOwnerRenderer"], res);
				}
			}
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
		}
	}
	res.comment_continue_type = -1;
	if (initial_data["engagementPanels"] == Json()) {
		res.comments_disabled = true;
	} else {
		res.comments_disabled = false;
		for (auto i : initial_data["engagementPanels"].array_items()) if (i["engagementPanelSectionListRenderer"] != Json()) {
			for (auto j : i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["continuations"].array_items()) if (j["reloadContinuationData"] != Json()) {
				res.comment_continue_token = j["reloadContinuationData"]["continuation"].string_value();
				res.comment_continue_type = 0;
			}
			for (auto j : i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) if (j["itemSectionRenderer"] != Json()) {
				for (auto k : j["itemSectionRenderer"]["contents"].array_items()) if (k["continuationItemRenderer"] != Json()) {
					res.comment_continue_token = k["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
					res.comment_continue_type = 1;
				}
			}
		}
	}
}

YouTubeVideoDetail youtube_parse_video_page(std::string url) {
	YouTubeVideoDetail res;
	
	url = convert_url_to_mobile(url);
	
	std::string html = http_get(url);
	if (!html.size()) {
		res.error = "failed to download video page";
		return res;
	}
	
	extract_stream(res, html);
	extract_metadata(res, html);
	
	debug(res.title);
	// debug(res.author.name);
	// debug(res.description);
	return res;
}

YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result) {
	YouTubeVideoDetail new_result = prev_result;
	
	if (prev_result.continue_key == "") {
		new_result.error = "continue key empty";
		return new_result;
	}
	if (prev_result.suggestions_continue_token == "") {
		new_result.error = "suggestion continue token empty";
		return new_result;
	}
	
	// POST to get more results
	Json yt_result;
	{
		std::string post_content = R"({"context": {"client": {"hl": "ja", "gl": "JP", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
			+ prev_result.suggestions_continue_token + "\"}";
		
		std::string post_url = "https://m.youtube.com/youtubei/v1/next?key=" + prev_result.continue_key;
		
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
	
	new_result.suggestions_continue_token = "";
	for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) if (i["appendContinuationItemsAction"] != Json()) {
		for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
			extract_item(j, new_result);
		}
	}
	/*
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
	if (new_result.continue_token == "") debug("failed to get next continue token");*/
	return new_result;
}
YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result) {
	YouTubeVideoDetail new_result = prev_result;
	
	if (prev_result.comment_continue_type == -1) {
		new_result.error = "No more comments available";
		return new_result;
	}
	
	auto parse_comment_thread_renderer = [&] (Json comment_thread_renderer) {
		Json comment_renderer = comment_thread_renderer["commentThreadRenderer"]["comment"]["commentRenderer"];
		YouTubeVideoDetail::Comment cur_comment;
		// get the icon of the author with minimum size
		cur_comment.id = comment_renderer["commentId"].string_value();
		cur_comment.content = get_text_from_object(comment_renderer["contentText"]);
		cur_comment.reply_num = comment_renderer["replyCount"].int_value(); // Json.int_value() defaults to zero, so... it works
		cur_comment.author.name = get_text_from_object(comment_renderer["authorText"]);
		cur_comment.author.url = "https://m.youtube.com" + comment_renderer["authorEndpoint"]["browseEndpoint"]["canonicalBaseUrl"].string_value();
		{
			int min_width = 1000000;
			for (auto icon : comment_renderer["authorThumbnail"]["thumbnails"].array_items()) {
				int cur_width = icon["width"].int_value();
				if (min_width > cur_width) {
					min_width = cur_width;
					cur_comment.author.icon_url = icon["url"].string_value();
				}
			}
		}
		return cur_comment;
	};
	
	Json comment_data;
	if (prev_result.comment_continue_type == 0) {
		{
			std::string received_str = http_get("https://m.youtube.com/watch_comment?action_get_comments=1&pbj=1&ctoken=" + prev_result.comment_continue_token, 
				{{"X-YouTube-Client-Version", "2.20210714.00.00"}, {"X-YouTube-Client-Name", "2"}});
			if (received_str != "") {
				std::string json_err;
				comment_data = Json::parse(received_str, json_err);
				if (json_err != "") debug("[comment] json parsing failed : " + json_err);
			}
		}
		if (comment_data == Json()) {
			new_result.error = "Failed to load comments";
			return new_result;
		}
		new_result.comment_continue_type = -1;
		new_result.comment_continue_token = "";
		for (auto i : comment_data.array_items()) if (i["response"] != Json()) {
			for (auto comment : i["response"]["continuationContents"]["commentSectionContinuation"]["items"].array_items()) if (comment["commentThreadRenderer"] != Json())
				new_result.comments.push_back(parse_comment_thread_renderer(comment));
			for (auto j : i["response"]["continuationContents"]["commentSectionContinuation"]["continuations"].array_items()) if (j["nextContinuationData"] != Json()) {
				new_result.comment_continue_token = j["nextContinuationData"]["continuation"].string_value();
				new_result.comment_continue_type = 0;
			}
		}
	} else {
		{
			std::string post_content = R"({"context": {"client": {"hl": "ja", "gl": "JP", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
				+ prev_result.comment_continue_token + "\"}";
			std::string received_str = http_post_json("https://m.youtube.com/youtubei/v1/next?key=" + prev_result.continue_key, post_content);
			if (received_str != "") {
				std::string json_err;
				comment_data = Json::parse(received_str, json_err);
				if (json_err != "") debug("[comment] json parsing failed : " + json_err);
			}
		}
		if (comment_data == Json()) {
			new_result.error = "Failed to load comments";
			return new_result;
		}
		new_result.comment_continue_type = -1;
		new_result.comment_continue_token = "";
		for (auto i : comment_data["onResponseReceivedEndpoints"].array_items()) {
			Json continuation_items;
			if (i["reloadContinuationItemsCommand"] != Json()) continuation_items = i["reloadContinuationItemsCommand"]["continuationItems"];
			else if (i["appendContinuationItemsAction"] != Json()) continuation_items = i["appendContinuationItemsAction"]["continuationItems"];
			
			for (auto comment : continuation_items.array_items()) {
				if (comment["commentThreadRenderer"] != Json()) new_result.comments.push_back(parse_comment_thread_renderer(comment));
				if (comment["continuationItemRenderer"] != Json()) {
					new_result.comment_continue_token = comment["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
					new_result.comment_continue_type = 1;
				}
			}
		}
	}
	return new_result;
}

#ifdef _WIN32
int main() {
	std::string url;
	std::cin >> url;
	
	auto result = youtube_parse_video_page(url);
	std::cerr << result.comments.size() << " " << result.has_more_comments() << std::endl;
	result = youtube_video_page_load_more_comments(result);
	std::cerr << result.comments.size() << " " << result.has_more_comments() << std::endl;
	/*
	result = youtube_video_page_load_more_comments(result);
	for (auto i : result.comments) {
		std::cout << i.content << std::endl;
	}
	std::cout << result.error << std::endl;*/
	
	/*
	for (int i = 0; i < 50; i++) {
		std::cout << i << std::endl;
		auto result = youtube_parse_video_page(url);
		for (int j = 0; j < 5; j++) {
			std::cerr << "  " << j << std::endl;
			auto new_result = youtube_video_page_load_more_comments(result);
			for (int k = result.comments.size(); k < (int) new_result.comments.size(); k++) {
				std::cout << "    " << new_result.comments[k].author.name << std::endl;
			}
			if (new_result.comments.size() <= result.comments.size() || !result.has_more_comments()) {
				std::cout << "FAILED" << std::endl;
				return 0;
			}
			result = new_result;
		}
	}*/
	
	return 0;
}
#endif

