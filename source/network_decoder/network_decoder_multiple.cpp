#include "network_decoder_multiple.hpp"
#include "headers.hpp"

void NetworkMultipleDecoder::deinit() {
	initer_stop_request = true;
	while (!initer_stopping) usleep(10000);
	
	inited = false;
	
	decoder.deinit();
	decoder.deinit_filter();
	for (auto &i : fragments) i.second.deinit(true);
	fragments.clear();
	if (mvd_inited) {
		mvdstdExit();
		mvd_inited = false;
	}
	
	initer_stop_request = false;
	while (initer_stopping && !initer_exit_request) usleep(10000);
}
void NetworkMultipleDecoder::init_mvd() {
	if (!mvd_inited) {
		Result mvd_result = -1;
		for (int mb = 15; mb >= 5; mb--) {
			mvd_result = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_BGR565, 1000000 * mb, NULL);
			if (mvd_result == 0) {
				logger.info("dec/init", "mvdstdInit ok at " + std::to_string(mb) + " MB (total : " + std::to_string(linearSpaceFree() / 1000) + " KB)");
				break;
			}
		}
		if (mvd_result != 0) logger.error("dec/init", "mvdstdInit returned " + std::to_string(mvd_result) + " with a minimum size of work buffer");
		else mvd_inited = true;
	}
}
Result_with_string NetworkMultipleDecoder::init(std::string video_url, std::string audio_url, NetworkStreamDownloader &downloader, int fragment_len,
	bool adjust_timestamp, bool request_hw_decoder) {
	
	Result_with_string result;
	
	if (inited) {
		logger.caution("net/mul-dec", "double init, deiniting...");
		deinit();
	}
	
	initer_stop_request = true;
	while (!initer_stopping) usleep(10000);
	
	video_audio_seperate = (video_url != audio_url);
	is_livestream = (fragment_len != -1);
	if (video_audio_seperate) {
		this->video_url = video_url;
		this->audio_url = audio_url;
	} else this->both_url = video_url;
	this->fragment_len = fragment_len;
	this->downloader = &downloader;
	error_count.clear();
	this->adjust_timestamp = adjust_timestamp;
	
	if (request_hw_decoder) init_mvd();
	
	auto get_base_url = [&] (const std::string &url) {
		auto erase_start = url.find("&sq=");
		if (erase_start == std::string::npos) return url;
		auto erase_end = erase_start + 4;
		while (erase_end < url.size() && url[erase_end] != '&') erase_end++;
		return url.substr(0, erase_start) + url.substr(erase_end, url.size());
	};
	
	std::string url_append = !is_livestream ? "" : fragment_len == 1 ? "&headm=4" : "&headm=2";
	int fragment_id = 0;
	NetworkDecoderFFmpegIOData tmp_ffmpeg_data;
	std::vector<NetworkStream *> streams;
	if (video_audio_seperate) {
		NetworkStream *video_stream = new NetworkStream(video_url + url_append, is_livestream, NULL);
		NetworkStream *audio_stream = new NetworkStream(audio_url + url_append, is_livestream, NULL);
		streams = {video_stream, audio_stream};
		downloader.add_stream(video_stream);
		downloader.add_stream(audio_stream);
		decoder.interrupt = false;
		result = tmp_ffmpeg_data.init(video_stream, audio_stream, &decoder);
		
		if (is_livestream) fragment_id = video_stream->seq_id;
		// handle redirect
		video_url = get_base_url(video_stream->url);
		audio_url = get_base_url(audio_stream->url);
	} else {
		NetworkStream *both_stream = new NetworkStream(both_url + url_append, is_livestream, NULL);
		streams = { both_stream };
		downloader.add_stream(both_stream);
		decoder.interrupt = false;
		result = tmp_ffmpeg_data.init(both_stream, &decoder);
		
		if (is_livestream) fragment_id = both_stream->seq_id;
		// handle redirect
		both_url = get_base_url(both_stream->url);
	}
	if (result.code != 0) goto cleanup;
	fragments[fragment_id] = tmp_ffmpeg_data;
	decoder.change_ffmpeg_io_data(fragments[fragment_id], adjust_timestamp ? fragment_id * fragment_len : 0);
	result = decoder.init(request_hw_decoder);
	if (result.code != 0) goto cleanup;
	result = decoder.init_filter();
	if (result.code != 0) goto cleanup;
	
	seq_using = fragment_id;
	seq_num = is_livestream ? std::numeric_limits<int>::max() : 1;
	if (streams[0]->seq_head != -1) seq_head = streams[0]->seq_head;
	seq_buffered_head = fragment_id + 1;
	if (adjust_timestamp) duration_first_fragment = fragment_len; // this seems to be true for premiere livestreams
	else duration_first_fragment = tmp_ffmpeg_data.get_duration() - fragment_id * fragment_len;
	
	inited = true;
	
	initer_stop_request = false;
	while (initer_stopping && !initer_exit_request) usleep(10000);
	
	return result;
	
	cleanup :
	tmp_ffmpeg_data.deinit(false);
	decoder.deinit_filter();
	for (auto stream : streams) stream->quit_request = true;
	return result;
}
Result_with_string NetworkMultipleDecoder::init(std::string both_url, NetworkStreamDownloader &downloader, int fragment_len, bool adjust_timestamp, bool request_hw_decoder) {
	return init(both_url, both_url, downloader, fragment_len, adjust_timestamp, request_hw_decoder);
}

