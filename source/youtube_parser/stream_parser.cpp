#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"
#include "cipher.hpp"
#include "n_param.hpp"
#include "cache.hpp"

static RJson get_initial_data(Document &json_root, const std::string &html) {
	RJson res;
	if (fast_extract_initial(json_root, html, "ytInitialData", res)) return res;
	res = get_succeeding_json_regexes(json_root, html, {
		"window\\[['\\\"]ytInitialData['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialData\\s*=\\s*['\\{]"
	});
	if (!res.is_valid()) return get_error_json("did not match any of the ytInitialData regexes");
	return res;
}
static RJson initial_player_response(Document &json_root, const std::string &html) {
	RJson res;
	if (fast_extract_initial(json_root, html, "ytInitialPlayerResponse", res)) return res;
	res = get_succeeding_json_regexes(json_root, html, {
		"window\\[['\\\"]ytInitialPlayerResponse['\\\"]]\\s*=\\s*['\\{]",
		"ytInitialPlayerResponse\\s*=\\s*['\\{]"
	});
	if (!res.is_valid()) return get_error_json("did not match any of the ytInitialData regexes");
	return res;
}


static std::map<std::string, yt_cipher_transform_procedure> cipher_transform_proc_cache;
static std::map<std::string, std::string> nparam_transform_function_cache;
static std::map<std::pair<std::string, std::string>, std::string> nparam_transform_results_cache;
static bool extract_player_data(Document &json_root, RJson player_response, YouTubeVideoDetail &res, bool use_js_player) {
	res.playability_status = player_response["playabilityStatus"]["status"].string_value();
	res.playability_reason = player_response["playabilityStatus"]["reason"].string_value();
	res.is_upcoming = player_response["videoDetails"]["isUpcoming"].bool_value();
	res.livestream_type = player_response["videoDetails"]["isLiveContent"].bool_value() ?
		YouTubeVideoDetail::LivestreamType::LIVESTREAM : YouTubeVideoDetail::LivestreamType::PREMIERE;
	
	// extract stream formats
	std::vector<RJson> formats;
	for (auto i : player_response["streamingData"]["formats"].array_items()) formats.push_back(i);
	for (auto i : player_response["streamingData"]["adaptiveFormats"].array_items()) formats.push_back(i);
	
	if (use_js_player) {
		// for obfuscated signatures & n parameter modification
		if (!cipher_transform_proc_cache.count(base_js_url) || !nparam_transform_function_cache.count(base_js_url)) {
			std::string js_id;
			std::regex pattern = std::regex(std::string("(/s/player/[\\w]+/[\\w-\\.]+/base\\.js)"));
			std::smatch match_res;
			if (std::regex_search(base_js_url, match_res, std::regex("/s/player/([\\w]+)/"))) js_id = match_res[1].str();
			if (js_id == "") {
				debug("failed to extract js id");
				return false;
			}
			bool cache_used = false;
#			ifndef _WIN32
			char *buf = (char *) malloc(0x4000);
			u32 read_size;
			if (buf && Util_file_load_from_file(js_id, DEF_MAIN_DIR + "js_cache/", (u8 *) buf, 0x4000 - 1, &read_size).code == 0) {
				debug("cache found (" + js_id + ") size:" + std::to_string(read_size) + " found, using...");
				buf[read_size] = '\0';
				if (yt_procs_from_string(buf, cipher_transform_proc_cache[base_js_url], nparam_transform_function_cache[base_js_url])) cache_used = true;
				else debug("failed to load cache");
			}
			free(buf);
#			endif
			if (!cache_used) {
				std::string js_content = http_get(base_js_url);
				if (!js_content.size()) {
					debug("base js download failed");
					return false;
				}
				cipher_transform_proc_cache[base_js_url] = yt_cipher_get_transform_plan(js_content);
				nparam_transform_function_cache[base_js_url] = yt_nparam_get_function_content(js_content);
#			ifndef _WIN32
				auto cache_str = yt_procs_to_string(cipher_transform_proc_cache[base_js_url], nparam_transform_function_cache[base_js_url]);
				Result_with_string result = Util_file_save_to_file(js_id, DEF_MAIN_DIR + "js_cache/", (u8 *) cache_str.c_str(), cache_str.size(), true);
				if (result.code != 0) debug("cache write failed : " + result.error_description);
#			endif
			}
		}
		
		for (auto &i : formats) { // handle decipher
			if (i.has_key("url")) continue;
			
			auto transform_procedure = cipher_transform_proc_cache[base_js_url];
			auto cipher_params = parse_parameters(i.has_key("cipher") ? i["cipher"].string_value() : i["signatureCipher"].string_value());
			i.set_str(json_root, "url",
				(cipher_params["url"] + "&" + cipher_params["sp"] + "=" + yt_deobfuscate_signature(cipher_params["s"], transform_procedure)).c_str());
		}
		static auto nparam_regex = std::regex(std::string("[\?&]n=([^&]+)"));
		for (auto &i : formats) { // modify the `n` parameter
			std::string url = i["url"].string_value();
			std::smatch match_result;
			if (std::regex_search(url, match_result, nparam_regex)) {
				auto cur_n = match_result[1].str();
				std::string next_n;
				if (!nparam_transform_results_cache.count({base_js_url, cur_n})) {
					next_n = yt_modify_nparam(match_result[1].str(), nparam_transform_function_cache[base_js_url]);
					nparam_transform_results_cache[{base_js_url, cur_n}] = next_n;
				} else next_n = nparam_transform_results_cache[{base_js_url, cur_n}];
				url = std::string(url.cbegin(), match_result[1].first) + next_n + std::string(match_result[1].second, url.cend());
			} else debug("failed to detect `n` parameter");
			if (url.find("ratebypass") == std::string::npos) url += "&ratebypass=yes";
			
			i.set_str(json_root, "url", url.c_str());
		}
	}
	for (auto &i : formats) i.set_str(json_root, "url", url_decode(i["url"].string_value()).c_str()); // something like %2C still appears in the url, so decode them back
	
	res.stream_fragment_len = -1;
	res.is_livestream = false;
	std::vector<RJson> audio_formats, video_formats;
	for (auto i : formats) {
		// std::cerr << i["itag"].int_value() << " : " << (i["contentLength"].string_value().size() ? "Yes" : "No") << std::endl;
		// if (i["contentLength"].string_value() == "") continue;
		if (i.has_key("targetDurationSec")) {
			int new_stream_fragment_len = i["targetDurationSec"].int_value();
			if (res.stream_fragment_len != -1 && res.stream_fragment_len != new_stream_fragment_len)
				debug("[unexp] diff targetDurationSec for diff streams");
			res.stream_fragment_len = new_stream_fragment_len;
			res.is_livestream = true;
		}
		if (i["approxDurationMs"].string_value() != "")
			res.duration_ms = stoll(i["approxDurationMs"].string_value());
		
		if (i["type"].string_value() == "FORMAT_STREAM_TYPE_OTF") continue;
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
		res.audio_stream_url = "";
		for (auto i : audio_formats) if (i["itag"].int_value() == 140) res.audio_stream_url = i["url"].string_value();
		
		if (res.audio_stream_url == "" && audio_formats.size()) res.audio_stream_url = audio_formats[0]["url"].string_value();
	}
	// video
	{
		std::map<int, int> itag_to_p = {
			{160, 144},
			{133, 240},
			{134, 360}
		};
		for (auto i : video_formats) {
			int cur_itag = i["itag"].int_value();
			if (itag_to_p.count(cur_itag)) {
				int p_value = itag_to_p[cur_itag];
				res.video_stream_urls[p_value] = i["url"].string_value();
			}
		}
		// both_stream_url : search for itag 18
		for (auto i : video_formats) if (i["itag"].int_value() == 18)
			res.both_stream_url = i["url"].string_value();
	}
	
	// extract caption data
	for (auto base_lang : player_response["captions"]["playerCaptionsTracklistRenderer"]["captionTracks"].array_items()) {
		YouTubeVideoDetail::CaptionBaseLanguage cur_lang;
		cur_lang.name = get_text_from_object(base_lang["name"]);
		cur_lang.id = base_lang["languageCode"].string_value();
		cur_lang.base_url = base_lang["baseUrl"].string_value();
		cur_lang.is_translatable = base_lang["isTranslatable"].bool_value();
		res.caption_base_languages.push_back(cur_lang);
	}
	for (auto translation_lang : player_response["captions"]["playerCaptionsTracklistRenderer"]["translationLanguages"].array_items()) {
		YouTubeVideoDetail::CaptionTranslationLanguage cur_lang;
		cur_lang.name = get_text_from_object(translation_lang["languageName"]);
		cur_lang.id = translation_lang["languageCode"].string_value();
		res.caption_translation_languages.push_back(cur_lang);
	}
	return true;
}

