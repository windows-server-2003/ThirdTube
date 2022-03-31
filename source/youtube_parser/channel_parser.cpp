#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"


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

static void parse_channel_data(RJson data, YouTubeChannelDetail &res) {
	std::string channel_name = "stub channel name";
	
	auto metadata_renderer = data["metadata"]["channelMetadataRenderer"];
	res.name = metadata_renderer["title"].string_value();
	res.subscriber_count_str = get_text_from_object(data["header"]["c4TabbedHeaderRenderer"]["subscriberCountText"]);
	res.id = metadata_renderer["externalId"].string_value();
	res.url = "https://m.youtube.com/channel/" + metadata_renderer["externalId"].string_value();
	res.description = metadata_renderer["description"].string_value();
	
	for (auto tab : data["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
			for (auto content : i["itemSectionRenderer"]["contents"].array_items()) {
				if (content.has_key("compactVideoRenderer")) {
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
				} else if (content.has_key("continuationItemRenderer")) {
					res.continue_token = content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				} else debug("unknown item found in channel videos");
			}
		}
		std::string tab_url = tab["tabRenderer"]["endpoint"]["commandMetadata"]["webCommandMetadata"]["url"].string_value();
		if (ends_with(tab_url, "/playlists")) {
			res.playlist_tab_browse_id = tab["tabRenderer"]["endpoint"]["browseEndpoint"]["browseId"].string_value();
			res.playlist_tab_params = tab["tabRenderer"]["endpoint"]["browseEndpoint"]["params"].string_value();
		}
	}
	{ // top banner
		for (auto banner : data["header"]["c4TabbedHeaderRenderer"]["banner"]["thumbnails"].array_items()) {
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
		int min_width = 1000000000;
		for (auto icon : data["header"]["c4TabbedHeaderRenderer"]["avatar"]["thumbnails"].array_items()) {
			int cur_width = icon["width"].int_value();
			if (min_width > cur_width) {
				min_width = cur_width;
				res.icon_url = icon["url"].string_value();
			}
		}
		if (res.icon_url.substr(0, 2) == "//") res.icon_url = "https:" + res.icon_url;
	}
}

YouTubeChannelDetail youtube_parse_channel_page(std::string url_or_id) {
	YouTubeChannelDetail res;
	
	if (starts_with(url_or_id, "http://") || starts_with(url_or_id, "https://")) {
		std::string &url = url_or_id;
		res.url_original = url;
		
		url = convert_url_to_mobile(url);
		
		// append "/videos" at the end of the url
		{
			bool ok = false;
			for (auto pattern : std::vector<std::string>{"https://m.youtube.com/channel/", "https://m.youtube.com/c/", "https://m.youtube.com/user/"}) {
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
		Document json_root;
		parse_channel_data(get_initial_data(json_root, html), res);
	} else {
		if (innertube_key == "") fetch_innertube_key_and_player();
		if (innertube_key == "") {
			res.error = "innertube key empty";
			return res;
		}
		
		std::string &id = url_or_id;
		res.url_original = "https://m.youtube.com/channel/" + id;
		
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3M%3D"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), id);
		
		std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + innertube_key;
		
		access_and_parse_json(
			[&] () { return http_post_json(post_url, post_content); },
			[&] (Document &, RJson json) { parse_channel_data(json, res); },
			[&] (const std::string &error) {
				res.error = "[ch-id] " + error;
				debug(res.error);
			}
		);
	}
	return res;
}
std::vector<YouTubeChannelDetail> youtube_parse_channel_page_multi(std::vector<std::string> ids, std::function<void (int, int)> progress) {
	std::vector<YouTubeChannelDetail> res;
	if (progress) progress(0, ids.size());
#ifdef _WIN32
	int finished = 0;
	for (auto id : ids) {
		res.push_back(youtube_parse_channel_page(id));
		if (progress) progress(++finished, ids.size());
	}
#else
	std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + innertube_key + "&prettyPrint=false";
	
	std::vector<HttpRequest> requests;
	int n = ids.size();
	int finished = 0;
	for (int i = 0; i < n; i++) {
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00"}}, "browseId": "%2", "params":"EgZ2aWRlb3M%3D"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), ids[i]);
		requests.push_back(http_post_json_request(post_url, post_content).with_on_finish_callback([&] (NetworkResult &, int cur) {
			if (progress) progress(++finished, n);
		}));
	}
	debug("access(multi)...");
	auto results = thread_network_session_list.perform(requests);
	debug("ok");
	for (auto result : results) {
		result.data.push_back('\0');
		YouTubeChannelDetail cur_res;
		parse_json_destructive((char *) &result.data[0],
			[&] (Document &, RJson data) { parse_channel_data(data, cur_res); },
			[&] (const std::string &error) {
				cur_res.error = "[ch-mul] " + error;
				debug(cur_res.error);
			}
		);
		res.push_back(cur_res);
	}
#endif
	return res;
}


YouTubeChannelDetail youtube_channel_page_continue(const YouTubeChannelDetail &prev_result) {
	YouTubeChannelDetail new_result = prev_result;
	
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") {
		new_result.error = "innertube key empty";
		return new_result;
	}
	if (prev_result.continue_token == "") {
		new_result.error = "continue token empty";
		return new_result;
	}
	
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}, "request": {}, "user": {}}, "continuation": ")"
		+ prev_result.continue_token + "\"}";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	
	std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + innertube_key;
	
	access_and_parse_json(
		[&] () { return http_post_json(post_url, post_content); },
		[&] (Document &, RJson yt_result) {
			new_result.continue_token = "";
			
			for (auto i : yt_result["onResponseReceivedActions"].array_items()) {
				for (auto j : i["appendContinuationItemsAction"]["continuationItems"].array_items()) {
					if (j.has_key("compactVideoRenderer")) {
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
					} else if (j.has_key("continuationItemRenderer"))
						new_result.continue_token = j["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
				}
			}
			if (new_result.continue_token == "") debug("failed to get next continue token");
		},
		[&] (const std::string &error) {
			new_result.error = "[ch+] " + error;
			debug(new_result.error);
		}
	);
	
	return new_result;
}




static void channel_load_playlists_(RJson yt_result, YouTubeChannelDetail &new_result) {
	auto convert_compact_playlist_renderer = [] (RJson playlist_renderer) {
		YouTubePlaylistSuccinct cur_list;
		cur_list.title = get_text_from_object(playlist_renderer["title"]);
		cur_list.video_count_str = get_text_from_object(playlist_renderer["videoCountText"]);
		for (auto thumbnail : playlist_renderer["thumbnail"]["thumbnails"].array_items())
			if (std::string(thumbnail["url"].string_value()).find("/default.jpg") != std::string::npos) cur_list.thumbnail_url = thumbnail["url"].string_value();
		
		cur_list.url = convert_url_to_mobile(playlist_renderer["shareUrl"].string_value());
		if (!starts_with(cur_list.url, "https://m.youtube.com/watch", 0)) {
			if (starts_with(cur_list.url, "https://m.youtube.com/playlist?", 0)) {
				auto params = parse_parameters(cur_list.url.substr(std::string("https://m.youtube.com/playlist?").size(), cur_list.url.size()));
				auto playlist_id = params["list"];
				auto video_id = get_video_id_from_thumbnail_url(cur_list.thumbnail_url);
				cur_list.url = "https://m.youtube.com/watch?v=" + video_id + "&list=" + playlist_id;
			} else {
				debug("unknown playlist url");
				return cur_list;
			}
		}
		return cur_list;
	};
	
	for (auto tab : yt_result["contents"]["singleColumnBrowseResultsRenderer"]["tabs"].array_items()) {
		for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items()) {
			if (i.has_key("shelfRenderer")) {
				std::string category_name = get_text_from_object(i["shelfRenderer"]["title"]);
				std::vector<YouTubePlaylistSuccinct> playlists;
				for (auto j : i["shelfRenderer"]["content"]["verticalListRenderer"]["items"].array_items())
					if (j.has_key("compactPlaylistRenderer")) playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
				if (playlists.size()) new_result.playlists.push_back({category_name, playlists});
			}
			if (i.has_key("itemSectionRenderer")) {
				std::string category_name;
				for (auto j : tab["tabRenderer"]["content"]["sectionListRenderer"]["subMenu"]["channelSubMenuRenderer"]["contentTypeSubMenuItems"].array_items())
					category_name += j["title"].string_value();
				std::vector<YouTubePlaylistSuccinct> playlists;
				for (auto j : i["itemSectionRenderer"]["contents"].array_items())
					if (j.has_key("compactPlaylistRenderer")) playlists.push_back(convert_compact_playlist_renderer(j["compactPlaylistRenderer"]));
				// If the channel has no playlists, there's an itemSectionRenderer with only a messageRenderer in i["itemSectionRenderer"]["contents"]
				if (playlists.size()) new_result.playlists.push_back({category_name, playlists});
			}
		}
	}
	new_result.playlist_tab_browse_id = "";
	new_result.playlist_tab_params = "";
}
YouTubeChannelDetail youtube_channel_load_playlists(const YouTubeChannelDetail &prev_result) {
	YouTubeChannelDetail new_result = prev_result;
	
	if (innertube_key == "") fetch_innertube_key_and_player();
	if (innertube_key == "") new_result.error = "innertube key empty";
	if (prev_result.playlist_tab_browse_id == "") new_result.error = "playlist browse id empty";
	if (prev_result.playlist_tab_params == "") new_result.error = "playlist params empty";
	
	if (new_result.error != "") return new_result;
	
	
	std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "MWEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}}, "browseId": "%2", "params": "%3"})";
	post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
	post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
	post_content = std::regex_replace(post_content, std::regex("%2"), prev_result.playlist_tab_browse_id);
	post_content = std::regex_replace(post_content, std::regex("%3"), prev_result.playlist_tab_params);
	
	std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + innertube_key;
	
	access_and_parse_json(
		[&] () { return http_post_json(post_url, post_content); },
		[&] (Document &, RJson json) { channel_load_playlists_(json, new_result); },
		[&] (const std::string &error) {
			new_result.error = "[ch/pl] " + error;
			debug(new_result.error);
		}
	);
	
	return new_result;
}

