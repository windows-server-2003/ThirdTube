#pragma once
#include "network_downloader.hpp"
#include "types.hpp"
#include <vector>
#include <set>
#include <deque>
#include <string.h>

#include "system/fake_pthread.hpp"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
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
		size_t size_max() { return num; }
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

class NetworkDecoder;

#define DECODER_REINIT_INTERVAL_PACKETS 40000

class NetworkDecoderFFmpegIOData {
private :
	static constexpr int VIDEO = 0;
	static constexpr int AUDIO = 1;
	static constexpr int BOTH = 0;
	
	// type : VIDEO, AUDIO
public :
	bool video_audio_seperate = false;
	bool audio_only = false;
	NetworkStream *network_stream[2] = {NULL, NULL};
	std::pair<NetworkDecoder *, NetworkStream *> *opaque[2] = {NULL, NULL};
	AVFormatContext *format_context[2] = {NULL, NULL};
	AVIOContext *io_context[2] = {NULL, NULL};
	int stream_index[2] = {0, 0};
	NetworkDecoder *parent_decoder = NULL;
	int packets_until_next_reinit = DECODER_REINIT_INTERVAL_PACKETS;
	
	Result_with_string init_(int type, NetworkDecoder *parent_decoder);
	Result_with_string init(NetworkStream *video_stream, NetworkStream *audio_stream, NetworkDecoder *parent_decoder);
	Result_with_string init(NetworkStream *both_stream, NetworkDecoder *parent_decoder);
	void deinit_(int type, bool deinit_stream);
	void deinit(bool deinit_stream);
	Result_with_string reinit();
	Result_with_string reinit_stream(int type, int64_t seek_timestamp);
	double get_duration();
};


class NetworkDecoderFilterData {
public :
	AVFilterGraph *audio_filter_graph = NULL;
	AVFilterContext *audio_filter_src = NULL;
	AVFilterContext *audio_filter_sink = NULL;
	AVFrame *output_frame;
	
	// actual values used
	double volume = 1.0;
	double tempo = 1.0;
	double pitch = 1.0;
	double equalizer_values[18] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
	// change requests(applied at reinitialization)
	volatile double volume_request = 1.0;
	volatile double tempo_request = 1.0;
	volatile double pitch_request = 1.0;
	volatile double equalizer_values_request[18] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
	// used to determine the pts of the output frames
	bool first_frame = true;
	double input_time_base = 0;
	double initial_timestamp = -1;
	int64_t received_frame_num = 0;
	
	void deinit();
	Result_with_string init(AVCodecContext *audio_context);
	Result_with_string process_audio_frame(AVFrame *input, double *out_pts); // filtered frame goes to output_frame. It should NOT be freed
};

class NetworkDecoder {
private :
	static constexpr size_t OLD_MAX_RAW_BUFFER_SIZE = 3 * 1000 * 1000;
	static constexpr size_t NEW_MAX_RAW_BUFFER_SIZE = 8 * 1000 * 1000;
	static constexpr int VIDEO = 0;
	static constexpr int AUDIO = 1;
	static constexpr int BOTH = 0;
	
	// ffmpeg io/format related things
	NetworkDecoderFFmpegIOData *io = NULL;
	// ffmpeg decoder
	AVCodecContext *decoder_context[2] = {NULL, NULL};
	SwrContext *swr_context = NULL;
	const AVCodec *codec[2] = {NULL, NULL};
	
	// buffers
	std::deque<AVPacket *> packet_buffer[2];
	network_decoder_::output_buffer<AVFrame *> video_tmp_frames;
	network_decoder_::output_buffer<u8 *> video_mvd_tmp_frames;
	u8 *mvd_frame = NULL; // internal buffer written directly by the mvd service
	u8 *sw_video_output_tmp = NULL;
	Mutex buffered_pts_list_lock; // lock of buffered_pts_list
	std::multiset<double> buffered_pts_list; // used for HW decoder to determine the pts when outputting a frame
	bool mvd_first = false;
	
	Mutex critical_op_lock; // lock between main(ui) thread and decoder thread
	