static void extract_like_dislike_counts(RJson buttons, YouTubeVideoDetail &res) {
	for (auto button : buttons.array_items()) if (button.has_key("slimMetadataToggleButtonRenderer")) {
		auto content = get_text_from_object(button["slimMetadataToggleButtonRenderer"]["button"]["toggleButtonRenderer"]["defaultText"]);
		if (content.size() && !isdigit(content[0])) content = "hidden";
		if (button["slimMetadataToggleButtonRenderer"]["isLike"].bool_value()) res.like_count_str = content;
		else if (button["slimMetadataToggleButtonRenderer"]["isDislike"].bool_value()) res.dislike_count_str = content;
		if (button["slimMetadataToggleButtonRenderer"]["target"]["videoId"].is_valid()) res.id = button["slimMetadataToggleButtonRenderer"]["target"]["videoId"].string_value();
	}
}

static void extract_owner(RJson slimOwnerRenderer, YouTubeVideoDetail &res) {
	res.author.id = slimOwnerRenderer["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
	// !!! res.author.url = slimOwnerRenderer["channelUrl"].string_value();
	res.author.name = slimOwnerRenderer["channelName"].string_value();
	res.author.subscribers = get_text_from_object(slimOwnerRenderer["expandedSubtitle"]);
	
	if (!slimOwnerRenderer["thumbnail"]["thumbnails"].array_items().size()) {
		debug("extract_owner : thumbnail not found");
		return;
	}
	auto thumbnail = slimOwnerRenderer["thumbnail"]["thumbnails"].array_items()[0];
	
	constexpr int height = 72;
	
	std::string icon_url = thumbnail["url"].string_value();
	std::string icon_url_modified;
	std::string replace_from = "s" + std::to_string(thumbnail["width"].int_value()) + "-";
	std::string replace_to = "s" + std::to_string(height) + "-";
	for (size_t i = 0; i < icon_url.size(); ) {
		if (icon_url.substr(i, replace_from.size()) == replace_from) {
			i += replace_from.size();
			icon_url_modified += replace_to;
		} else icon_url_modified.push_back(icon_url[i++]);
	}
	res.author.icon_url = icon_url_modified;
	
	if (res.author.icon_url.substr(0, 2) == "//") res.author.icon_url = "https:" + res.author.icon_url;
}

static void extract_item(RJson content, YouTubeVideoDetail &res) {
	auto get_video_from_renderer = [&] (RJson video_renderer) {
		YouTubeVideoSuccinct cur_video;
		std::string video_id = video_renderer["videoId"].string_value();
		cur_video.url = youtube_get_video_url_by_id(video_id);
		cur_video.title = get_text_from_object(video_renderer["headline"]);
		cur_video.duration_text = get_text_from_object(video_renderer["lengthText"]);
		cur_video.views_str = get_text_from_object(video_renderer["shortViewCountText"]);
		cur_video.author = get_text_from_object(video_renderer["shortBylineText"]);
		cur_video.thumbnail_url = youtube_get_video_thumbnail_url_by_id(video_id);
		return cur_video;
	};
	if (content.has_key("slimVideoMetadataRenderer")) {
		RJson metadata_renderer = content["slimVideoMetadataRenderer"];
		res.title = get_text_from_object(metadata_renderer["title"]);
		res.description = get_text_from_object(metadata_renderer["description"]);
		res.views_str = get_text_from_object(metadata_renderer["expandedSubtitle"]);
		res.publish_date = get_text_from_object(metadata_renderer["dateText"]);
		extract_like_dislike_counts(metadata_renderer["buttons"], res);
		extract_owner(metadata_renderer["owner"]["slimOwnerRenderer"], res);
	} else if (content.has_key("compactAutoplayRenderer")) {
		for (auto j : content["compactAutoplayRenderer"]["contents"].array_items()) if (j.has_key("videoWithContextRenderer"))
			res.suggestions.push_back(get_video_from_renderer(j["videoWithContextRenderer"]));
	} else if (content.has_key("videoWithContextRenderer"))
		res.suggestions.push_back(get_video_from_renderer(content["videoWithContextRenderer"]));
	else if (content.has_key("continuationItemRenderer"))
		res.suggestions_continue_token = content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
	else if (content.has_key("compactRadioRenderer") || content.has_key("compactPlaylistRenderer")) {
		auto playlist_renderer = content.has_key("compactRadioRenderer") ? content["compactRadioRenderer"] : content["compactPlaylistRenderer"];
		
		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items())
			if (thumbnail["url"].string_value().find("/default.jpg") != std::string::npos) cur_list.thumbnail_url = thumbnail["url"].string_value();
		
		cur_list.url = convert_url_to_mobile(playlist_renderer["shareUrl"].string_value());
		if (!starts_with(cur_list.url, "https://m.youtube.com/watch", 0)) {
			if (starts_with(cur_list.url, "https://m.youtube.com/playlist?", 0)) {
				auto params = parse_parameters(cur_list.url.substr(std::string("https://m.youtube.com/playlist?").size(), cur_list.url.size()));
				auto playlist_id = params["list"];
				auto video_id = get_video_id_from_thumbnail_url(cur_list.thumbnail_url);
				cur_list.url = "https://m.youtube.com/watch?v=" + video_id + "&list=" + playlist_id;
			} else {
				debug("unknown playlist url");
				return;
			}
		}
		
		res.suggestions.push_back(YouTubeSuccinctItem(cur_list));
	}
}

