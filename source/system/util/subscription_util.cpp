#include "headers.hpp"
#include "system/util/subscription_util.hpp"
#include "system/util/util.hpp"
#include "ui/ui.hpp"
#include "rapidjson_wrapper.hpp"

using namespace rapidjson;

static std::vector<SubscriptionChannel> subscribed_channels;
static Mutex resource_lock;

#define SUBSCRIPTION_VERSION 0
#define SUBSCRIPTION_FILE_NAME "subscription.json"

void load_subscription() {
	u64 file_size;
	Result_with_string result = Util_file_check_file_size(SUBSCRIPTION_FILE_NAME, DEF_MAIN_DIR, &file_size);
	if (result.code != 0) {
		Util_log_save("subsc/load" , "Util_file_check_file_size()..." + result.string + result.error_description, result.code);
		return;
	}
	
	char *buf = (char *) malloc(file_size + 1);
	
	u32 read_size;
	result = Util_file_load_from_file(SUBSCRIPTION_FILE_NAME, DEF_MAIN_DIR, (u8 *) buf, file_size, &read_size);
	Util_log_save("subsc/load" , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	if (result.code == 0) {
		buf[read_size] = '\0';
		
		Document json_root;
		std::string error;
		RJson data = RJson::parse(json_root, buf, error);
		
		int version = data.has_key("version") ? data["version"].int_value() : -1;
		
		if (version >= 0) {
			std::vector<SubscriptionChannel> loaded_channels;
			for (auto video : data["channels"].array_items()) {
				SubscriptionChannel cur_channel;
				cur_channel.id = video["id"].string_value();
				cur_channel.url = video["url"].string_value();
				cur_channel.icon_url = video["icon_url"].string_value();
				cur_channel.name = video["name"].string_value();
				cur_channel.subscriber_count_str = video["subscriber_count_str"].string_value();
				
				bool valid = is_youtube_url(cur_channel.url) && is_youtube_thumbnail_url(cur_channel.icon_url);
				if (!valid) Util_log_save("subsc/load", "invalid channel, ignoring...");
				else loaded_channels.push_back(cur_channel);
			}
			std::sort(loaded_channels.begin(), loaded_channels.end(), [] (const auto &i, const auto &j) { return i.name < j.name; });
			resource_lock.lock();
			subscribed_channels = loaded_channels;
			resource_lock.unlock();
			Util_log_save("subsc/load" , "loaded subsc(" + std::to_string(subscribed_channels.size()) + " items)");
		} else Util_log_save("subsc/load" , "json err: " + error);
	}
	free(buf);
}
void save_subscription() {
	resource_lock.lock();
	auto channels_backup = subscribed_channels;
	resource_lock.unlock();
	
	Document json_root;
	auto &allocator = json_root.GetAllocator();
	
	json_root.SetObject();
	json_root.AddMember("version", std::to_string(SUBSCRIPTION_VERSION), allocator);
	
	Value channels(kArrayType);
	for (auto channel : channels_backup) {
		Value cur_json(kObjectType);
		cur_json.AddMember("id", channel.id, allocator);
		cur_json.AddMember("url", channel.url, allocator);
		cur_json.AddMember("icon_url", channel.icon_url, allocator);
		cur_json.AddMember("name", channel.name, allocator);
		cur_json.AddMember("subscriber_count_str", channel.subscriber_count_str, allocator);
		channels.PushBack(cur_json, allocator);
	}
	json_root.AddMember("channels", channels, allocator);
	
	std::string data = RJson(json_root).dump();
	
	Result_with_string result = Util_file_save_to_file(SUBSCRIPTION_FILE_NAME, DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	Util_log_save("subsc/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}


bool subscription_is_subscribed(const std::string &id) {
	resource_lock.lock();
	bool found = false;
	for (auto channel : subscribed_channels) if (channel.id == id) {
		found = true;
		break;
	}
	resource_lock.unlock();
	return found;
}

void subscription_subscribe(const SubscriptionChannel &new_channel) {
	resource_lock.lock();
	bool found = false;
	for (auto channel : subscribed_channels) if (channel.id == new_channel.id) {
		found = true;
		break;
	}
	if (!found) subscribed_channels.push_back(new_channel);
	std::sort(subscribed_channels.begin(), subscribed_channels.end(), [] (const auto &i, const auto &j) { return i.name < j.name; });
	resource_lock.unlock();
}
void subscription_unsubscribe(const std::string &id) {
	resource_lock.lock();
	std::vector<SubscriptionChannel> new_subscribed_channels;
	for (auto channel : subscribed_channels) if (channel.id != id) new_subscribed_channels.push_back(channel);
	subscribed_channels = new_subscribed_channels;
	std::sort(subscribed_channels.begin(), subscribed_channels.end(), [] (const auto &i, const auto &j) { return i.name < j.name; });
	resource_lock.unlock();
}

std::vector<SubscriptionChannel> get_subscribed_channels() {
	resource_lock.lock();
	auto res = subscribed_channels;
	resource_lock.unlock();
	return res;
}

