#include "headers.hpp"
#include <cassert>


// --------------------------------
// NetworkStream implementation
// --------------------------------

NetworkStream::NetworkStream(std::string url, size_t len) : url(url), len(len) {
	block_num = (len + BLOCK_SIZE - 1) / BLOCK_SIZE;
	svcCreateMutex(&downloaded_data_lock, false);
}
bool NetworkStream::is_data_available(size_t start, size_t size) {
	if (start + size > len) return false;
	size_t end = start + size - 1;
	size_t start_block = start / BLOCK_SIZE;
	size_t end_block = end / BLOCK_SIZE;
	
	bool res = true;
	svcWaitSynchronization(downloaded_data_lock, std::numeric_limits<s64>::max());
	for (size_t block = start_block; block <= end_block; block++) if (!downloaded_data.count(block)) {
		res = false;
		break;
	}
	svcReleaseMutex(downloaded_data_lock);
	return res;
}
std::vector<u8> NetworkStream::get_data(size_t start, size_t size) {
	if (!size) return {};
	size_t end = start + size - 1;
	size_t start_block = start / BLOCK_SIZE;
	size_t end_block = end / BLOCK_SIZE;
	std::vector<u8> res;
	
	svcWaitSynchronization(downloaded_data_lock, std::numeric_limits<s64>::max());
	auto itr = downloaded_data.find(start_block);
	assert(itr != downloaded_data.end());
	for (size_t block = start_block; block <= end_block; block++) {
		assert(itr->first == block);
		size_t cur_l = std::max(start, block * BLOCK_SIZE) - block * BLOCK_SIZE;
		size_t cur_r = std::min(end + 1, (block + 1) * BLOCK_SIZE) - block * BLOCK_SIZE;
		res.insert(res.end(), itr->second.begin() + cur_l, itr->second.begin() + cur_r);
		itr++;
	}
	svcReleaseMutex(downloaded_data_lock);
	return res;
}
void NetworkStream::set_data(size_t block, const std::vector<u8> &data) {
	svcWaitSynchronization(downloaded_data_lock, std::numeric_limits<s64>::max());
	downloaded_data[block] = data;
	svcReleaseMutex(downloaded_data_lock);
}
double NetworkStream::get_download_percentage() {
	svcWaitSynchronization(downloaded_data_lock, std::numeric_limits<s64>::max());
	double res = (double) downloaded_data.size() * BLOCK_SIZE / len * 100;
	svcReleaseMutex(downloaded_data_lock);
	return res;
}
std::vector<double> NetworkStream::get_download_percentage_list(size_t res_len) {
	svcWaitSynchronization(downloaded_data_lock, std::numeric_limits<s64>::max());
	std::vector<double> res(res_len);
	auto itr = downloaded_data.begin();
	for (size_t i = 0; i < res_len; i++) {
		size_t l = (u64) len * i / res_len;
		size_t r = std::min<u32>(len, (u64) len * (i + 1) / res_len);
		while (itr != downloaded_data.end()) {
			size_t il = itr->first * BLOCK_SIZE;
			size_t ir = std::min((itr->first + 1) * BLOCK_SIZE, len);
			if (ir <= l) itr++;
			else if (il >= r) break;
			else {
				res[i] += std::min(ir, r) - std::max(il, l);
				if (ir >= r) break;
				else itr++;
			}
		}
		res[i] /= r - l;
		res[i] *= 100;
	}
	svcReleaseMutex(downloaded_data_lock);
	return res;
}


// --------------------------------
// NetworkStreamDownloader implementation
// --------------------------------

NetworkStreamDownloader::NetworkStreamDownloader() {
	svcCreateMutex(&streams_lock, false);
}
size_t NetworkStreamDownloader::add_stream(NetworkStream *stream) {
	svcWaitSynchronization(streams_lock, std::numeric_limits<s64>::max());
	streams.push_back(stream);
	svcReleaseMutex(streams_lock);
	return streams.size() - 1;
}