static void extract_metadata(RJson data, YouTubeVideoDetail &res) {
	{
		auto contents = data["contents"]["singleColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content.has_key("itemSectionRenderer")) {
				for (auto i : content["itemSectionRenderer"]["contents"].array_items()) extract_item(i, res);
			} else if (content.has_key("slimVideoMetadataSectionRenderer")) {
				for (auto i : content["slimVideoMetadataSectionRenderer"]["contents"].array_items()) {
					if (i.has_key("slimVideoInformationRenderer")) res.title = get_text_from_object(i["slimVideoInformationRenderer"]["title"]);
					if (i.has_key("slimVideoActionBarRenderer")) extract_like_dislike_counts(i["slimVideoActionBarRenderer"]["buttons"], res);
					if (i.has_key("slimOwnerRenderer")) extract_owner(i["slimOwnerRenderer"], res);
					if (i.has_key("slimVideoDescriptionRenderer")) res.description = get_text_from_object(i["slimVideoDescriptionRenderer"]["description"]);
				}
			}
		}
	}
	RJson playlist_object = data["contents"]["singleColumnWatchNextResults"]["playlist"]["playlist"];
	if (playlist_object.is_valid()) {
		res.playlist.id = playlist_object["playlistId"].string_value();
		res.playlist.selected_index = -1;
		res.playlist.author_name = get_text_from_object(playlist_object["ownerName"]);
		res.playlist.title = playlist_object["title"].string_value();
		res.playlist.total_videos = playlist_object["totalVideos"].int_value();
		for (auto playlist_item : playlist_object["contents"].array_items()) {
			if (playlist_item.has_key("playlistPanelVideoRenderer")) {
				YouTubeVideoSuccinct cur_video;
				auto renderer = playlist_item["playlistPanelVideoRenderer"];
				cur_video.url = youtube_get_video_url_by_id(renderer["videoId"].string_value()) + "&list=" + res.playlist.id;
				cur_video.title = get_text_from_object(renderer["title"]);
				cur_video.duration_text = get_text_from_object(renderer["lengthText"]);
				cur_video.author = get_text_from_object(renderer["longBylineText"]);
				cur_video.thumbnail_url = youtube_get_video_thumbnail_url_by_id(renderer["videoId"].string_value());
				if (renderer["selected"].bool_value()) res.playlist.selected_index = res.playlist.videos.size();
				res.playlist.videos.push_back(cur_video);
			}
		}
	}
	res.comment_continue_type = -1;
	res.comments_disabled = true;
	for (auto i : data["engagementPanels"].array_items()) {
		for (auto j : i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["continuations"].array_items()) if (j.has_key("reloadContinuationData")) {
			res.comment_continue_token = j["reloadContinuationData"]["continuation"].string_value();
			res.comment_continue_type = 0;
			res.comments_disabled = false;
		}
		for (auto j : i["engagementPanelSectionListRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
			for (auto k : j["itemSectionRenderer"]["contents"].array_items()) if (k.has_key("continuationItemRenderer")) {
				res.comment_continue_token = k["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				res.comment_continue_type = 1;
				res.comments_disabled = false;
			}
		}
		for (auto j : i["engagementPanelSectionListRenderer"]["content"]["structuredDescriptionContentRenderer"]["items"].array_items()) {
			if (j.has_key("expandableVideoDescriptionBodyRenderer"))
				res.description = get_text_from_object(j["expandableVideoDescriptionBodyRenderer"]["descriptionBodyText"]);
			if (j.has_key("videoDescriptionHeaderRenderer")) {
				res.publish_date = get_text_from_object(j["videoDescriptionHeaderRenderer"]["publishDate"]);
				res.views_str = get_text_from_object(j["videoDescriptionHeaderRenderer"]["views"]);
			}
		}
	}
}

YouTubeVideoDetail youtube_parse_video_page(std::string url) {
	YouTubeVideoDetail res;
	
	bool cur_quick_mode = quick_mode;
	if (cur_quick_mode) cur_quick_mode = (youtube_get_video_id_by_url(url) != "");
	
	if (innertube_key == "" || base_js_url == "") fetch_innertube_key_and_player();
	if (innertube_key == "" || base_js_url == "") {
		res.error = "innertube key or base.js url empty";
		return res;
	}
	if (cur_quick_mode) {
		res.id = youtube_get_video_id_by_url(url);
		std::string playlist_id = youtube_get_playlist_id_by_url(url);
		
		const std::string innertube_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8"; // hardcoded
		
		std::string post_content = R"({"videoId": "%0", %1"context": {"client": {"hl": "%2","gl": "%3","clientName": "MWEB","clientVersion": "2.20220308.01.00"}}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": %4}}})";
		post_content = std::regex_replace(post_content, std::regex("%0"), res.id);
		post_content = std::regex_replace(post_content, std::regex("%1"), playlist_id == "" ? "" : "\"playlistId\": \"" + playlist_id + "\", ");
		post_content = std::regex_replace(post_content, std::regex("%2"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%3"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%4"), std::to_string(sts));
		std::string urls[2] = {
			get_innertube_api_url("next"),
			get_innertube_api_url("player")
		};
