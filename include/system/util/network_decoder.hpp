#pragma once
#include "utils.hpp"
#include "network_io.hpp"
#include "speaker.hpp"
#include "system/types.hpp"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
}

class NetworkDecoder {
private :
	AVFormatContext *video_format_context = NULL;
	AVFormatContext *audio_format_context = NULL;
	AVIOContext *video_io_context = NULL;
	AVIOContext *audio_io_context = NULL;
	AVCodecContext *video_decoder_context = NULL;
	AVCodecContext *audio_decoder_context = NULL;
	SwrContext *swr_context = NULL;
	const AVCodec *video_codec = NULL;
	const AVCodec *audio_codec = NULL;
	AVPacket *video_tmp_packet = NULL;
	AVPacket *audio_tmp_packet = NULL;
	fixed_capacity_queue<AVFrame *> video_tmp_frames;
	
	Result_with_string init_(NetworkStreamCacherData *, AVFormatContext *&, AVIOContext *&, AVMediaType);
	Result_with_string init_video_decoder();
	Result_with_string init_audio_decoder();
	Result_with_string read_packet(AVFormatContext *, AVPacket *&);
public :
	
	void deinit();
	Result_with_string init(NetworkStreamCacherData *video_cacher, NetworkStreamCacherData *audio_cacher);
	
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
	
	Result_with_string read_video_packet() { return read_packet(video_format_context, video_tmp_packet); }
	Result_with_string read_audio_packet() { return read_packet(audio_format_context, audio_tmp_packet); }
	std::string next_decode_type();
	
	
	// decode the previously read video packet
	// decoded image is stored internally and can be acquired via get_decoded_video_frame()
	Result_with_string decode_video(int *width, int *height, bool *key_frame, double *cur_pos);
	
	// decode the previously read audio packet
	Result_with_string decode_audio(int *size, u8 **data, double *cur_pos);
	
	// get the previously decoded video frame raw data
	Result_with_string get_decoded_video_frame(int width, int height, u8** data, double *cur_pos);
	
	// seek both audio and video
	Result_with_string seek(s64 microseconds);
};