#define LOG_THREAD_STR "net/dl"
void NetworkStreamDownloader::downloader_thread() {
	while (!thread_exit_reqeusted) {
		size_t cur_stream_index = (size_t) -1; // the index of the stream on which we will perform a download in this loop
		svcWaitSynchronization(streams_lock, std::numeric_limits<s64>::max());
		// back up 'read_head's as those can be changed from another thread
		std::vector<size_t> read_heads(streams.size());
		for (size_t i = 0; i < streams.size(); i++) read_heads[i] = streams[i]->read_head;
		
		
		// find the stream to download next
		double margin_percentage_min = 1000;
		for (size_t i = 0; i < streams.size(); i++) {
			if (streams[i]->error) continue;
			if (streams[i]->suspend_request) continue;
			if (streams[i]->quit_request) continue;
			
			size_t read_head_block = read_heads[i] / BLOCK_SIZE;
			size_t first_not_downloaded_block = read_head_block;
			while (first_not_downloaded_block < streams[i]->block_num && streams[i]->downloaded_data.count(first_not_downloaded_block)) {
				first_not_downloaded_block++;
				if (first_not_downloaded_block == read_head_block + MAX_FORWARD_READ_BLOCKS) break;
			}
			if (first_not_downloaded_block == streams[i]->block_num) continue; // no need to download this stream for now
			
			if (first_not_downloaded_block == read_head_block + MAX_FORWARD_READ_BLOCKS) continue; // no need to download this stream for now
			
			double margin_percentage;
			if (first_not_downloaded_block == read_head_block) margin_percentage = 0;
			else margin_percentage = (double) (first_not_downloaded_block * BLOCK_SIZE - read_heads[i]) / streams[i]->len * 100;
			// Util_log_save("net/dl", std::to_string(margin_percentage_min) + " " + std::to_string(margin_percentage) + " " + std::to_string(streams[i]->len) + " " + std::to_string(first_not_downloaded_block));
			if (margin_percentage_min > margin_percentage) {
				margin_percentage_min = margin_percentage;
				cur_stream_index = i;
			}
		}
		
		if (cur_stream_index == (size_t) -1) {
			svcReleaseMutex(streams_lock);
			usleep(50000);
			continue;
		}
		Util_log_save("net/dl", "dl next : " + std::to_string(cur_stream_index) + " " + std::to_string(margin_percentage_min));
		NetworkStream *cur_stream = streams[cur_stream_index];
		svcReleaseMutex(streams_lock);
		
		size_t block_reading = read_heads[cur_stream_index] / BLOCK_SIZE;
		while (block_reading < cur_stream->block_num && cur_stream->downloaded_data.count(block_reading)) block_reading++;
		if (block_reading == cur_stream->block_num) { // something unexpected happened
			Util_log_save(LOG_THREAD_STR, "unexpected error (trying to read beyond the end of the stream)");
			cur_stream->error = true;
			continue;
		}
		
		size_t start = block_reading * BLOCK_SIZE;
		size_t end = std::min((block_reading + 1) * BLOCK_SIZE, cur_stream->len);
		size_t expected_len = end - start;
		auto network_result = access_http(cur_stream->url, {{"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(end - 1)}});
		
		if (network_result.first == "") {
			auto context = network_result.second;
			std::vector<u8> data(expected_len);
			u32 len_read;
				Util_log_save("net/dl", "dl");
			httpcDownloadData(&context, &data[0], expected_len, &len_read);
				Util_log_save("net/dl", "dl ok");
			if (len_read != expected_len) {
				Util_log_save(LOG_THREAD_STR, "size discrepancy : " + std::to_string(expected_len) + " -> " + std::to_string(len_read));
				cur_stream->error = true;
				httpcCloseContext(&context);
				continue;
			}
			cur_stream->set_data(block_reading, data);
			httpcCloseContext(&context);
		} else {
			Util_log_save("net/dl", "access failed : " + network_result.first);
			cur_stream->error = true;
		}
	}
	Util_log_save(LOG_THREAD_STR, "Exit, deiniting...");
	for (auto stream : streams) {
		stream->quit_request = true;
	}
}
void NetworkStreamDownloader::delete_all() {
	for (auto stream : streams) delete stream;
	streams.clear();
}


// --------------------------------
// other functions implementation
// --------------------------------

void network_downloader_thread(void *downloader_) {
	NetworkStreamDownloader *downloader = (NetworkStreamDownloader *) downloader_;
	
	downloader->downloader_thread();
	
	threadExit(0);
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
		if (!request_headers.count("User-Agent")) ret = httpcAddRequestHeaderField(&context, "User-Agent", "httpc-example/1.0.0");
		for (auto i : request_headers) ret = httpcAddRequestHeaderField(&context, i.first.c_str(), i.second.c_str());

	Util_log_save("http", "begin req");
		ret = httpcBeginRequest(&context);
	Util_log_save("http", "begin req ok");
		if (ret != 0) {
			httpcCloseContext(&context);
			return {"httpcBeginRequest() failed : " + std::to_string(ret), context};
		}

	Util_log_save("http", "get st");
		ret = httpcGetResponseStatusCode(&context, &statuscode);
	Util_log_save("http", "get st ok");
		if (ret != 0) {
			httpcCloseContext(&context);
			return {"httpcGetResponseStatusCode() failed : " + std::to_string(ret), context};
		}

		if ((statuscode >= 301 && statuscode <= 303) || (statuscode >= 307 && statuscode <= 308)) {
	Util_log_save("http", "redir");
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



