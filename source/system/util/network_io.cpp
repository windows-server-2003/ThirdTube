#include "headers.hpp"
#include <cassert>


NetworkStreamCacherData::NetworkStreamCacherData (std::string url) : url(url) { // the url is actually accessed in the downloader thread
	svcCreateMutex(&start_change_request_mutex, false);
	svcCreateMutex(&downloaded_data_mutex, false);
}
bool NetworkStreamCacherData::is_inited() { return inited; }
size_t NetworkStreamCacherData::get_length() { return length; }
bool NetworkStreamCacherData::bad() { return error; }

bool NetworkStreamCacherData::is_data_available(size_t start, size_t size) {
	if (!size) return true;
	
	svcWaitSynchronization(downloaded_data_mutex, std::numeric_limits<s64>::max());
	
	size_t end = start + size - 1;
	bool ok = true;
	for (size_t block = start / BLOCK_SIZE; block <= end / BLOCK_SIZE; block++) if (!downloaded_data.count(block)) {
		ok = false;
		break;
	}
	
	svcReleaseMutex(downloaded_data_mutex);
	
	return ok;
}
void NetworkStreamCacherData::set_download_start(size_t start) {
	svcWaitSynchronization(start_change_request_mutex, std::numeric_limits<s64>::max());
	
	start_change_request = start;
	
	svcReleaseMutex(start_change_request_mutex);
}
std::vector<u8> NetworkStreamCacherData::get_data(size_t start, size_t size) {
	if (!size) return {};
	std::vector<u8> res;
	
	svcWaitSynchronization(downloaded_data_mutex, std::numeric_limits<s64>::max());
	
	size_t end = start + size - 1;
	auto itr = downloaded_data.find(start / BLOCK_SIZE);
	assert(itr != downloaded_data.end());
	for (size_t block = start / BLOCK_SIZE; block <= end / BLOCK_SIZE; block++) {
		assert(itr->first == block);
		size_t cur_l = std::max(start, block * BLOCK_SIZE) - block * BLOCK_SIZE;
		size_t cur_r = std::min(end + 1, (block + 1) * BLOCK_SIZE) - block * BLOCK_SIZE;
		res.insert(res.end(), itr->second.begin() + cur_l, itr->second.begin() + cur_r);
		itr++;
	}
	
	svcReleaseMutex(downloaded_data_mutex);
	Util_log_save("dl/main", "get_data, returned data sz req:" + std::to_string(size) + " ac:" + std::to_string(res.size()));
	
	return res;
}
void NetworkStreamCacherData::exit() {
	should_be_running = false;
}





// return.first : error message, an empty string if the operation suceeded without an error
// return.second : the acquired http context, should neither be used nor closed if return.first isn't empty
static std::pair<std::string, httpcContext> access_http(std::string url, std::map<std::string, std::string> request_headers) {
	httpcContext context;
	
	u32 statuscode = 0;
	Result ret = 0;
	do {
		ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url.c_str(), 1);
		ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify); // to access https:// websites
		// ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
		// ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
		ret = httpcAddRequestHeaderField(&context, "User-Agent", "httpc-example/1.0.0");
		for (auto i : request_headers) httpcAddRequestHeaderField(&context, i.first.c_str(), i.second.c_str());

		ret = httpcBeginRequest(&context);
		if (ret != 0) {
			httpcCloseContext(&context);
			return {"httpcBeginRequest() failed : " + std::to_string(ret), context};
		}

		ret = httpcGetResponseStatusCode(&context, &statuscode);
		if (ret != 0) {
			httpcCloseContext(&context);
			return {"httpcGetResponseStatusCode() failed : " + std::to_string(ret), context};
		}

		if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
			char newurl[0x1000];
			ret = httpcGetResponseHeader(&context, "Location", newurl, 0x1000);
			Util_log_save("yay:", "redirect : " + std::string(newurl));
			url = std::string(newurl);
			httpcCloseContext(&context); // close this context before we try the next
		}
	} while ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308));

	if (statuscode / 100 != 2) { // allow any statuscode between 200 and 299
		httpcCloseContext(&context);
		return {"the website returned " + std::to_string(statuscode), context};
	}
	
	return {"", context};
}




#define LOG_THREAD_STR "dl thread"

