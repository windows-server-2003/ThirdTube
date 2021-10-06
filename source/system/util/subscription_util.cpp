#include "headers.hpp"
#include "system/util/subscription_util.hpp"
#include "system/util/util.hpp"
#include "ui/ui.hpp"
#include "json11/json11.hpp"

using namespace json11;

static std::vector<SubscriptionChannel> subscribed_channels;
static bool lock_initialized = false;
static Handle resource_lock;

static void lock() {
	if (!lock_initialized) {
		lock_initialized = true;
		svcCreateMutex(&resource_lock, false);
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}

#define SUBSCRIPTION_VERSION 0

void load_subscription() {
	const int BUF_SIZE = 0x100000;
	char *buf = (char *) malloc(BUF_SIZE + 1);
	
	u32 read_size;
	Result_with_string result = Util_file_load_from_file("subscription.json", DEF_MAIN_DIR, (u8 *) buf, BUF_SIZE, &read_size);
	Util_log_save("subsc/load" , "Util_file_load_from_file()..." + result.string + result.error_description, result.code);
	if (result.code == 0) {
		buf[read_size] = '\0';
		
		std::string error;
		Json data = Json::parse(buf, error);
		int version = data["version"] == Json() ? -1 : data["version"].int_value();
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
			lock();
			subscribed_channels = loaded_channels;
			release();
			Util_log_save("subsc/load" , "loaded subsc(" + std::to_string(subscribed_channels.size()) + " items)");
		} else {
			Util_log_save("subsc/load" , "failed to load subsc, json err:" + error);
		}
	}
	free(buf);
}
void save_subscription() {
	lock();
	auto channels_backup = subscribed_channels;
	release();
	
	std::string data = 
		std::string() +
		"{\n" + 
			"\t\"version\": " + std::to_string(SUBSCRIPTION_VERSION) + ",\n" +
			"\t\"channels\": [\n";
	
	bool first = true;
	for (auto channel : channels_backup) {
		if (first) first = false;
		else data += ",";
		data += 
			std::string() +
			"\t\t{\n" +
				"\t\t\t\"id\": \"" + channel.id + "\",\n" +
				"\t\t\t\"url\": \"" + channel.url + "\",\n" +
				"\t\t\t\"icon_url\": \"" + channel.icon_url + "\",\n" +
				"\t\t\t\"name\": \"" + channel.name + "\",\n" +
				"\t\t\t\"subscriber_count_str\": \"" + channel.subscriber_count_str + "\"\n" +
			"\t\t}";
	}
	data += "\n";
	data += "\t]\n";
	data += "}\n";
	
	Result_with_string result = Util_file_save_to_file("subscription.json", DEF_MAIN_DIR, (u8 *) data.c_str(), data.size(), true);
	Util_log_save("subsc/save", "Util_file_save_to_file()..." + result.string + result.error_description, result.code);
}


bool subscription_is_subscribed(const std::string &id) {
	lock();
	bool found = false;
	for (auto channel : subscribed_channels) if (channel.id == id) {
		found = true;
		break;
	}
	release();
	return found;
}

void subscription_subscribe(const SubscriptionChannel &new_channel) {
	lock();
	bool found = false;
	for (auto channel : subscribed_channels) if (channel.id == new_channel.id) {
		found = true;
		break;
	}
	if (!found) subscribed_channels.push_back(new_channel);
	release();
}
void subscription_unsubscribe(const std::string &id) {
	lock();
	std::vector<SubscriptionChannel> new_subscribed_channels;
	for (auto channel : subscribed_channels) if (channel.id != id) new_subscribed_channels.push_back(channel);
	subscribed_channels = new_subscribed_channels;
	release();
}

std::vector<SubscriptionChannel> get_subscribed_channels() {
	lock();
	auto res = subscribed_channels;
	release();
	return res;
}

