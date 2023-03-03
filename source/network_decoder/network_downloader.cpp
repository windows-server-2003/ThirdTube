#include "headers.hpp"
#include "network_downloader.hpp"
#include "network_io.hpp"

#define MAX_CACHE_BLOCKS (var_is_new3ds ? NetworkStream::NEW3DS_MAX_CACHE_BLOCKS : NetworkStream::OLD3DS_MAX_CACHE_BLOCKS)


// --------------------------------
// NetworkStream implementation
// --------------------------------

bool NetworkStream::is_data_available(u64 start, u64 size) {
	if (!ready) return false;
	if (start + size > len) return false;
	u64 end = start + size - 1;
	u64 start_block = start / BLOCK_SIZE;
	u64 end_block = end / BLOCK_SIZE;
	
	bool res = true;
	downloaded_data_lock.lock();
	for (u64 block = start_block; block <= end_block; block++) if (!downloaded_data.count(block)) {
		res = false;
		break;
	}
	downloaded_data_lock.unlock();
	return res;
}
std::vector<u8> NetworkStream::get_data(u64 start, u64 size) {
	if (!ready) return {};
	if (!size) return {};
	u64 end = start + size - 1;
	u64 start_block = start / BLOCK_SIZE;
	u64 end_block = end / BLOCK_SIZE;
	std::vector<u8> res;
	
	downloaded_data_lock.lock();
	auto itr = downloaded_data.find(start_block);
	my_assert(itr != downloaded_data.end());
	for (u64 block = start_block; block <= end_block; block++) {
		my_assert(itr->first == block);
		u64 cur_l = std::max(start, block * BLOCK_SIZE) - block * BLOCK_SIZE;
		u64 cur_r = std::min(end + 1, (block + 1) * BLOCK_SIZE) - block * BLOCK_SIZE;
		res.insert(res.end(), itr->second.begin() + cur_l, itr->second.begin() + cur_r);
		itr++;
	}
	downloaded_data_lock.unlock();
	return res;
}
void NetworkStream::set_data(u64 block, const std::vector<u8> &data) {
	downloaded_data_lock.lock();
	downloaded_data[block] = data;
	if (downloaded_data.size() > MAX_CACHE_BLOCKS) { // ensure it doesn't cache too much and run out of memory
		u64 read_head_block = read_head / BLOCK_SIZE;
		if (std::next(downloaded_data.begin())->first < read_head_block) downloaded_data.erase(std::next(downloaded_data.begin()));
		else downloaded_data.erase(std::prev(downloaded_data.end()));
	}
	downloaded_data_lock.unlock();
}
double NetworkStream::get_download_percentage() {
	downloaded_data_lock.lock();
	double res = (double) downloaded_data.size() * BLOCK_SIZE / len * 100;
	downloaded_data_lock.unlock();
	return res;
}
std::vector<double> NetworkStream::get_buffering_progress_bar(int res_len) {
	downloaded_data_lock.lock();
	std::vector<double> res(res_len);
	auto itr = downloaded_data.begin();
	for (int i = 0; i < res_len; i++) {
		u64 l = (u64) len * i / res_len;
		u64 r = std::min<u64>(len, len * (i + 1) / res_len);
		while (itr != downloaded_data.end()) {
			u64 il = itr->first * BLOCK_SIZE;
			u64 ir = std::min((itr->first + 1) * BLOCK_SIZE, len);
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
	downloaded_data_lock.unlock();
	return res;
}


// --------------------------------
// NetworkStreamDownloader implementation
// --------------------------------

void NetworkStreamDownloader::add_stream(NetworkStream *stream) {
	streams_lock.lock();
	size_t index = (size_t) -1;
	for (size_t i = 0; i < streams.size(); i++) if (!streams[i]) {
		streams[i] = stream;
		index = i;
		break;
	}
	if (index == (size_t) -1) {
		index = streams.size();
		streams.push_back(stream);
	}
	streams_lock.unlock();
}

static bool thread_network_session_list_inited = false;
static NetworkSessionList thread_network_session_list;
static void confirm_thread_network_session_list_inited() {
	if (!thread_network_session_list_inited) {
		thread_network_session_list_inited = true;
		thread_network_session_list.init();
	}
}

static std::string remove_url_parameter(const std::string &url, const std::string &param) {
	std::string res;
	for (size_t i = 0; i < url.size(); ) {
		if (url[i] == '&' || url[i] == '?') {
			if (url.substr(i + 1, param.size() + 1) == param + "=") {
				i = std::find(url.begin() + i + 1, url.end(), '&') - url.begin();
			} else res.push_back(url[i++]);
		} else res.push_back(url[i++]);
	}
	return res;
}

#define LOG_THREAD_STR "net/dl"
void NetworkStreamDownloader::downloader_thread() {
	while (!thread_exit_reqeusted) {
		size_t cur_stream_index = (size_t) -1; // the index of the stream on which we will perform a download in this loop
		streams_lock.lock();
		// back up 'read_head's as those can be changed from another thread
		std::vector<u64> read_heads(streams.size());
		for (size_t i = 0; i < streams.size(); i++) if (streams[i]) read_heads[i] = streams[i]->read_head;
		
		
		// find the stream to download next
		double margin_percentage_min = 1000;
		for (size_t i = 0; i < streams.size(); i++) {
			if (!streams[i]) continue;
			if (streams[i]->quit_request) {
				delete streams[i];
				streams[i] = NULL;
				continue;
			}
			if (streams[i]->error) continue;
			if (streams[i]->suspend_request) continue;
			if (!streams[i]->ready) {
				cur_stream_index = i;
				break;
			}
			if (streams[i]->whole_download) continue; // its entire content should already be downloaded
			
			int forward_buffer_block_num = std::max<int>(2, (MAX_CACHE_BLOCKS - 1) * var_forward_buffer_ratio); // block #0 is always kept
			u64 read_head_block = read_heads[i] / BLOCK_SIZE;
			u64 first_not_downloaded_block = read_head_block;
			while (first_not_downloaded_block < streams[i]->block_num && streams[i]->downloaded_data.count(first_not_downloaded_block)) {
				first_not_downloaded_block++;
				if (first_not_downloaded_block == read_head_block + forward_buffer_block_num) break;
			}
			if (first_not_downloaded_block == streams[i]->block_num) continue;
			if (first_not_downloaded_block == read_head_block + forward_buffer_block_num) continue; // no need to download this stream for now
			
			double margin_percentage;
			if (first_not_downloaded_block == read_head_block) margin_percentage = 0;
			else margin_percentage = (double) (first_not_downloaded_block * BLOCK_SIZE - read_heads[i]) / streams[i]->len * 100;
			if (margin_percentage_min > margin_percentage) {
				margin_percentage_min = margin_percentage;
				cur_stream_index = i;
			}
		}
		
		if (cur_stream_index == (size_t) -1) {
			streams_lock.unlock();
			usleep(20000);
			continue;
		}
		NetworkStream *cur_stream = streams[cur_stream_index];
		streams_lock.unlock();
		
		confirm_thread_network_session_list_inited();
		// whole download
		if (cur_stream->whole_download) {
			auto &session_list = cur_stream->session_list ? *cur_stream->session_list : thread_network_session_list;
			auto result = session_list.perform(HttpRequest::GET(cur_stream->url, {}));
			if (result.redirected_url != "") cur_stream->url = result.redirected_url;
			
			if (!result.fail && result.status_code_is_success() && result.data.size()) {
				{ // acquire necessary headers
					char *end;
					auto value = result.get_header("x-head-seqnum");
					cur_stream->seq_head = strtoll(value.c_str(), &end, 10);
					if (*end || !value.size()) {
						logger.error("net/dl", "failed to acquire x-head-seqnum");
						cur_stream->seq_head = -1;
						cur_stream->error = true;
					}
					value = result.get_header("x-sequence-num");
					cur_stream->seq_id = strtoll(value.c_str(), &end, 10);
					if (*end || !value.size()) {
						logger.error("net/dl", "failed to acquire x-sequence-num");
						cur_stream->seq_id = -1;
						cur_stream->error = true;
					}
				}
				if (!cur_stream->error) {
					cur_stream->len = result.data.size();
					cur_stream->block_num = (cur_stream->len + BLOCK_SIZE - 1) / BLOCK_SIZE;
					for (size_t i = 0; i < result.data.size(); i += BLOCK_SIZE) {
						size_t left = i;
						size_t right = std::min<size_t>(i + BLOCK_SIZE, result.data.size());
						cur_stream->set_data(i / BLOCK_SIZE, std::vector<u8>(result.data.begin() + left, result.data.begin() + right));
					}
					cur_stream->ready = true;
				}
			} else {
				logger.error("net/dl", "failed accessing : " + result.error);
				cur_stream->error = true;
				switch (result.status_code) {
					// these codes are returned when trying to read beyond the end of the livestream
					case HTTP_STATUS_CODE_NO_CONTENT :
					case HTTP_STATUS_CODE_NOT_FOUND :
						cur_stream->livestream_eof = true;
						break;
					// this code is returned when trying to read an ended livestream without archive
					case HTTP_STATUS_CODE_FORBIDDEN :
						cur_stream->livestream_private = true;
						break;
				}
			}
		} else {
			u64 block_reading = read_heads[cur_stream_index] / BLOCK_SIZE;
			if (cur_stream->ready) {
				while (block_reading < cur_stream->block_num && cur_stream->downloaded_data.count(block_reading)) block_reading++;
				if (block_reading == cur_stream->block_num) { // something unexpected happened
					logger.error(LOG_THREAD_STR, "unexpected error (trying to read beyond the end of the stream)");
					cur_stream->error = true;
					continue;
				}
			}
			// Util_log_save("net/dl", "dl next : " + std::to_string(cur_stream_index) + " " + std::to_string(block_reading));
			
			u64 start = block_reading * BLOCK_SIZE;
			u64 end = cur_stream->ready ? std::min((block_reading + 1) * BLOCK_SIZE, cur_stream->len) : (block_reading + 1) * BLOCK_SIZE;
			u64 expected_len = end - start;
			
			auto &session_list = cur_stream->session_list ? *cur_stream->session_list : thread_network_session_list;
			// length not sure -> use Range header to get the size (slower)
			auto result = cur_stream->len == 0 ?
				session_list.perform(HttpRequest::GET(cur_stream->url, {{"Range", "bytes=" + std::to_string(start) + "-" + std::to_string(end - 1)}})) :
				session_list.perform(HttpRequest::GET(cur_stream->url + "&range=" + std::to_string(start) + "-" + std::to_string(end - 1), {}));
			if (result.redirected_url != "") cur_stream->url = remove_url_parameter(result.redirected_url, "range");
			
			
			if (!result.fail && result.status_code_is_success()) {
				if (cur_stream->len == 0) {
					auto content_range_str = result.get_header("Content-Range");
					char *slash = strchr(content_range_str.c_str(), '/');
					bool ok = false;
					if (slash) {
						char *end;
						cur_stream->len = strtoll(slash + 1, &end, 10);
						if (!*end) {
							ok = true;
							cur_stream->block_num = NetworkStream::get_block_num(cur_stream->len);
						} else logger.error(LOG_THREAD_STR, "failed to parse Content-Range : " + std::string(slash + 1));
					} else logger.error(LOG_THREAD_STR, "no slash in Content-Range response header : " + content_range_str);
					if (!ok) cur_stream->error = true;
				}
				if (cur_stream->ready && result.data.size() != expected_len) {
					logger.error(LOG_THREAD_STR, "size discrepancy : " + std::to_string(expected_len) + " -> " + std::to_string(result.data.size()));
					if (cur_stream->retry_cnt_left) {
						cur_stream->retry_cnt_left--;
					} else cur_stream->error = true;
					continue;
				}
				cur_stream->retry_cnt_left = NetworkStream::RETRY_CNT_MAX;
				cur_stream->set_data(block_reading, result.data);
				cur_stream->ready = true;
			} else if (!result.fail) {
				logger.error("net/dl", "stream returned: " + std::to_string(result.status_code));
				cur_stream->error = true;
			} else {
				logger.error("net/dl", "access failed : " + result.error);
				if (cur_stream->retry_cnt_left) {
					cur_stream->retry_cnt_left--;
				} else cur_stream->error = true;
			}
		}
	}
	logger.info(LOG_THREAD_STR, "Exit, deiniting...");
	for (auto stream : streams) if (stream) stream->quit_request = true;
}
void NetworkStreamDownloader::delete_all() {
	for (auto &stream : streams) {
		delete stream;
		stream = NULL;
	}
}


// --------------------------------
// other functions implementation
// --------------------------------

void network_downloader_thread(void *downloader_) {
	NetworkStreamDownloader *downloader = (NetworkStreamDownloader *) downloader_;
	
	downloader->downloader_thread();
	
	threadExit(0);
}

