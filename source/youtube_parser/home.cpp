#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"

YouTubeHomeResult youtube_load_home_page() {
	YouTubeHomeResult res;
	
	std::string post_content = R"({"context": {"client": {"hl": "$0", "gl": "$1", "clientName": "MWEB", "clientVersion": "2.20220407.00.00"}}, "browseId": "FEwhat_to_watch"})";
	post_content = std::regex_replace(post_content, std::regex("\\$0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("\\$1"), country_code);
	
	access_and_parse_json(
		[&] () { return http_post_json(get_innertube_api_url("browse"), post_content); },
		[&] (Document &, RJson yt_result) {
			res.visitor_data = yt_result["responseContext"]["visitorData"].string_value();
			for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
				for (auto item : tab["tabRenderer"]["content"]["richGridRenderer"]["contents"].array_items()) {
					if (item.has_key("richItemRenderer")) res.videos.push_back(parse_succinct_video(item["richItemRenderer"]["content"]["videoWithContextRenderer"]));
					if (item.has_key("continuationItemRenderer"))
						res.continue_token = item["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				}
			}
		},
		[&] (const std::string &error) {
			res.error = "[home] " + error;
			debug(res.error);
		}
	);
	
	return res;
}
void YouTubeHomeResult::load_more_results() {
	if (continue_token == "") error = "[home] continue token not set";
	if (visitor_data == "") error = "[home] visitor data not set";
	if (error != "") {
		debug(error);
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
				for (auto item : action["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (item.has_key("richItemRenderer")) videos.push_back(parse_succinct_video(item["richItemRenderer"]["content"]["videoWithContextRenderer"]));
					if (item.has_key("continuationItemRenderer"))
						continue_token = item["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				}
			}
		},
		[&] (const std::string &error) { debug((this->error = "[home-c] " + error)); }
	);
}

