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

static void extract_metadata(YouTubeVideoDetail &res, const std::string &html) {
	Json initial_data = get_initial_data(html);
	
	auto extract_owner = [&] (Json slimOwnerRenderer) {
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
	};
	
	bool ok = false;
	{
		auto contents = initial_data["contents"]["singleColumnWatchNextResults"]["results"]["results"]["contents"];
		for (auto content : contents.array_items()) {
			if (content["itemSectionRenderer"] != Json()) {
				for (auto i : content["itemSectionRenderer"]["contents"].array_items()) if (i["slimVideoMetadataRenderer"] != Json()) {
					Json metadata_renderer = i["slimVideoMetadataRenderer"];
					res.title = get_text_from_object(metadata_renderer["title"]);
					extract_owner(metadata_renderer["owner"]["slimOwnerRenderer"]);
					ok = true;
					break;
				}
			} else if (content["slimVideoMetadataSectionRenderer"] != Json()) {
				for (auto i : content["slimVideoMetadataSectionRenderer"]["contents"].array_items()) {
					if (i["slimVideoInformationRenderer"] != Json()) {
						res.title = get_text_from_object(i["slimVideoInformationRenderer"]["title"]);
					}
					if (i["slimOwnerRenderer"] != Json()) {
						extract_owner(i["slimOwnerRenderer"]);
					}
				}
				ok = true;
			}
			if (ok) break;
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
	debug(res.author.name);
	debug(res.author.url);
	debug(res.author.icon_url);
	debug(res.audio_stream_url);
	debug(res.video_stream_url);
	debug(res.both_stream_url);
	return res;
}
#ifdef _WIN32
int main() {
	std::string url;
	std::cin >> url;
	youtube_parse_video_page(url);
	return 0;
}
#endif