	Result_with_string init_output_buffer(bool);
	Result_with_string init_decoder(int type);
	Result_with_string read_packet(int type);
	Result_with_string mvd_decode(int *width, int *height);
	AVStream *get_stream(int type) { return io->format_context[is_av_separate() ? type : BOTH]->streams[io->stream_index[type]]; }
public :
	bool hw_decoder_enabled = false;
	volatile bool interrupt = false;
	volatile bool need_reinit = false;
	volatile bool ready = false;
	volatile bool avformat_reinit_request[2] = {false, false};
	double timestamp_offset = 0;
	bool frame_cores_enabled[4];
	bool slice_cores_enabled[4];
	
	bool is_audio_only() { return io->audio_only; }
	bool is_av_separate() { return io->video_audio_seperate; }
	
	void set_frame_cores_enabled(bool *enabled) { memcpy(frame_cores_enabled, enabled, 4); }
	void set_slice_cores_enabled(bool *enabled) { memcpy(slice_cores_enabled, enabled, 4); }
	
	const char *get_network_waiting_status() {
		const char *res = NULL;
		critical_op_lock.lock();
		if (ready && io && io->network_stream[VIDEO] && io->network_stream[VIDEO]->network_waiting_status) res = io->network_stream[VIDEO]->network_waiting_status;
		if (ready && io && io->network_stream[AUDIO] && io->network_stream[AUDIO]->network_waiting_status) res = io->network_stream[AUDIO]->network_waiting_status;
		critical_op_lock.unlock();
		return res;
	}
	std::vector<std::pair<double, std::vector<double> > > get_buffering_progress_bars(int bar_len);
	
	size_t get_raw_buffer_num() { return hw_decoder_enabled ? video_mvd_tmp_frames.size() : video_tmp_frames.size(); }
	size_t get_raw_buffer_num_max() { return hw_decoder_enabled ? video_mvd_tmp_frames.size_max() : video_tmp_frames.size_max(); }
	
	
	Result_with_string init(bool request_hw_decoder); // should be called after change_ffmpeg_data()
	void deinit();
	void clear_buffer();
	
	
	NetworkDecoderFilterData filter;
	void deinit_filter() { filter.deinit(); }
	// should be called after this->init()
	Result_with_string init_filter() { return filter.init(decoder_context[AUDIO]); }
	// used for livestreams/premieres where the video is splitted into fragments
	void change_ffmpeg_io_data(NetworkDecoderFFmpegIOData &ffmpeg_io_data, double timestamp_offset) {
		interrupt = false;
		this->io = &ffmpeg_io_data;
		this->timestamp_offset = timestamp_offset;
	}
	
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
	
	enum class PacketType {
		AUDIO,
		VIDEO,
		EoF, // EOF is reserved so...
		INTERRUPTED
	};
	PacketType next_decode_type();
	
	enum class DecoderType {
		HW, // hardware decoder
		MT_FRAME, // frame-multithreaded
		MT_SLICE, // slice-multithreaded
		ST, // single threaded
		NA // not initialized
	};
	DecoderType get_decoder_type() {
		DecoderType res;
		critical_op_lock.lock();
		if (!ready) res = DecoderType::NA;
		else if (hw_decoder_enabled) res = DecoderType::HW;
		else if (decoder_context[VIDEO]->active_thread_type == FF_THREAD_FRAME) res = DecoderType::MT_FRAME;
		else if (decoder_context[VIDEO]->active_thread_type == FF_THREAD_SLICE) res = DecoderType::MT_SLICE;
		else res = DecoderType::ST;
		critical_op_lock.unlock();
		return res;
	}
	
	
	// decode the previously read video packet
	// decoded image is stored internally and can be acquired via get_decoded_video_frame()
	Result_with_string decode_video(int *width, int *height, bool *key_frame);
	
	// decode the previously read audio packet
	// if return.code == 0, *data is a malloced pointer and should be freed
	// otherwise, *data is untouched
	Result_with_string decode_audio(int *size, u8 **data, double *cur_pos);
	
	// get the previously decoded video frame raw data
	// the pointer stored in *data should NOT be freed
	Result_with_string get_decoded_video_frame(int width, int height, u8** data, double *cur_pos);
	
	// seek both audio and video
	Result_with_string seek(s64 microseconds);
};

