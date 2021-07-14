/*
	reference :
	https://github.com/pytube/pytube
*/

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
				// debug(j.dump());
				if (j["compactVideoRenderer"] != Json()) {
					auto video_renderer = j["compactVideoRenderer"];
					YouTubeVideoSuccinct cur_result;
					std::string video_id = video_renderer["navigationEndpoint"]["watchEndpoint"]["videoId"].string_value();
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
					res.results.push_back(YouTubeSearchResult::Item(cur_result));
				} else if (j["compactChannelRenderer"] != Json()) {
					auto channel_renderer = j["compactChannelRenderer"];
					YouTubeChannelSuccinct cur_result;
					cur_result.name = get_text_from_object(channel_renderer["displayName"]);
					
					res.results.push_back(YouTubeSearchResult::Item(cur_result));
				}
			}
		}
		if (i["continuationItemRenderer"] != Json()) {
			std::string continuation_string = i["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
			debug(continuation_string);
		}
	}
	{
		std::regex pattern(std::string(R"***("INNERTUBE_API_KEY":"([\w-]+)")***"));
		std::smatch match_result;
		if (std::regex_search(html, match_result, pattern)) {
			debug(match_result[1].str());
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

#ifdef _WIN32
int main() {
	std::string url;
	std::cin >> url;
	youtube_parse_search(url);
	return 0;
}
#endif