#		ifdef _WIN32
		std::string json_str[2]; // {/next, /player}
		for (int i = 0; i < 2; i++) json_str[i] = http_post_json(urls[i], post_content);
		for (int i = 0; i < 2; i++) {
			parse_json_destructive(&json_str[i][0],
				[&] (Document &json_root, RJson data) {
					if (i == 0) extract_metadata(data, res);
					else extract_player_data(json_root, data, res, true);
				},
				[&] (const std::string &error) {
					res.error = "[v-#" + std::to_string(i) + "] " + error;
					debug(res.error);
				}
			);
		}
#		else
		debug("accessing(multi)...");
		std::vector<NetworkResult> results;
		{
			std::vector<HttpRequest> requests;
			for (int i = 0; i < 2; i++) requests.push_back(http_post_json_request(urls[i], post_content));
			results = thread_network_session_list.perform(requests);
			bool fail = false;
			for (int i = 0; i < 2; i++) if (results[i].fail) {
				fail = true;
				debug("#" + std::to_string(i) + " fail");
			}
			if (!fail) debug("ok");
		}
		for (int i = 0; i < 2; i++) {
			results[i].data.push_back('\0');
			parse_json_destructive((char *) &results[i].data[0],
				[&] (Document &json_root, RJson data) {
					if (i == 0) extract_metadata(data, res);
					else extract_player_data(json_root, data, res, true);
				},
				[&] (const std::string &error) {
					res.error = "[v-#" + std::to_string(i) + "] " + error;
					debug(res.error);
				}
			);
		}
