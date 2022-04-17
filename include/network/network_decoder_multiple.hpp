#pragma once
#include <string>
#include <map>
#include "headers.hpp"
#include "types.hpp"
#include "network_io.hpp"
#include "network_decoder.hpp"
#include "network_downloader.hpp"


class NetworkMultipleDecoder {
private :
	static constexpr int MAX_CACHE_FRAGMENTS_NUM = 10;
	static constexpr int MAX_LIVESTREAM_RETRY = 2;
	volatile bool initer_stop_request = true;
	volatile bool initer_exit_request = false;
	volatile bool initer_stopping = false;
	
	bool mvd_inited = false;
	bool inited = false;
	bool is_livestream = false;
	
	void init_mvd();
	
	bool video_audio_seperate;
	bool adjust_timestamp = false;
	
	std::string video_url;
	std::string audio_url;
	std::string both_url;
	
	NetworkDecoder decoder;
	Mutex fragments_lock;
	std::map<int, NetworkDecoderFFmpegIOData> fragments;
	std::map<int, int> error_count;
	int fragment_len = -1;
	NetworkStreamDownloader *downloader = NULL;
	
	volatile int seq_buffered_head = 0;
	
	volatile int seq_using = 0;
	volatile int seq_num = std::numeric_limits<int>::max();
	
	volatile int seq_head = 0; // the latest sequence id available
	volatile double duration_first_fragment = 0; // for some reason, the first fragment of a livestream differs in length from other fragments
	
	double cur_preamp = 1.0;
	double cur_tempo = 1.0;
	double cur_pitch = 1.0;
	
	void check_filter_update();
	void recalc_buffered_head();
public :
	volatile bool &hw_decoder_enabled = decoder.hw_decoder_enabled;
	volatile bool &interrupt = decoder.interrupt;
	volatile bool &need_reinit = decoder.need_reinit;
	volatile const bool &ready = decoder.ready;
	volatile double preamp_change_request = -1;
	volatile double tempo_change_request = -1;
	volatile double pitch_change_request = -1;
	const char *get_network_waiting_status() { return decoder.get_network_waiting_status(); }
	
	NetworkMultipleDecoder () = default;
	
	void deinit();
	// pass fragment_len == -1 if it's not a livestream
	Result_with_string init(std::string video_url, std::string audio_url, NetworkStreamDownloader &downloader, int fragment_len, bool adjust_timestamp, bool request_hw_decoder);
	Result_with_string init(std::string both_url, NetworkStreamDownloader &downloader, int fragment_len, bool adjust_timestamp, bool request_hw_decoder);
	
	void livestream_initer_thread_func();
	void request_thread_exit() { initer_exit_request = true; }
	
	void set_frame_cores_enabled(bool *enabled) { decoder.set_frame_cores_enabled(enabled); }
	void set_slice_cores_enabled(bool *enabled) { decoder.set_slice_cores_enabled(enabled); }
	void request_avformat_reinit() { decoder.avformat_reinit_request[0] = decoder.avformat_reinit_request[1] = true; }
	
	using VideoFormatInfo = NetworkDecoder::VideoFormatInfo;
	using AudioFormatInfo = NetworkDecoder::AudioFormatInfo;
	VideoFormatInfo get_video_info() { return decoder.get_video_info(); }
	AudioFormatInfo get_audio_info() { return decoder.get_audio_info(); }
	
	size_t get_raw_buffer_num() { return decoder.get_raw_buffer_num(); }
	size_t get_raw_buffer_num_max() { return decoder.get_raw_buffer_num_max(); }
	
	double get_duration() { return duration_first_fragment + (fragment_len != -1 ? std::min(seq_num - 1, (int) seq_head) * fragment_len : 0); }
	double get_forward_buffer() { return fragment_len == -1 ? 0 : (seq_buffered_head - seq_using - 1) * fragment_len; }
	double get_timestamp_from_bar_pos(double pos) {
		pos = std::min(1.0, std::max(0.0, pos));
		double duration = get_duration();
		if (is_livestream && !adjust_timestamp) return duration - std::min<double>(duration, 60 * 60 * 12) * (1 - pos);
		else return duration * pos;
	}
	double get_bar_pos_from_timestamp(double timestamp) {
		double duration = get_duration();
		double res;
		if (is_livestream && !adjust_timestamp) res = 1 - (duration - timestamp) / std::min<double>(duration, 60 * 60 * 12);
		else res = timestamp / duration;
		return std::max(0.0, std::min(1.0, res));
	}
	
	std::vector<std::pair<double, std::vector<double> > > get_buffering_progress_bars(int bar_len);
	
	// the switch to the next sequence is done inside this function
	using PacketType = NetworkDecoder::PacketType;
	PacketType next_decode_type();
	
	using DecoderType = NetworkDecoder::DecoderType;
	DecoderType get_decoder_type() { return decoder.get_decoder_type(); }
	
	// decode the previously read video packet
	// decoded image is stored internally and can be acquired via get_decoded_video_frame()
	Result_with_string decode_video(int *width, int *height, bool *key_frame, double *cur_pos) {
		auto res = decoder.decode_video(width, height, key_frame, cur_pos);
		return res;
	}
	
	// decode the previously read audio packet
	Result_with_string decode_audio(int *size, u8 **data, double *cur_pos) {
		check_filter_update();
		auto res = decoder.decode_audio(size, data, cur_pos);
		return res;
	}
	
	// get the previously decoded video frame raw data
	// the pointer stored in *data should NOT be freed
	Result_with_string get_decoded_video_frame(int width, int height, u8** data, double *cur_pos) {
		auto res = decoder.get_decoded_video_frame(width, height, data, cur_pos);
		return res;
	}
	
	// seek both audio and video
	Result_with_string seek(s64 microseconds);
};
// `arg` should be a pointer to an instance of NetworkMultipleDecoder
void livestream_initer_thread_func(void *arg);

