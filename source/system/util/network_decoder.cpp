#include "headers.hpp"
#include "system/util/network_io.hpp"

// mostly stolen from decoder.cpp

extern "C" void memcpy_asm(u8*, u8*, int);

void NetworkDecoder::deinit() {
	avcodec_free_context(&video_decoder_context);
	avcodec_free_context(&audio_decoder_context);
	av_freep(&video_io_context->buffer);
	av_freep(&video_io_context);
	av_freep(&audio_io_context->buffer);
	av_freep(&audio_io_context);
	av_packet_free(&video_tmp_packet);
	av_packet_free(&audio_tmp_packet);
	for (auto i : video_tmp_frames.clear()) av_frame_free(&i);
	swr_free(&swr_context);
	avformat_close_input(&video_format_context);
	avformat_close_input(&audio_format_context);
}

static int read_network_stream(void *cacher_, u8 *buf, int buf_size_) { // size or AVERROR_EOF
	NetworkStreamCacherData *cacher = (NetworkStreamCacherData *) cacher_;
	size_t buf_size = buf_size_;
	
	// network_waiting_status = cacher->waiting_status;
	while (!cacher->latest_inited()) {
		// network_waiting_status = cacher->waiting_status;
		usleep(200000);
		if (cacher->has_error()) return AVERROR_EOF;
	}
	while (!cacher->is_data_available(cacher->head, std::min(buf_size, cacher->get_len() - cacher->head))) {
		// network_waiting_status = cacher->waiting_status;
		usleep(10000);
	}
	// network_waiting_status = NULL;
	
	auto tmp = cacher->get_data(cacher->head, std::min(buf_size, cacher->get_len() - cacher->head));
	cacher->head += tmp.size();
	size_t read_size = tmp.size();
	memcpy(buf, &tmp[0], tmp.size());
	if (!read_size) return AVERROR_EOF;
	return read_size;
}
static int64_t seek_network_stream(void *cacher_, s64 offset, int whence) { // size or AVERROR_EOF
	NetworkStreamCacherData *cacher = (NetworkStreamCacherData *) cacher_;
	
	if (whence == AVSEEK_SIZE) return cacher->get_len();
	size_t new_pos = 0;
	if (whence == SEEK_SET) new_pos = offset;
	else if (whence == SEEK_CUR) new_pos = cacher->head + offset;
	else if (whence == SEEK_END) new_pos = cacher->get_len() + offset;
	
	if (new_pos > cacher->get_len()) return -1;
	
	while (!cacher->latest_inited()) {
		// network_waiting_status = cacher->waiting_status;
		usleep(200000);
		if (cacher->has_error()) return -1;
	}
	// network_waiting_status = NULL;
	cacher->head = new_pos;
	cacher->set_download_start(new_pos);
	
	return cacher->head;
}

