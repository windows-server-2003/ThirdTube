#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

YouTubeHomeResult youtube_load_home_page() {
	YouTubeHomeResult res;
	
	std::string post_content = R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "MWEB", "clientVersion": "2.20220407.00.00"}}, "browseId": "FEtrending"})";
	post_content = std::regex_replace(post_content, std::regex("\\$0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("\\$1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
		[&] (Document &, RJson yt_result) {
			res.visitor_data = yt_result["responseContext"]["visitorData"].string_value();
			for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
				for (auto section : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
					if (section.has_key("continuationItemRenderer")) 
						res.continue_token = section["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
					for (auto item : section["itemSectionRenderer"]["contents"].array_items()) {
						if (item.has_key("videoWithContextRenderer")) res.videos.push_back(parse_succinct_video(item["videoWithContextRenderer"]));
					}
				}
			}
		},
		[&] (const std::string &error) {
			res.error = "[home] " + error;
			debug_error(res.error);
		}
	);
	
	return res;
}
void YouTubeHomeResult::load_more_results() {
	if (continue_token == "") error = "[home] continue token not set";
	if (visitor_data == "") error = "[home] visitor data not set";
	if (error != "") {
		debug_error(error);
		return;
	}
	
	std::string post_content = R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "MWEB", "clientVersion": "2.20220407.00.00", "visitorData": "$2"}}, "continuation": "$3"})";
	post_content = std::regex_replace(post_content, std::regex("\\$0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("\\$1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("\\$2"), visitor_data);
	post_content = std::regex_replace(post_content, std::regex("\\$3"), continue_token);
	
	continue_token = "";
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
		[&] (Document &, RJson yt_result) {
			if (yt_result["responseContext"]["visitorData"].string_value() != "") visitor_data = yt_result["responseContext"]["visitorData"].string_value();
			for (auto action : yt_result["onResponseReceivedActions"].array_items()) {
				for (auto section : action["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (section.has_key("continuationItemRenderer")) 
						continue_token = section["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
					for (auto item : section["itemSectionRenderer"]["contents"].array_items()) {
						if (item.has_key("videoWithContextRenderer")) videos.push_back(parse_succinct_video(item["videoWithContextRenderer"]));
					}
				}
			}
		},
		[&] (const std::string &error) { debug_error((this->error = "[home-c] " + error)); }
	);
}