static void load_community_items(RJson contents, YouTubeChannelDetail &res) {
	res.community_continuation_token = "";
	for (auto post : contents.array_items()) {
		if (post.has_key("backstagePostThreadRenderer")) {
			auto post_renderer = post["backstagePostThreadRenderer"]["post"]["backstagePostRenderer"];
			YouTubeChannelDetail::CommunityPost cur_post;
			cur_post.message = get_text_from_object(post_renderer["contentText"]);
			cur_post.author_name = get_text_from_object(post_renderer["authorText"]);
			{
				constexpr int target_height = 70;
				int min_distance = 100000;
				std::string best_icon;
				for (auto icon : post_renderer["authorThumbnail"]["thumbnails"].array_items()) {
					int cur_height = icon["height"].int_value();
					if (cur_height >= 256) continue; // too large
					if (min_distance > std::abs(target_height - cur_height)) {
						min_distance = std::abs(target_height - cur_height);
						best_icon = icon["url"].string_value();
					}
				}
				if (best_icon.substr(0, 2) == "//") best_icon = "https:" + best_icon;
				cur_post.author_icon_url = best_icon;
			}
			cur_post.time = get_text_from_object(post_renderer["publishedTimeText"]);
			cur_post.upvotes_str = get_text_from_object(post_renderer["voteCount"]);
			if (post_renderer["backstageAttachment"]["backstageImageRenderer"].is_valid()) {
				auto tmp = post_renderer["backstageAttachment"]["backstageImageRenderer"]["image"]["thumbnails"].array_items();
				if (tmp.size()) cur_post.image_url = tmp[0]["url"].string_value();
			}
			if (post_renderer["backstageAttachment"]["videoRenderer"].is_valid()) {
				auto video_renderer = post_renderer["backstageAttachment"]["videoRenderer"];
				std::string video_id = video_renderer["videoId"].string_value();
				YouTubeVideoSuccinct cur_video;
				cur_video.url = "https://m.youtube.com/watch?v=" + video_id;
				cur_video.title = get_text_from_object(video_renderer["title"]);
				cur_video.views_str = get_text_from_object(video_renderer["viewCountText"]);
				cur_video.author = get_text_from_object(video_renderer["ownerText"]);
				cur_video.thumbnail_url = "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
				cur_video.publish_date = get_text_from_object(video_renderer["publishedTimeText"]);
				cur_video.duration_text = get_text_from_object(video_renderer["lengthText"]);
				cur_post.video = cur_video;
			}
			if (post_renderer["backstageAttachment"]["pollRenderer"].is_valid()) {
				auto poll_renderer = post_renderer["backstageAttachment"]["pollRenderer"];
				cur_post.poll_total_votes = get_text_from_object(poll_renderer["totalVotes"]);
				for (auto choice : poll_renderer["choices"].array_items())
					cur_post.poll_choices.push_back(get_text_from_object(choice["text"]));
			}
			res.community_posts.push_back(cur_post);
		} else if (post.has_key("continuationItemRenderer")) {
			res.community_continuation_token = post["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"].string_value();
		}
	}
}