#		endif
	} else {
		std::string html = http_get(url);
		if (!html.size()) {
			res.error = "failed to download video page";
			return res;
		}
		Document json_root[2];
		RJson json[2];
		json[0] = get_initial_data(json_root[0], html);
		json[1] = initial_player_response(json_root[1], html);
		extract_metadata(json[0], res);
		extract_player_data(json_root[1], json[1], res, true);
		
		if (res.id.size() != 11) res.id = youtube_get_video_id_by_url(url);
	}

#	ifdef _WIN32
	// for debug purpose, to check whether the extracted stream url is working
	if (res.audio_stream_url != "") {
		auto tmp_data = http_get(res.audio_stream_url, {{"Range", "bytes=0-400000"}});
		if (tmp_data.size() != 400001) {
			debug("!!!!!!!!!!!!!!!!!!!!! SIZE DIFFER : " + std::to_string(tmp_data.size()) + " !!!!!!!!!!!!!!!!!!!!!");
		} else debug("----------------------- OK -----------------------");
	} else debug("!!!!!!!!!!!!!!!!!!!!! AUDIO STREAM URL EMPTY !!!!!!!!!!!!!!!!!!!!!");
#	endif
	
	if (res.id != "") res.succinct_thumbnail_url = youtube_get_video_thumbnail_url_by_id(res.id);