void network_downloader_thread(void *cacher_) {
	NetworkStreamCacherData *cacher = (NetworkStreamCacherData *) cacher_;
	constexpr size_t BLOCK_SIZE = NetworkStreamCacherData::BLOCK_SIZE;
	
	const std::string url = cacher->url;
	httpcContext context;
	size_t cur_head_block = 0;
	size_t connection_head_block = 0;
	size_t block_num = 0;
	while (cacher->should_be_running) {
		if (!cacher->inited) {
			Util_log_save(LOG_THREAD_STR, "init begin");
			auto network_res = access_http(url, {});
			if (network_res.first == "") {
				context = network_res.second;
				{
					u32 tmp;
					httpcGetDownloadSizeState(&context, NULL, &tmp);
					cacher->length = tmp;
					Util_log_save(LOG_THREAD_STR, "init access ok : ", tmp);
				}
				block_num = (cacher->length + BLOCK_SIZE - 1) / BLOCK_SIZE;
				cacher->inited = true;
			} else {
				Util_log_save(LOG_THREAD_STR, network_res.first);
				cacher->error = true;
				break;
			}
		} else if (cacher->downloaded_data.size() == block_num) { // completed downloading the entire content
			Util_log_save(LOG_THREAD_STR, "entire content downloaded, exiting");
			httpcCancelConnection(&context);
			httpcCloseContext(&context);
			break;
		} else {
			// process the request by set_download_start()
			Util_log_save(LOG_THREAD_STR, "locking start change request...");
			svcWaitSynchronization(cacher->start_change_request_mutex, std::numeric_limits<s64>::max());
			Util_log_save(LOG_THREAD_STR, "lock acquired.");
			
			if (cacher->start_change_request != (size_t) -1) {
				cur_head_block = cacher->start_change_request / BLOCK_SIZE;
				cacher->start_change_request = (size_t) -1;
			}
			
			svcReleaseMutex(cacher->start_change_request_mutex);
			Util_log_save(LOG_THREAD_STR, "released.");
			
			// search the first uncached block
			while (cur_head_block < block_num && cacher->downloaded_data.count(cur_head_block)) cur_head_block++;
			if (cur_head_block == block_num) cur_head_block = 0;
			while (cur_head_block < block_num && cacher->downloaded_data.count(cur_head_block)) cur_head_block++;
			
			if (connection_head_block != cur_head_block) { // establish a new connection
				Util_log_save(LOG_THREAD_STR, "reconnect needed, discarding previous connection");
				httpcCancelConnection(&context);
				httpcCloseContext(&context);
				Util_log_save(LOG_THREAD_STR, "previous connection discarded");
				
				connection_head_block = cur_head_block;
				size_t start = cur_head_block * BLOCK_SIZE;
				auto network_res = access_http(url, {{"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(cacher->length - 1)}});
				if (network_res.first == "") context = network_res.second;
				else {
					Util_log_save(LOG_THREAD_STR, "reconnect failed : " + std::to_string(start) + "-" + std::to_string(cacher->length - 1));
					Util_log_save(LOG_THREAD_STR, "errmsg : " + network_res.first);
					cacher->error = true;
					break;
				}
			}
			size_t expected_len = std::min(BLOCK_SIZE, cacher->length - cur_head_block * BLOCK_SIZE);
			std::vector<u8> data(expected_len);
			u32 len_read;
			httpcDownloadData(&context, &data[0], expected_len, &len_read);
			if (len_read != expected_len)
				Util_log_save(LOG_THREAD_STR, "warining:size discrepancy : " + std::to_string(expected_len) + " -> " + std::to_string(len_read));
			
			Util_log_save(LOG_THREAD_STR, "locking on downloaded_data request...");
			svcWaitSynchronization(cacher->downloaded_data_mutex, std::numeric_limits<s64>::max());
			Util_log_save(LOG_THREAD_STR, "lock acquired.");
			
			cacher->downloaded_data[cur_head_block] = std::move(data);
			
			svcReleaseMutex(cacher->downloaded_data_mutex);
			Util_log_save(LOG_THREAD_STR, "released.");
			
			cur_head_block++;
			connection_head_block++;
		}
		
	}
	
	Util_log_save(LOG_THREAD_STR, "Thread exit.");
	threadExit(0);
}
