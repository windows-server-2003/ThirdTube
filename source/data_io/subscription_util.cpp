#include "headers.hpp"
#include "subscription_util.hpp"
#include "util/util.hpp"
#include "ui/ui.hpp"
#include "rapidjson_wrapper.hpp"

using namespace rapidjson;

static std::vector<SubscriptionChannel> subscribed_channels;
static Mutex resource_lock;

#define SUBSCRIPTION_VERSION 0
#define SUBSCRIPTION_FILE_PATH (DEF_MAIN_DIR + "subscription.json")
#define SUBSCRIPTION_FILE_TMP_PATH (DEF_MAIN_DIR + "subscription_tmp.json")

static AtomicFileIO atomic_io(SUBSCRIPTION_FILE_PATH, SUBSCRIPTION_FILE_TMP_PATH);

static bool is_valid_subscription_channel(const SubscriptionChannel &channel) {
	return is_youtube_url(channel.url) && is_youtube_thumbnail_url(channel.icon_url);
}

void load_subscription() {
	auto tmp = atomic_io.load([] (const std::string &data) {
		Document json_root;
		std::string error;
		RJson data_json = RJson::parse(json_root, data.c_str(), error);
		
		int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
		return version >= 0;
	});
	Result_with_string result = tmp.first;
	std::string data = tmp.second;
	if (result.code != 0) {
		logger.error("subsc/load", result.string + result.error_description + " " + std::to_string(result.code));
		return;
	}
	
	Document json_root;
	std::string error;
	RJson data_json = RJson::parse(json_root, data.c_str(), error);
	
	int version = data_json.has_key("version") ? data_json["version"].int_value() : -1;
	
	std::vector<SubscriptionChannel> loaded_channels;
	if (version >= 0) {
		for (auto video : data_json["channels"].array_items()) {
			SubscriptionChannel cur_channel;
			cur_channel.id = video["id"].string_value();
			cur_channel.url = video["url"].string_value();
			cur_channel.icon_url = video["icon_url"].string_value();
			cur_channel.name = video["name"].string_value();
			cur_channel.subscriber_count_str = video["subscriber_count_str"].string_value();
			// "invalid" channels will not be shown in the subscription list but will still be kept in the subscription file
			cur_channel.valid = is_valid_subscription_channel(cur_channel);
			if (!cur_channel.valid) logger.caution("subsc/load", "invalid channel : " + cur_channel.name);
			
			loaded_channels.push_back(cur_channel);
		}
		std::sort(loaded_channels.begin(), loaded_channels.end(), [] (const auto &i, const auto &j) { return i.name < j.name; });
	} else {
		logger.error("subsc/load", "json err : " + data.substr(0, 40));
		return;
	}
	
	resource_lock.lock();
	subscribed_channels = loaded_channels;
	resource_lock.unlock();
	logger.info("subsc/load" , "loaded subsc(" + std::to_string(loaded_channels.size()) + " items)");
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
	
	auto result = atomic_io.save(data);
	if (result.code != 0) logger.warning("subsc/save", result.string + result.error_description, result.code);
	else logger.info("subsc/save", "subscription saved.");
}


bool subscription_is_subscribed(const std::string &id) {
	resource_lock.lock();
	bool found = false;
	for (auto channel : subscribed_channels) if (channel.valid && channel.id == id) {
		found = true;
		break;
	}
	resource_lock.unlock();
	return found;
}

void subscription_subscribe(const SubscriptionChannel &new_channel) {
	resource_lock.lock();
	bool found = false;
	for (auto &channel : subscribed_channels) if (channel.id == new_channel.id) {
		found = true;
		channel = new_channel;
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
	resource_lock.unlock();
}

std::vector<SubscriptionChannel> get_valid_subscribed_channels() {
	resource_lock.lock();
	std::vector<SubscriptionChannel> res;
	for (auto &channel : subscribed_channels) if (channel.valid) res.push_back(channel);
	resource_lock.unlock();
	return res;
}