#	ifndef _WIN32
	if (res.title != "" && res.id != "") {
		HistoryVideo video;
		video.id = res.id;
		video.title = res.title;
		video.author_name = res.author.name;
		video.length_text = Util_convert_seconds_to_time((double) res.duration_ms / 1000);
		video.my_view_count = 1;
		video.last_watch_time = time(NULL);
		add_watched_video(video);
		misc_tasks_request(TASK_SAVE_HISTORY);
	}
#	endif
	
	debug(res.title != "" ? res.title : "preason : " + res.playability_reason);
	return res;
}

YouTubeVideoDetail youtube_video_page_load_more_suggestions(const YouTubeVideoDetail &prev_result) {
	YouTubeVideoDetail new_result = prev_result;
	
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") {
		new_result.error = "innertube key empty";
		return new_result;
	}
	if (prev_result.suggestions_continue_token == "") {
		new_result.error = "suggestion continue token empty";
		return new_result;
	}
	
	// POST to get more results
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
		+ prev_result.suggestions_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("next"), post_content); },
		[&] (Document &, RJson yt_result) {
			new_result.suggestions_continue_token = "";
			for (auto i : yt_result["onResponseReceivedEndpoints"].array_items())
				for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items())
					extract_item(j, new_result);
		},
		[&] (const std::string &error) {
			new_result.error = "[v-sug+] " + error;
			debug(new_result.error);
		}
	);
	
	return new_result;
}

