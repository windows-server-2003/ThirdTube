#include "headers.hpp"
#include <cassert>


NetworkStreamCacherData::NetworkStreamCacherData () { // the url is actually accessed in the downloader thread
	svcCreateMutex(&resource_locker, false);
}
void NetworkStreamCacherData::lock() {
	svcWaitSynchronization(resource_locker, std::numeric_limits<s64>::max());
}
void NetworkStreamCacherData::release() {
	svcReleaseMutex(resource_locker);
}
bool NetworkStreamCacherData::latest_inited() {
	lock();
	bool res = inited_once && !(url_change_resolving || unseen_change_request);
	release();
	return res;
}
std::string NetworkStreamCacherData::url() {
	return cur_inited_url; // assuming that latest_inited() is true, there's no need to lock
}
void NetworkStreamCacherData::change_url(std::string url) {
	lock();
	url_change_request = url;
	unseen_change_request = true;
	release();
}
bool NetworkStreamCacherData::has_error() { return error; }
size_t NetworkStreamCacherData::get_len() { return length; }
void NetworkStreamCacherData::set_download_start(size_t start) {
	lock();
	start_change_request = start;
	release();
}

bool NetworkStreamCacherData::is_data_available(size_t start, size_t size) {
	if (!size) return true;
	lock();
	
	size_t end = start + size - 1;
	bool ok = true;
	for (size_t block = start / BLOCK_SIZE; block <= end / BLOCK_SIZE; block++) if (!downloaded_data.count(block)) {
		ok = false;
		break;
	}
	
	release();
	
	return ok;
}
std::vector<u8> NetworkStreamCacherData::get_data(size_t start, size_t size) {
	if (!size) return {};
	std::vector<u8> res;
	
	lock();
	
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
	
	release();
	
	return res;
}
void NetworkStreamCacherData::exit() {
	should_be_running = false;
}





// return.first : error message, an empty string if the operation suceeded without an error
// return.second : the acquired http context, should neither be used nor closed if return.first isn't empty
std::pair<std::string, httpcContext> access_http(std::string url, std::map<std::string, std::string> request_headers) {
	httpcContext context;
	
	u32 statuscode = 0;
	Result ret = 0;
	do {
		ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url.c_str(), 0);
		ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify); // to access https:// websites
		ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
		ret = httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
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
			Util_log_save("httpc", "redirect");
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
	
	
	std::string cur_url = "";
	httpcContext context;
	bool context_alive = false;
	size_t block_num = 0;
	size_t cur_head_block = 0;
	size_t connection_head_block = 0;
	while (cacher->should_be_running) {
		// look for url change request
		int fix_cputime_limit = -1;
		cacher->lock();
		if (cacher->unseen_change_request) {
			cacher->unseen_change_request = false;
			cacher->url_change_resolving = true;
			cacher->cur_inited_url = cacher->url_change_request;
			cur_url = std::move(cacher->url_change_request);
			cacher->release();
			// init stuff
			cacher->error = false;
			cacher->downloaded_data = {};
			cacher->start_change_request = (size_t) -1;
			cacher->download_percent = 0;
			cur_head_block = 0;
			connection_head_block = 0;
			
			// clean up previous connection
			if (context_alive) {
				cacher->waiting_status = "cleaning up the previous network";
				httpcCancelConnection(&context);
				httpcCloseContext(&context);
				context_alive = false;
			}
			
			cacher->waiting_status = "accessing the stream";
			APT_SetAppCpuTimeLimit(25); // we apply an aggresive cputime limit to maximize the download speed
			fix_cputime_limit = 80; // revert it after downloading the first block
			auto network_res = access_http(cur_url, {});
			if (network_res.first == "") {
				context = network_res.second;
				{
					cacher->waiting_status = "getting stream size";
					u32 tmp;
					httpcGetDownloadSizeState(&context, NULL, &tmp);
					cacher->length = tmp;
				}
				block_num = (cacher->length + BLOCK_SIZE - 1) / BLOCK_SIZE;
				context_alive = true;
			} else {
				Util_log_save(LOG_THREAD_STR, network_res.first);
				cacher->error = true;
				continue;
			}
			
			cacher->lock();
			cacher->inited_once = true;
			cacher->url_change_resolving = false;
		}
		// seek request
		if (cacher->start_change_request != (size_t) -1) {
			cur_head_block = cacher->start_change_request / BLOCK_SIZE;
			cacher->start_change_request = (size_t) -1;
		}
		cacher->release();
		cacher->waiting_status = NULL;
		
		
		if (cacher->downloaded_data.size() < block_num) { // there's something to download
			while (cur_head_block < block_num && cacher->downloaded_data.count(cur_head_block)) cur_head_block++;
			if (cur_head_block == block_num) cur_head_block = 0;
			while (cur_head_block < block_num && cacher->downloaded_data.count(cur_head_block)) cur_head_block++;
			
			if (connection_head_block != cur_head_block) { // establish a new connection
				if (context_alive) {
					cacher->waiting_status = "cleaning up the previous network";
					httpcCancelConnection(&context);
					httpcCloseContext(&context);
					context_alive = false;
				}
				
				connection_head_block = cur_head_block;
				size_t start = cur_head_block * BLOCK_SIZE;
				cacher->waiting_status = "accessing the stream";
				APT_SetAppCpuTimeLimit(25); // we apply an aggresive cputime limit to maximize the download speed
				fix_cputime_limit = 80; // revert it after downloading the first block
				while (1) {
					auto network_res = access_http(cur_url, {{"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(cacher->length - 1)}});
					if (network_res.first == "") {
						context = network_res.second;
						context_alive = true;
						break;
					}
					Util_log_save(LOG_THREAD_STR, "reconnect failed : " + std::to_string(start) + "-" + std::to_string(cacher->length - 1));
					Util_log_save(LOG_THREAD_STR, "errmsg : " + network_res.first);
					Util_log_save(LOG_THREAD_STR, "trying again in 5 s");
					usleep(5000000);
				}
			}
			size_t expected_len = std::min(BLOCK_SIZE, cacher->length - cur_head_block * BLOCK_SIZE);
			cacher->waiting_status = "downloading the content of the stream";
			std::vector<u8> data(expected_len);
			u32 len_read;
			httpcDownloadData(&context, &data[0], expected_len, &len_read);
			if (len_read != expected_len)
				Util_log_save(LOG_THREAD_STR, "warining:size discrepancy : " + std::to_string(expected_len) + " -> " + std::to_string(len_read));
			if (fix_cputime_limit != -1) APT_SetAppCpuTimeLimit(fix_cputime_limit);
			
			cacher->lock();
			cacher->downloaded_data[cur_head_block] = std::move(data);
			cacher->release();
			
			cur_head_block++;
			connection_head_block++;
			
			cacher->download_percent = cacher->downloaded_data.size() * 100 / block_num;
			cacher->waiting_status = NULL;
		} else usleep(DEF_INACTIVE_THREAD_SLEEP_TIME);
		
	}
	
	if (context_alive) {
		httpcCancelConnection(&context);
		httpcCloseContext(&context);
		context_alive = false;
	}
	
	
	Util_log_save(LOG_THREAD_STR, "Thread exit.");
	threadExit(0);
}