void NetworkMultipleDecoder::check_filter_update() {
	if (filter_update_request) {
		filter_update_request = false;
		decoder.deinit_filter();
		decoder.init_filter();
	}
}

NetworkMultipleDecoder::PacketType NetworkMultipleDecoder::next_decode_type() {
	PacketType res = decoder.next_decode_type();
	if (res == PacketType::EoF) {
		while (!initer_exit_request && !interrupt && seq_using + 1 < seq_num) {
			fragments_lock.lock();
			if (fragments.count(seq_using + 1)) break;
			fragments_lock.unlock();
			usleep(30000);
		}
		/*
		if (interrupt || initer_exit_request) Util_log_save("net-mul", "interrupt");
		else if (seq_using + 1 >= seq_num) Util_log_save("net-mul", "seq end : " + std::to_string(seq_using) + " " + std::to_string(seq_num));
		*/
		
		if (interrupt || initer_exit_request) res = PacketType::INTERRUPTED;
		else if (seq_using + 1 >= seq_num) res = PacketType::EoF;
		else {
			decoder.change_ffmpeg_io_data(fragments[seq_using + 1], adjust_timestamp ? (seq_using + 1) * fragment_len : 0);
			seq_using++;
			res = decoder.next_decode_type();
			// Util_log_save("net-mul", "after change ffmpeg res : " + std::to_string((int) res));
		}
		fragments_lock.unlock();
	}
	return res;
}
void NetworkMultipleDecoder::recalc_buffered_head() {
	int res = seq_using;
	while (fragments.count(res)) res++;
	seq_buffered_head = res;
}
std::vector<std::pair<double, std::vector<double> > > NetworkMultipleDecoder::get_buffering_progress_bars(int bar_len) {
	if (!inited || !decoder.ready) return {};
	if (is_livestream) {
		std::pair<double, std::vector<double> > res = {0, std::vector<double>(bar_len)};
		double pos = std::max(0.0, (double) (seq_buffered_head - seq_using - 1) / MAX_CACHE_FRAGMENTS_NUM);
		for (int i = 0; i < bar_len; i++) res.second[i] = std::min(1.0, std::max(0.0, bar_len * pos - i)) * 100;
		return { res };
	} else return decoder.get_buffering_progress_bars(bar_len);
}
Result_with_string NetworkMultipleDecoder::seek(s64 microseconds) {
	Result_with_string result;
	if (need_reinit) { // the initer function should be stopped
		need_reinit = false;
		result = fragments[(int) seq_using].reinit();
		if (result.code != 0) {
			fragments[(int) seq_using].deinit(true);
			fragments.erase((int) seq_using);
			return result;
		}
	}
	if (is_livestream) {
		int next_fragment = 0;
		if (microseconds / 1000000 > duration_first_fragment)
			next_fragment = (int) ((microseconds / 1000000 - duration_first_fragment) / fragment_len);
		next_fragment = std::min(next_fragment, (int) seq_head); // just to be safe
		seq_using = next_fragment;
		while (!initer_exit_request && !decoder.interrupt) {
			fragments_lock.lock();
			if (fragments.count((int) seq_using)) break;
			fragments_lock.unlock();
			usleep(30000);
		}
		if (initer_exit_request || decoder.interrupt) {
			result.code = -1;
			result.error_description = initer_exit_request ? "The app is about to close" : "Interrupted";
			return result;
		}
		recalc_buffered_head();
		decoder.clear_buffer();
		decoder.change_ffmpeg_io_data(fragments[(int) seq_using], adjust_timestamp ? seq_using * fragment_len : 0);
		fragments_lock.unlock();
	} else {
		decoder.clear_buffer();
		// flush data buffered inside the filter
		decoder.deinit_filter();
		decoder.init_filter();
		
		decoder.change_ffmpeg_io_data(fragments[(int) seq_using], adjust_timestamp ? seq_using * fragment_len : 0);
		// trying to seek to a point too close to the end somehow causes ffmpeg to read the entire stream again ?
		microseconds = std::max(0.0, std::min((double) microseconds, (get_duration() - 1) * 1000000));
		result = decoder.seek(microseconds);
	}
	return result;
}