YouTubeVideoDetail::Comment extract_comment_from_comment_renderer(RJson comment_renderer, int thumbnail_height) {
	YouTubeVideoDetail::Comment cur_comment;
	// get the icon of the author with minimum size
	cur_comment.id = comment_renderer["commentId"].string_value();
	cur_comment.content = get_text_from_object(comment_renderer["contentText"]);
	cur_comment.reply_num = comment_renderer["replyCount"].int_value(); // RJson.int_value() defaults to zero, so... it works
	cur_comment.author.id = comment_renderer["authorEndpoint"]["browseEndpoint"]["browseId"].string_value();
	// !!! cur_comment.author.url = "https://m.youtube.com" + comment_renderer["authorEndpoint"]["browseEndpoint"]["canonicalBaseUrl"].string_value();
	cur_comment.author.name = get_text_from_object(comment_renderer["authorText"]);
	cur_comment.publish_date = get_text_from_object(comment_renderer["publishedTimeText"]);
	cur_comment.upvotes_str = get_text_from_object(comment_renderer["voteCount"]);
	if (comment_renderer["authorThumbnail"]["thumbnails"].array_items().size()) {
		auto thumbnail = comment_renderer["authorThumbnail"]["thumbnails"].array_items()[0];
		std::string icon_url = thumbnail["url"].string_value();
		std::string icon_url_modified;
		std::string replace_from = "s" + std::to_string(thumbnail["width"].int_value()) + "-";
		std::string replace_to = "s" + std::to_string(thumbnail_height) + "-";
		for (size_t i = 0; i < icon_url.size(); ) {
			if (icon_url.substr(i, replace_from.size()) == replace_from) {
				i += replace_from.size();
				icon_url_modified += replace_to;
			} else icon_url_modified.push_back(icon_url[i++]);
		}
		cur_comment.author.icon_url = icon_url_modified;
	}
	return cur_comment;
	
}
YouTubeVideoDetail youtube_video_page_load_more_comments(const YouTubeVideoDetail &prev_result) {
	YouTubeVideoDetail new_result = prev_result;
	
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") {
		new_result.error = "innertube key empty";
		return new_result;
	}
	if (prev_result.comment_continue_type == -1) {
		new_result.error = "No more comments available";
		return new_result;
	}
	
	auto parse_comment_thread_renderer = [&] (RJson comment_thread_renderer) {
		auto cur_comment = extract_comment_from_comment_renderer(comment_thread_renderer["commentThreadRenderer"]["comment"]["commentRenderer"], 48);
		// get the icon of the author with minimum size
		for (auto i : comment_thread_renderer["commentThreadRenderer"]["replies"]["commentRepliesRenderer"]["contents"].array_items()) if (i.has_key("continuationItemRenderer"))
			cur_comment.replies_continue_token = i["continuationItemRenderer"]["button"]["buttonRenderer"]["command"]["continuationCommand"]["token"].string_value();
		return cur_comment;
	};
	
	if (prev_result.comment_continue_type == 0) {
		access_and_parse_json(
			[&] () { return http_get("https://m.youtube.com/watch_comment?action_get_comments=1&pbj=1&ctoken=" + prev_result.comment_continue_token, 
				{{"X-YouTube-Client-Version", "2.20210714.00.00"}, {"X-YouTube-Client-Name", "2"}});
			},
			[&] (Document &, RJson yt_result) {
				new_result.comment_continue_type = -1;
				new_result.comment_continue_token = "";
				for (auto i : yt_result.array_items()) {
					for (auto comment : i["response"]["continuationContents"]["commentSectionContinuation"]["items"].array_items()) if (comment.has_key("commentThreadRenderer"))
						new_result.comments.push_back(parse_comment_thread_renderer(comment));
					for (auto j : i["response"]["continuationContents"]["commentSectionContinuation"]["continuations"].array_items()) if (j.has_key("nextContinuationData")) {
						new_result.comment_continue_token = j["nextContinuationData"]["continuation"].string_value();
						new_result.comment_continue_type = 0;
					}
				}
			},
			[&] (const std::string &error) {
				new_result.error = "[v-com+0] " + error;
				debug(new_result.error);
			}
		);
	} else {
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
			+ prev_result.comment_continue_token + "\"}";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		
		access_and_parse_json(
			[&] () { return http_post_json(get_innertube_api_url("next"), post_content); },
			[&] (Document &, RJson yt_result) {
				new_result.comment_continue_type = -1;
				new_result.comment_continue_token = "";
				for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
					RJson continuation_items = i.has_key("reloadContinuationItemsCommand") ? i["reloadContinuationItemsCommand"]["continuationItems"]
						: i["appendContinuationItemsAction"]["continuationItems"];
					
					for (auto comment : continuation_items.array_items()) {
						if (comment.has_key("commentThreadRenderer")) new_result.comments.push_back(parse_comment_thread_renderer(comment));
						if (comment.has_key("continuationItemRenderer")) {
							new_result.comment_continue_token = comment["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
							new_result.comment_continue_type = 1;
						}
					}
				}
			},
			[&] (const std::string &error) {
				new_result.error = "[v-com+1] " + error;
				debug(new_result.error);
			}
		);
	}
	return new_result;
}

YouTubeVideoDetail::Comment youtube_video_page_load_more_replies(const YouTubeVideoDetail::Comment &comment) {
	YouTubeVideoDetail::Comment res = comment;
	res.replies_continue_token = "";
	
	if (!comment.has_more_replies()) {
		debug("load_more_replies on a comment that has no replies to load");
		return comment;
	}
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") return comment;
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
		+ comment.replies_continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("next"), post_content); },
		[&] (Document &, RJson yt_result) {
			for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) {
				for (auto item : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (item.has_key("commentRenderer")) res.replies.push_back(extract_comment_from_comment_renderer(item["commentRenderer"], 32));
					if (item.has_key("continuationItemRenderer"))
						res.replies_continue_token = item["continuationItemRenderer"]["button"]["buttonRenderer"]["command"]["continuationCommand"]["token"].string_value();
				}
			}
		},
		[&] (const std::string &error) { debug("[v-rep+] " + error); }
	);
	
	return res;
}

