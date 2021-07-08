#pragma once
#include "network_io.hpp"
#include "speaker.hpp"
#include "system/types.hpp"
#include <vector>
#include <set>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
}

namespace network_decoder_ {
	/*
		internal queue used to buffer the raw output of decoded images
		thread-safe when one thread only pushes and the other thread pops
	*/
	template <typename T> class output_buffer {
		size_t num = 0;
		std::vector<T> buffer;
		volatile size_t head = 0; // the index of the element in the buffer which the next pushed element should go in
		volatile size_t tail = 0; // the index of the element in the buffer which should be poped next
		
		public :
		void init(const std::vector<T> &buffer_init) {
			num = buffer_init.size() - 1;
			buffer = buffer_init;
			head = tail = 0;
			Util_log_save("que", "init:" + std::to_string(num));
		}
		std::vector<T> deinit() {
			auto res = buffer;
			buffer.clear();
			num = 0;
			head = tail = 0;
			return res;
		}
		// get the size of the queue
		size_t size() {
			if (head >= tail) return head - tail;
			else return head + num + 1 - tail;
		}
		bool full() {
			return size() == num;
		}
		bool empty() {
			return size() == 0;
		}
		T get_next_pushed() {
			if (full()) return NULL;
			return buffer[head];
		}
		bool push() {
			if (full()) return false;
			head = (head == num ? 0 : head + 1);
			return true;
		}
		T get_next_poped() {
			if (empty()) return NULL;
			return buffer[tail];
		}
		bool pop() {
			if (empty()) return false;
			tail = (tail == num ? 0 : tail + 1);
			return true;
		}
		void clear() {
			head = tail;
		}
	};
}


class NetworkDecoder {
private :
	static constexpr int VIDEO = 0;
	static constexpr int AUDIO = 1;
	NetworkStreamCacherData *network_cacher[2] = {NULL, NULL};
	AVFormatContext *format_context[2] = {NULL, NULL};
	AVIOContext *io_context[2] = {NULL, NULL};
	AVCodecContext *decoder_context[2] = {NULL, NULL};
	SwrContext *swr_context = NULL;
	const AVCodec *codec[2] = {NULL, NULL};
	AVPacket *tmp_packet[2] = {NULL, NULL};
	bool eof[2] = {false, false};
	network_decoder_::output_buffer<AVFrame *> video_tmp_frames;
	network_decoder_::output_buffer<u8 *> video_mvd_tmp_frames;
	u8 *mvd_frame = NULL; // internal buffer written directly by GPU
	u8 *sw_video_output_tmp = NULL;
	Handle buffered_pts_list_lock; // lock of buffered_pts_list
	std::multiset<double> buffered_pts_list; // used for HW decoder to determine the pts when outputting a frame
	bool mvd_first = false;
	
	Result_with_string init_(int, AVMediaType);
	Result_with_string init_video_decoder(bool &);
	Result_with_string init_audio_decoder();
	Result_with_string read_packet(int type);
	Result_with_string mvd_decode(int *width, int *height);
public :
	bool hw_decoder_enabled = false;
	
	void deinit();
	Result_with_string init(NetworkStreamCacherData *video_cacher, NetworkStreamCacherData *audio_cacher, bool request_hw_decoder);
	
	struct VideoFormatInfo {
		int width;
		int height;
		double framerate;
		std::string format_name;
		double duration;
	};
	VideoFormatInfo get_video_info();
	
	struct AudioFormatInfo {
		int bitrate;
		int sample_rate;
		int ch;
		std::string format_name;
		double duration;
	};
	AudioFormatInfo get_audio_info();
	
	Result_with_string read_video_packet() { return read_packet(VIDEO); }
	Result_with_string read_audio_packet() { return read_packet(AUDIO); }
	std::string next_decode_type();
	
	
	// decode the previously read video packet
	// decoded image is stored internally and can be acquired via get_decoded_video_frame()
	Result_with_string decode_video(int *width, int *height, bool *key_frame, double *cur_pos);
	
	// decode the previously read audio packet
	Result_with_string decode_audio(int *size, u8 **data, double *cur_pos);
	
	// get the previously decoded video frame raw data
	// the pointer stored in *data should NOT be freed
	Result_with_string get_decoded_video_frame(int width, int height, u8** data, double *cur_pos);
	
	// seek both audio and video
	Result_with_string seek(s64 microseconds);
};