void NetworkMultipleDecoder::livestream_initer_thread_func() {
	while (!initer_exit_request) {
		while (initer_stop_request && !initer_exit_request) {
			initer_stopping = true;
			usleep(10000);
		}
		if (initer_exit_request) break;
		initer_stopping = false;
		
		if (!inited || !is_livestream) {
			usleep(10000);
			continue;
		}
		
		int seq_next = seq_using;
		while (fragments.count(seq_next)) seq_next++;
		if (seq_next - seq_using >= MAX_CACHE_FRAGMENTS_NUM || seq_next >= seq_num) {
			usleep(10000);
			continue;
		}
		logger.info("net/live-init", "next : " + std::to_string(seq_next));
		
		NetworkDecoderFFmpegIOData tmp_ffmpeg_data;
		if (video_audio_seperate) {
			NetworkStream *video_stream = new NetworkStream(video_url + "&sq=" + std::to_string(seq_next), is_livestream, NULL);
			NetworkStream *audio_stream = new NetworkStream(audio_url + "&sq=" + std::to_string(seq_next), is_livestream, NULL);
			video_stream->disable_interrupt = audio_stream->disable_interrupt = true;
			downloader->add_stream(video_stream);
			downloader->add_stream(audio_stream);
			
			Result_with_string result = tmp_ffmpeg_data.init(video_stream, audio_stream, &decoder);
			// Util_log_save("debug", "init finish");
			if (result.code != 0) {
				if (video_stream->livestream_eof || audio_stream->livestream_eof) {
					if (++error_count[seq_next] >= MAX_LIVESTREAM_RETRY) {
						logger.caution("net/live-init", "eof detected, the end of the livestream");
						seq_num = std::min((int) seq_num, seq_next);
					} else logger.info("net/live-init", "eof detected, retrying...  retry cnt : " + std::to_string(error_count[seq_next]));
				} else error_count[seq_next] = 0;
				if (video_stream->livestream_private || audio_stream->livestream_private) {
					logger.caution("net/live-init", "the livestream became private");
					seq_num = 0;
				}
				logger.caution("net/live-init", "init err : " + result.error_description);
				video_stream->quit_request = true;
				audio_stream->quit_request = true;
				continue;
			} else if (decoder.interrupt) {
				logger.caution("net/live-init", "init interrupt but not err : " + result.error_description);
				video_stream->quit_request = true;
				audio_stream->quit_request = true;
				continue;
			} else if (result.code == 0) {
				if (video_stream->seq_head != -1) seq_head = video_stream->seq_head;
			}
			video_stream->disable_interrupt = audio_stream->disable_interrupt = false;
		} else {
			NetworkStream *both_stream = new NetworkStream(both_url + "&sq=" + std::to_string(seq_next), is_livestream, NULL);
			both_stream->disable_interrupt = true;
			downloader->add_stream(both_stream);
			Result_with_string result = tmp_ffmpeg_data.init(both_stream, &decoder);
			if (result.code == 0) {
				if (both_stream->seq_head != -1) seq_head = both_stream->seq_head;
			} else {
				if (both_stream->livestream_eof) {
					if (++error_count[seq_next] >= MAX_LIVESTREAM_RETRY) {
						logger.caution("net/live-init", "eof detected, the end of the livestream");
						seq_num = std::min((int) seq_num, seq_next);
					} else logger.caution("net/live-init", "eof detected, retrying...  retry cnt : " + std::to_string(error_count[seq_next]));
				} else error_count[seq_next] = 0;
				if (both_stream->livestream_private) {
					logger.caution("net/live-init", "the livestream became private");
					seq_num = 0;
				}
				both_stream->quit_request = true;
				continue;
			}
			both_stream->disable_interrupt = false;
		}
		
		fragments_lock.lock();
		fragments[seq_next] = tmp_ffmpeg_data;
		if (fragments.size() > MAX_CACHE_FRAGMENTS_NUM) {
			if (fragments.begin()->first < seq_using) {
				fragments.begin()->second.deinit(true);
				fragments.erase(fragments.begin());
			} else {
				std::prev(fragments.end())->second.deinit(true);
				fragments.erase(std::prev(fragments.end()));
			}
		}
		recalc_buffered_head();
		fragments_lock.unlock();
		
		logger.info("net/live-init", "finish : " + std::to_string(seq_next));
	}
	initer_stopping = true;
}


void livestream_initer_thread_func(void *arg) {
	NetworkMultipleDecoder *mul_decoder = (NetworkMultipleDecoder *) arg;
	mul_decoder->livestream_initer_thread_func();
	
	logger.info("net/live-init", "Thread exit.");
	threadExit(0);
}