YouTubeVideoDetail youtube_video_page_load_caption(const YouTubeVideoDetail &prev_result, const std::string &base_lang_id, const std::string &translation_lang_id) {
	YouTubeVideoDetail new_result = prev_result;
	
	int base_lang_index = -1;
	for (size_t i = 0; i < prev_result.caption_base_languages.size(); i++) if (prev_result.caption_base_languages[i].id == base_lang_id) {
		base_lang_index = i;
		break;
	}
	if (base_lang_index == -1) {
		debug("[caption] unknown base language");
		return new_result;
	}
		
	int translation_lang_index = -1;
	if (translation_lang_id != "") {
		for (size_t i = 0; i < prev_result.caption_translation_languages.size(); i++) if (prev_result.caption_translation_languages[i].id == translation_lang_id) {
			translation_lang_index = i;
			break;
		}
		if (translation_lang_index == -1) {
			debug("[caption] unknown translation language");
			return new_result;
		}
	}
	
	std::string url = "https://m.youtube.com" + prev_result.caption_base_languages[base_lang_index].base_url;
	if (translation_lang_id != "") url += "&tlang=" + translation_lang_id;
	url += "&fmt=json3&xorb=2&xobt=3&xovt=3"; // the meanings of xorb, xobt, xovt are unknwon, and these three parameters seem to be unnecessary
	
	access_and_parse_json(
		[&] () { return http_get(url); },
		[&] (Document &, RJson yt_result) {
			std::vector<YouTubeVideoDetail::CaptionPiece> cur_caption;
			for (auto caption_piece : yt_result["events"].array_items()) {
				if (!caption_piece.has_key("segs")) continue;
				YouTubeVideoDetail::CaptionPiece cur_caption_piece;
				cur_caption_piece.start_time = caption_piece["tStartMs"].int_value() / 1000.0;
				cur_caption_piece.end_time = cur_caption_piece.start_time + caption_piece["dDurationMs"].int_value() / 1000.0;
				for (auto seg : caption_piece["segs"].array_items()) cur_caption_piece.content += seg["utf8"].string_value();
				
				cur_caption.push_back(cur_caption_piece);
			}
			new_result.caption_data[{base_lang_id, translation_lang_id}] = cur_caption;
		},
		[&] (const std::string &error) {
			new_result.error = "[v-cap+] " + error;
			debug(new_result.error);
		}
	);
	
	return new_result;
}


std::string youtube_get_video_id_by_url(const std::string &url) {
	auto pos = url.find("?v=");
	if (pos == std::string::npos) pos = url.find("&v=");
	if (pos != std::string::npos) {
		std::string res = url.substr(pos + 3, 11);
		return youtube_is_valid_video_id(res) ? res : "";
	}
	return "";
}
std::string youtube_get_playlist_id_by_url(const std::string &url) {
	auto pos = url.find("?list=");
	if (pos == std::string::npos) pos = url.find("&list=");
	if (pos != std::string::npos) {
		pos += 6;
		std::string res;
		for (size_t i = pos; i < url.size() && url[i] != '&'; i++) res.push_back(url[i]);
		return res;
	}
	return "";
}
std::string youtube_get_video_thumbnail_url_by_id(const std::string &id) {
	return "https://i.ytimg.com/vi/" + id + "/default.jpg";
}
std::string youtube_get_video_url_by_id(const std::string &id) {
	return "https://m.youtube.com/watch?v=" + id;
}
std::string get_video_id_from_thumbnail_url(const std::string &url) {
	auto pos = url.find("i.ytimg.com/vi/");
	if (pos == std::string::npos) return "";
	pos += std::string("i.ytimg.com/vi/").size();
	std::string res;
	while (pos < url.size() && url[pos] != '/') res.push_back(url[pos++]);
	return res;
}
bool youtube_is_valid_video_id(const std::string &id) {
	for (auto c : id) if (!isalnum(c) && c != '-' && c != '_') return false;
	if (id.size() != 11) return false;
	return true;
}
bool is_youtube_url(const std::string &url) {
	std::vector<std::string> patterns = {
		"https://m.youtube.com/",
		"https://www.youtube.com/"
	};
	for (auto pattern : patterns) if (starts_with(url, pattern, 0)) return true;
	return false;
}
bool is_youtube_thumbnail_url(const std::string &url) {
	std::vector<std::string> patterns = {
		"https://i.ytimg.com/vi/",
		"https://yt3.ggpht.com/"
	};
	for (auto pattern : patterns) if (starts_with(url, pattern, 0)) return true;
	return false;
}
YouTubePageType youtube_get_page_type(std::string url) {
	url = convert_url_to_mobile(url);
	if (starts_with(url, "https://m.youtube.com/watch?", 0)) return YouTubePageType::VIDEO;
	if (starts_with(url, "https://m.youtube.com/user/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/channel/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/c/", 0)) return YouTubePageType::CHANNEL;
	if (starts_with(url, "https://m.youtube.com/results?", 0)) return YouTubePageType::SEARCH;
	return YouTubePageType::INVALID;
}