YouTubeChannelDetail youtube_channel_load_community(const YouTubeChannelDetail &prev_result) {
	auto new_result = prev_result;
	new_result.community_loaded = true;
	
	if (!prev_result.has_community_posts_to_load()) {
		new_result.error = "No community post to load";
		return new_result;
	}
	
	if (!prev_result.community_loaded) {
		// community post seems to be only available in the desktop version
		std::string url = convert_url_to_desktop(prev_result.url + "/community");
		std::string html = http_get(url, {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:94.0) Gecko/20100101 Firefox/94.0"}});
		if (!html.size()) {
			new_result.error = "failed to download community page";
			return new_result;
		}
		Document json_root;
		RJson initial_data = get_initial_data(json_root, html);
		
		RJson contents;
		for (auto tab : initial_data["contents"]["twoColumnBrowseResultsRenderer"]["tabs"].array_items())
			for (auto i : tab["tabRenderer"]["content"]["sectionListRenderer"]["contents"].array_items())
				contents = i["itemSectionRenderer"]["contents"];
		load_community_items(contents, new_result);
	} else {
		if (innertube_key == "") fetch_innertube_key_and_player();
		if (innertube_key == "") {
			new_result.error = "innertube key empty";
			return new_result;
		}
		std::string post_content = R"({"context": {"client": {"hl": "%0", "gl": "%1", "clientName": "WEB", "clientVersion": "2.20210711.08.00", "utcOffsetMinutes": 0}}, "continuation": "%2"})";
		post_content = std::regex_replace(post_content, std::regex("%0"), language_code);
		post_content = std::regex_replace(post_content, std::regex("%1"), country_code);
		post_content = std::regex_replace(post_content, std::regex("%2"), prev_result.community_continuation_token);
		
		std::string post_url = "https://m.youtube.com/youtubei/v1/browse?key=" + innertube_key;
		
		access_and_parse_json(
			[&] () { return http_post_json(post_url, post_content); },
			[&] (Document &, RJson yt_result) {
				RJson contents;
				for (auto i : yt_result["onResponseReceivedEndpoints"].array_items()) if (i.has_key("appendContinuationItemsAction"))
					contents = i["appendContinuationItemsAction"]["continuationItems"];
				load_community_items(contents, new_result);
			},
			[&] (const std::string &error) {
				new_result.error = "[ch/c+] " + error;
				debug(new_result.error);
			}
		);
	}
	
	return new_result;
}
