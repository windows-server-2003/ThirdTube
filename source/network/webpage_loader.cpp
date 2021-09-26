#include "headers.hpp"
#include "network/webpage_loader.hpp"

static Handle resource_lock;
static bool resource_lock_initialized = false;
static void *requests[100] = { NULL };
static bool in_progress[100] = { false };

static void lock() {
	if (!resource_lock_initialized) {
		svcCreateMutex(&resource_lock, false);
		resource_lock_initialized = true;
	}
	svcWaitSynchronization(resource_lock, std::numeric_limits<s64>::max());
}
static void release() {
	svcReleaseMutex(resource_lock);
}

static void delete_request(LoadRequestType request_type) {
	if (request_type == LoadRequestType::CHANNEL || request_type == LoadRequestType::CHANNEL_CONTINUE) {
		delete ((ChannelLoadRequestArg *) requests[(int) request_type]);
	}
	// add here
	requests[(int) request_type] = NULL;
}

bool request_webpage_loading(LoadRequestType request_type, void *arg) {
	lock();
	bool res;
	if (!requests[(int) request_type]) requests[(int) request_type] = arg, res = true;
	else res = false;
	release();
	return res;
}

void cancel_webpage_loading(LoadRequestType request_type) {
	lock();
	if (requests[(int) request_type] && !in_progress[(int) request_type]) delete_request(request_type);
	release();
}

bool is_webpage_loading_requested(LoadRequestType request_type) {
	return requests[(int) request_type] != NULL;
}


/*
	The functions below here run in webpage loader thread
	--------------------------------------------------------------------------------
	--------------------------------------------------------------------------------
	--------------------------------------------------------------------------------
*/

static void load_search_channel(ChannelLoadRequestArg arg) {
	add_cpu_limit(25);
	YouTubeChannelDetail new_result = youtube_parse_channel_page(arg.url);
	remove_cpu_limit(25);
	
	// wrap and truncate here
	Util_log_save("wloader/channel", "truncate start");
	std::vector<std::vector<std::string> > new_wrapped_titles(new_result.videos.size());
	for (size_t i = 0; i < new_result.videos.size(); i++)
		new_wrapped_titles[i] = truncate_str(new_result.videos[i].title, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
	Util_log_save("wloader/channel", "truncate end");
	
	
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	*arg.result = new_result;
	*arg.wrapped_titles = new_wrapped_titles;
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
}
static void load_search_channel_continue(ChannelLoadRequestArg arg) {
	lock();
	if (requests[(int) LoadRequestType::CHANNEL]) {
		release();
		return;
	}
	auto prev_result = *arg.result;
	release();
	
	auto new_result = youtube_channel_page_continue(prev_result);
	
	Util_log_save("wloader/channel-c", "truncate start");
	std::vector<std::vector<std::string> > wrapped_titles_add(new_result.videos.size() - prev_result.videos.size());
	for (size_t i = prev_result.videos.size(); i < new_result.videos.size(); i++)
		wrapped_titles_add[i - prev_result.videos.size()] = truncate_str(new_result.videos[i].title, arg.max_width, 2, arg.text_size_x, arg.text_size_y);
	Util_log_save("wloader/channel-c", "truncate end");
	
	
	lock();
	if (requests[(int) LoadRequestType::CHANNEL]) {
		release();
		return;
	}
	svcWaitSynchronization(arg.lock, std::numeric_limits<s64>::max());
	if (new_result.error != "") arg.result->error = new_result.error;
	else {
		*arg.result = new_result;
		arg.wrapped_titles->insert(arg.wrapped_titles->end(), wrapped_titles_add.begin(), wrapped_titles_add.end());
	}
	if (arg.on_load_complete) arg.on_load_complete();
	svcReleaseMutex(arg.lock);
	release();
}


static bool should_be_running = true;
void webpage_loader_thread_exit_request() { should_be_running = false; }
void webpage_loader_thread_func(void* arg) {
	(void) arg;
	
	while (should_be_running) {
		lock();
		LoadRequestType next_type = LoadRequestType::NONE;
		for (int i = 0; i < 7; i++) if (requests[i]) next_type = (LoadRequestType) i;
		
		if (next_type != LoadRequestType::NONE) in_progress[(int) next_type] = true;
		release();
		if (next_type == LoadRequestType::NONE) {
			usleep(50000);
			continue;
		}
		
		if (next_type == LoadRequestType::CHANNEL) load_search_channel(*((ChannelLoadRequestArg *) requests[(int) LoadRequestType::CHANNEL]));
		if (next_type == LoadRequestType::CHANNEL_CONTINUE) load_search_channel_continue(*((ChannelLoadRequestArg *) requests[(int) LoadRequestType::CHANNEL_CONTINUE]));
		// add here
		
		lock();
		in_progress[(int) next_type] = false;
		delete_request(next_type);
		release();
	}
	
	threadExit(0);
}