#define NETWORK_BUFFER_SIZE 0x8000
Result_with_string NetworkDecoder::init_(NetworkStreamCacherData *cacher, AVFormatContext *&format_context, AVIOContext *&io_context, AVMediaType expected_type) {
	Result_with_string result;
	int ffmpeg_result;
	
	cacher->head = 0;
	
	unsigned char *buffer = (unsigned char *) av_malloc(NETWORK_BUFFER_SIZE);
	if (!buffer) {
		result.error_description = "network buffer allocation failed";
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	io_context = avio_alloc_context(buffer, NETWORK_BUFFER_SIZE, 0, cacher, read_network_stream, NULL, seek_network_stream);
	if (!io_context) {
		result.error_description = "IO context allocation failed";
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	format_context = avformat_alloc_context();
	format_context->pb = io_context;
	ffmpeg_result = avformat_open_input(&format_context, "yay", NULL, NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avformat_open_input() failed " + std::to_string(ffmpeg_result);
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
		return result;
	}
	ffmpeg_result = avformat_find_stream_info(format_context, NULL);
	if (!format_context) {
		result.error_description = "avformat_find_stream_info() failed " + std::to_string(ffmpeg_result);
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
		return result;
	}
	// final check
	if (format_context->nb_streams != 1) {
		result.error_description = "nb_streams != 1 : " + std::to_string(format_context->nb_streams);
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	} else if (format_context->streams[0]->codecpar->codec_type != expected_type) {
		result.error_description = "stream type wrong : " + std::to_string(format_context->streams[0]->codecpar->codec_type);
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	}
	return result;
}

Result_with_string NetworkDecoder::init_video_decoder() {
	int ffmpeg_result;
	Result_with_string result;

	video_codec = avcodec_find_decoder(video_format_context->streams[0]->codecpar->codec_id);
	if(!video_codec) {
		result.error_description = "avcodec_find_decoder() failed";
		goto fail;
	}

	video_decoder_context = avcodec_alloc_context3(video_codec);
	if(!video_decoder_context) {
		result.error_description = "avcodec_alloc_context3() failed";
		goto fail;
	}

	ffmpeg_result = avcodec_parameters_to_context(video_decoder_context, video_format_context->streams[0]->codecpar);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_parameters_to_context() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}

	video_decoder_context->lowres = 0; // <-------
	ffmpeg_result = avcodec_open2(video_decoder_context, video_codec, NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_open2() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	// read the first packet
	result = read_video_packet();
	if (result.code != 0) goto fail;
	
	return result;
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
Result_with_string NetworkDecoder::init_audio_decoder() {
	int ffmpeg_result;
	Result_with_string result;

	audio_codec = avcodec_find_decoder(audio_format_context->streams[0]->codecpar->codec_id);
	if(!audio_codec) {
		result.error_description = "avcodec_find_decoder() failed";
		goto fail;
	}

	audio_decoder_context = avcodec_alloc_context3(audio_codec);
	if(!audio_decoder_context) {
		result.error_description = "avcodec_alloc_context3() failed";
		goto fail;
	}

	ffmpeg_result = avcodec_parameters_to_context(audio_decoder_context, audio_format_context->streams[0]->codecpar);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_parameters_to_context() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}

	ffmpeg_result = avcodec_open2(audio_decoder_context, audio_codec, NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_open2() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}

	swr_context = swr_alloc();
	if (!swr_context) {
		result.error_description = "swr_alloc() failed ";
		goto fail;
	}
	if (!swr_alloc_set_opts(swr_context, av_get_default_channel_layout(audio_decoder_context->channels), AV_SAMPLE_FMT_S16, audio_decoder_context->sample_rate,
		av_get_default_channel_layout(audio_decoder_context->channels), audio_decoder_context->sample_fmt, audio_decoder_context->sample_rate, 0, NULL))
	{
		result.error_description = "swr_alloc_set_opts() failed ";
		goto fail;
	}

	ffmpeg_result = swr_init(swr_context);
	if (ffmpeg_result != 0) {
		result.error_description = "swr_init() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	// read the first packet
	result = read_audio_packet();
	if (result.code != 0) goto fail;
	
	return result;
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::init(NetworkStreamCacherData *video_cacher, NetworkStreamCacherData *audio_cacher) {
	Result_with_string result;
	result = init_(video_cacher, video_format_context, video_io_context, AVMEDIA_TYPE_VIDEO);
	if (result.code != 0) {
		result.error_description = "[video] " + result.error_description;
		return result;
	}
	result = init_(audio_cacher, audio_format_context, audio_io_context, AVMEDIA_TYPE_AUDIO);
	if (result.code != 0) {
		result.error_description = "[audio] " + result.error_description;
		return result;
	}
	
	result = init_video_decoder();
	if (result.code != 0) {
		result.error_description = "[video] " + result.error_description;
		return result;
	}
	result = init_audio_decoder();
	if (result.code != 0) {
		result.error_description = "[audio] " + result.error_description;
		return result;
	}
	
	video_tmp_frames = fixed_capacity_queue<AVFrame *>(20);
	return result;
}
NetworkDecoder::VideoFormatInfo NetworkDecoder::get_video_info() {
	VideoFormatInfo res;
	res.width = video_decoder_context->width;
	res.height = video_decoder_context->height;
	res.framerate = (double) video_format_context->streams[0]->avg_frame_rate.num / video_format_context->streams[0]->avg_frame_rate.den;
	res.format_name = video_codec->long_name;
	res.duration = (double) video_format_context->duration / AV_TIME_BASE;
	return res;
}
NetworkDecoder::AudioFormatInfo NetworkDecoder::get_audio_info() {
	AudioFormatInfo res;
	res.bitrate = audio_decoder_context->bit_rate;
	res.sample_rate = audio_decoder_context->sample_rate;
	res.ch = audio_decoder_context->channels;
	res.format_name = audio_codec->long_name;
	res.duration = (double) audio_format_context->duration / AV_TIME_BASE;
	return res;
}
Result_with_string NetworkDecoder::read_packet(AVFormatContext *format_context, AVPacket *&packet) {
	Result_with_string result;
	int ffmpeg_result;
	
	packet = av_packet_alloc();
	if (!packet) {
		result.error_description = "av_packet_alloc() failed";
		goto fail;
	}
	ffmpeg_result = av_read_frame(format_context, packet);
	if (ffmpeg_result != 0) {
		result.error_description = "av_read_frame() failed";
		goto fail;
	}
	return result;
	
	fail :
	av_packet_free(&packet); // this sets 'packet' to NULL
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
std::string NetworkDecoder::next_decode_type() {
	if (!audio_tmp_packet && !video_tmp_packet) return "EOF";
	if (!audio_tmp_packet) return "video";
	if (!video_tmp_packet) return "audio";
	double video_dts = video_tmp_packet->dts * av_q2d(video_format_context->streams[0]->time_base);
	double audio_dts = audio_tmp_packet->dts * av_q2d(audio_format_context->streams[0]->time_base);
	return video_dts <= audio_dts ? "video" : "audio";
}
Result_with_string NetworkDecoder::decode_video(int *width, int *height, bool *key_frame, double *cur_pos) {
	Result_with_string result;
	int ffmpeg_result = 0;
	
	if (video_tmp_frames.full()) {
		result.code = DEF_ERR_NEED_MORE_OUTPUT;
		return result;
	}
	
	*width = 0;
	*height = 0;
	
	*key_frame = (video_tmp_packet->flags & AV_PKT_FLAG_KEY);
	
	AVFrame *cur_frame = av_frame_alloc();
	if (!cur_frame) {
		result.error_description = "av_frame_alloc() failed";
		goto fail;
	}
	
	ffmpeg_result = avcodec_send_packet(video_decoder_context, video_tmp_packet);
	if(ffmpeg_result == 0) {
		ffmpeg_result = avcodec_receive_frame(video_decoder_context, cur_frame);
		if(ffmpeg_result == 0) {
			*width = cur_frame->width;
			*height = cur_frame->height;
			double time_base = av_q2d(video_format_context->streams[0]->time_base);
			if (cur_frame->pts != AV_NOPTS_VALUE) *cur_pos = cur_frame->pts * time_base;
			else *cur_pos = cur_frame->pkt_dts * time_base;
			video_tmp_frames.push(cur_frame);
		} else {
			result.error_description = "avcodec_receive_frame() failed " + std::to_string(ffmpeg_result);
			goto fail;
		}
	} else {
		result.error_description = "avcodec_send_packet() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	av_packet_free(&video_tmp_packet);
	return result;
	
	fail:
	
	av_packet_free(&video_tmp_packet);
	av_frame_free(&cur_frame);
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
Result_with_string NetworkDecoder::decode_audio(int *size, u8 **data, double *cur_pos) {
	int ffmpeg_result = 0;
	Result_with_string result;
	*size = 0;
	double time_base = av_q2d(audio_format_context->streams[0]->time_base);
	if (audio_tmp_packet->pts != AV_NOPTS_VALUE) *cur_pos = audio_tmp_packet->pts * time_base;
	else *cur_pos = audio_tmp_packet->dts * time_base;
	
	AVFrame *cur_frame = av_frame_alloc();
	if (!cur_frame) {
		result.error_description = "av_frame_alloc() failed";
		goto fail;
	}
	
	ffmpeg_result = avcodec_send_packet(audio_decoder_context, audio_tmp_packet);
	if(ffmpeg_result == 0) {
		ffmpeg_result = avcodec_receive_frame(audio_decoder_context, cur_frame);
		if(ffmpeg_result == 0) {
			*data = (u8 *) malloc(cur_frame->nb_samples * 2 * audio_decoder_context->channels);
			*size = swr_convert(swr_context, data, cur_frame->nb_samples, (const u8 **) cur_frame->data, cur_frame->nb_samples);
			*size *= 2;
		} else {
			result.error_description = "avcodec_receive_frame() failed " + std::to_string(ffmpeg_result);
			goto fail;
		}
	} else {
		result.error_description = "avcodec_send_packet() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}

	av_packet_free(&audio_tmp_packet);
	av_frame_free(&cur_frame);
	return result;
	
	fail:
	
	av_packet_free(&audio_tmp_packet);
	av_frame_free(&cur_frame);
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::get_decoded_video_frame(int width, int height, u8** data, double *cur_pos) {
	Result_with_string result;
	
	if (video_tmp_frames.empty()) {
		result.code = DEF_ERR_NEED_MORE_INPUT;
		return result;
	}
	AVFrame *cur_frame = NULL;
	video_tmp_frames.pop(cur_frame);
	
	
	int cpy_size[2] = { 0, 0, };

	cpy_size[0] = (width * height);
	cpy_size[1] = cpy_size[0] / 4;
	cpy_size[0] -= cpy_size[0] % 32;
	cpy_size[1] -= cpy_size[1] % 32;
	*data = (u8 *) malloc(width * height * 1.5);
	if (!data) {
		av_frame_free(&cur_frame);
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	
	memcpy_asm(*data, cur_frame->data[0], cpy_size[0]);
	memcpy_asm(*data + (width * height), cur_frame->data[1], cpy_size[1]);
	memcpy_asm(*data + (width * height) + (width * height / 4), cur_frame->data[2], cpy_size[1]);
	
	double time_base = av_q2d(video_format_context->streams[0]->time_base);
	if (cur_frame->pts != AV_NOPTS_VALUE) *cur_pos = cur_frame->pts * time_base;
	else *cur_pos = cur_frame->pkt_dts * time_base;
	
	av_frame_free(&cur_frame);
	return result;
}

Result_with_string NetworkDecoder::seek(s64 microseconds) {
	Result_with_string result;
	
	// clear buffer
	av_packet_free(&video_tmp_packet);
	av_packet_free(&audio_tmp_packet);
	for (auto i : video_tmp_frames.clear()) av_frame_free(&i);
	
	int ffmpeg_result = avformat_seek_file(video_format_context, -1, microseconds - 1000000, microseconds, microseconds + 1000000, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ???
	if(ffmpeg_result < 0) {
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
		result.error_description = "avformat_seek_file() for video failed " + std::to_string(ffmpeg_result);
		return result;
	}
	avcodec_flush_buffers(video_decoder_context);
	// refill the next packets
	result = read_video_packet();
	if (result.code != 0) return result;
	
	// once successfully sought video, perform an exact seek on audio
	if (video_tmp_packet) {
		double time_base = av_q2d(video_format_context->streams[0]->time_base);
		if (video_tmp_packet->pts != AV_NOPTS_VALUE) microseconds = video_tmp_packet->pts * time_base * 1000000;
		else microseconds = video_tmp_packet->dts * time_base * 1000000;
	}
	ffmpeg_result = avformat_seek_file(audio_format_context, -1, microseconds, microseconds, microseconds, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ???
	if(ffmpeg_result < 0) {
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
		result.error_description = "avformat_seek_file() for audio failed " + std::to_string(ffmpeg_result);
		return result;
	}
	avcodec_flush_buffers(audio_decoder_context);
	result = read_audio_packet();
	if (result.code != 0) return result;
	return result;
}
