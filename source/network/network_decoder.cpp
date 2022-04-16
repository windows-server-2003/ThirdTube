#include "headers.hpp"
#include "network/network_decoder.hpp"
#include "network/network_downloader.hpp"
#include <numeric>

extern "C" void memcpy_asm(u8*, u8*, int);


/* --------------------------------------------------------- */
/*                  NetworkDecoderFFmpegIOData               */
/* --------------------------------------------------------- */
void NetworkDecoderFFmpegIOData::deinit_(int type, bool deinit_stream) {
	delete opaque[type];
	opaque[type] = NULL;
	if (io_context[type]) av_freep(&io_context[type]->buffer);
	av_freep(&io_context[type]);
	avformat_close_input(&format_context[type]);
	if (deinit_stream) {
		network_stream[type]->quit_request = true;
		network_stream[type] = NULL;
	}
}
void NetworkDecoderFFmpegIOData::deinit(bool deinit_stream) {
	for (int type = 0; type < 2; type++) deinit_(type, deinit_stream);
}
static int read_network_stream(void *opaque, u8 *buf, int buf_size_) { // size or AVERROR_EOF
	NetworkDecoder *decoder = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->first;
	NetworkStream *stream = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->second;
	size_t buf_size = buf_size_;
	
	// Util_log_save("dec", "read " + std::to_string(stream->read_head) + " " + std::to_string(buf_size_) + " " + std::to_string(stream->len));
	bool cpu_limited = false;
	while (!stream->ready || !stream->is_data_available(stream->read_head, std::min<u64>(buf_size, stream->len - stream->read_head))) {
		if (stream->ready && stream->read_head >= stream->len) {
			Util_log_save("dec", "read beyond eof : " + std::to_string(stream->read_head) + " " + std::to_string(stream->len));
			goto fail; // beyond the eof
		}
		if (!stream->disable_interrupt && decoder->interrupt) {
			Util_log_save("dec", "read interrupt");
			decoder->need_reinit = true;
			goto fail;
		}
		stream->network_waiting_status = "Reading stream";
		if (!cpu_limited) {
			cpu_limited = true;
			add_cpu_limit(ADDITIONAL_CPU_LIMIT);
		}
		usleep(20000);
		if (stream->error || stream->quit_request) {
			Util_log_save("dec", "read dead stream : " + std::string(stream->error ? "error" : "quitted"));
			usleep(100000);
			goto fail;
		}
	}
	if (cpu_limited) {
		cpu_limited = false;
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}
	stream->network_waiting_status = NULL;
	
	
	{
		auto tmp = stream->get_data(stream->read_head, std::min<u64>(buf_size, stream->len - stream->read_head));
		size_t read_size = tmp.size();
		stream->read_head += read_size;
		memcpy(buf, &tmp[0], read_size);
		if (!read_size) return AVERROR_EOF;
		return read_size;
	}
	
	fail :
	if (cpu_limited) {
		cpu_limited = false;
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}
	stream->network_waiting_status = NULL;
	return AVERROR_EOF;
}
static int64_t seek_network_stream(void *opaque, s64 offset, int whence) { // size or AVERROR_EOF
	NetworkDecoder *decoder = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->first;
	NetworkStream *stream = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->second;
	
	(void) decoder;
	
	while (!stream->ready) {
		stream->network_waiting_status = "Reading stream (init, seek)";
		usleep(20000);
		if (stream->error || stream->quit_request) {
			stream->network_waiting_status = NULL;
			return -1;
		}
	}
	stream->network_waiting_status = NULL;
	
	if (whence == AVSEEK_SIZE) {
		// Util_log_save("dec", "inquire size : " + std::to_string(stream->len));
		return stream->len;
	}
	
	u64 new_pos = 0;
	if (whence == SEEK_SET) new_pos = offset;
	else if (whence == SEEK_CUR) new_pos = stream->read_head + offset;
	else if (whence == SEEK_END) new_pos = stream->len + offset;
	
	// Util_log_save("dec", "seek " + std::to_string(new_pos) + " " + std::to_string(stream->len));
	
	if (new_pos > stream->len) return -1;
	
	stream->read_head = new_pos;
	
	return stream->read_head;
}

void ffmpeg_log_callback(void *, int level, const char *fmt, va_list vargs) {
	char buf[256] = { 0 };
	vsnprintf(buf, 256, fmt, vargs);
	if (level <= AV_LOG_WARNING) Util_log_save("ffmpeg", buf);
}

#define NETWORK_BUFFER_SIZE 0x10000
Result_with_string NetworkDecoderFFmpegIOData::init_(int type, NetworkDecoder *parent_decoder) {
	Result_with_string result;
	int ffmpeg_result;
	
	network_stream[type]->read_head = 0;
	
	opaque[type] = new std::pair<NetworkDecoder *, NetworkStream *>(parent_decoder, network_stream[type]);
	unsigned char *buffer = (unsigned char *) av_malloc(NETWORK_BUFFER_SIZE);
	if (!buffer) {
		result.error_description = "network buffer allocation failed";
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	io_context[type] = avio_alloc_context(buffer, NETWORK_BUFFER_SIZE, 0, opaque[type], read_network_stream, NULL, seek_network_stream);
	if (!io_context[type]) {
		result.error_description = "IO context allocation failed";
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	format_context[type] = avformat_alloc_context();
	if (!format_context[type]) {
		result.error_description = "format context allocation failed";
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		return result;
	}
	format_context[type]->pb = io_context[type];
	ffmpeg_result = avformat_open_input(&format_context[type], "yay", NULL, NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avformat_open_input() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	ffmpeg_result = avformat_find_stream_info(format_context[type], NULL);
	if (!format_context[type]) {
		result.error_description = "avformat_find_stream_info() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	if (video_audio_seperate) {
		if (format_context[type]->nb_streams != 1) {
			result.error_description = "nb_streams != 1 : " + std::to_string(format_context[type]->nb_streams);
			goto fail;
		} else if (format_context[type]->streams[0]->codecpar->codec_type != (type == VIDEO ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO)) {
			result.error_description = "stream type wrong : " + std::to_string(format_context[type]->streams[0]->codecpar->codec_type);
			goto fail;
		}
		stream_index[type] = 0;
	} else {
		stream_index[VIDEO] = stream_index[AUDIO] = -1;
		for (size_t i = 0; i < format_context[BOTH]->nb_streams; i++) {
			if (format_context[BOTH]->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) stream_index[VIDEO] = i;
			if (format_context[BOTH]->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) stream_index[AUDIO] = i;
		}
	}
	return result;
	
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
#	define RETURN_WITH_PREFIX_ON_ERROR(exp, prefix) \
	do {\
		if ((result = exp).code != 0) {\
			result.error_description = prefix + result.error_description;\
			return result;\
		}\
	} while (0)
Result_with_string NetworkDecoderFFmpegIOData::init(NetworkStream *video_stream, NetworkStream *audio_stream, NetworkDecoder *parent_decoder) {
	Result_with_string result;
	
	video_audio_seperate = video_stream != audio_stream;
	network_stream[VIDEO] = video_stream;
	network_stream[AUDIO] = audio_stream;
	this->parent_decoder = parent_decoder;
	
	
	// init io
	if (video_audio_seperate) {
		RETURN_WITH_PREFIX_ON_ERROR(init_(VIDEO, parent_decoder), "[v] ");
		RETURN_WITH_PREFIX_ON_ERROR(init_(AUDIO, parent_decoder), "[a] ");
	} else RETURN_WITH_PREFIX_ON_ERROR(init_(VIDEO, parent_decoder), "[v+a] ");
	
	if (stream_index[VIDEO] == -1) audio_only = true;
	if (stream_index[AUDIO] == -1) {
		result.error_description = "audio stream not found";
		result.code = -1;
		return result;
	}
	
	return result;
}
Result_with_string NetworkDecoderFFmpegIOData::init(NetworkStream *both_stream, NetworkDecoder *parent_decoder) {
	return init(both_stream, both_stream, parent_decoder);
}
Result_with_string NetworkDecoderFFmpegIOData::reinit() {
	deinit(false);
	return init(network_stream[VIDEO], network_stream[AUDIO], parent_decoder);
}
Result_with_string NetworkDecoderFFmpegIOData::reinit_stream(int type, int64_t seek_timestamp) {
	Result_with_string result;
	int ffmpeg_result;
	AVPacket *test_packet = NULL;
	
	int log_num = Util_log_save("debug", "avformat reinit #" + std::to_string(type) + "...");
	deinit_(type, false);
	RETURN_WITH_PREFIX_ON_ERROR(init_(type, parent_decoder), video_audio_seperate ? "[v+a]" : type == VIDEO ? "[v]" : "[a]");
	
	ffmpeg_result = av_seek_frame(format_context[type], stream_index[type], seek_timestamp, 0);
	if (ffmpeg_result != 0) {
		result.error_description = "av_seek_frame() failed : " + std::to_string(ffmpeg_result);
		goto ffmpeg_fail;
	}
	
	// the dts of the first packet read from the new format_context should be equal to seek_timestamp
	test_packet = av_packet_alloc();
	ffmpeg_result = av_read_frame(format_context[type], test_packet);
	if (ffmpeg_result != 0) {
		result.error_description = "av_read_frame() failed : " + std::to_string(ffmpeg_result);
		goto ffmpeg_fail;
	}
	if (test_packet->dts != seek_timestamp) Util_log_add(log_num, "warning : " + std::to_string(seek_timestamp) + " -> " + std::to_string(test_packet->dts));
	else Util_log_add(log_num, "ok");
	goto end;
	
	ffmpeg_fail :
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	
	end :
	av_packet_free(&test_packet);
	return result;
}
double NetworkDecoderFFmpegIOData::get_duration() {
	return (double) format_context[video_audio_seperate ? AUDIO : BOTH]->duration / AV_TIME_BASE;
}






/* ********************************************************* */
/*                  NetworkDecoderFilterData                 */
/* ********************************************************* */

void NetworkDecoderFilterData::deinit() {
	avfilter_graph_free(&audio_filter_graph);
	av_frame_free(&output_frame);
	audio_filter_src = audio_filter_sink = NULL;
}
Result_with_string NetworkDecoderFilterData::init(AVCodecContext *audio_context, double volume, double tempo, double pitch) {
	Result_with_string result;
	int ffmpeg_result = 0;
	
	{
		// av_log_set_level(AV_LOG_ERROR);
		// av_log_set_callback(ffmpeg_log_callback);
		
		output_frame = av_frame_alloc();
		if (!output_frame) {
			result.error_description = "av_frame_alloc() failed";
			goto fail;
		}
		
		audio_filter_graph = avfilter_graph_alloc();
		if (!audio_filter_graph) {
			result.error_description = "avfilter_graph_alloc() failed ";
			goto fail;
		}
		const AVFilter *abuffer = avfilter_get_by_name("abuffer");
		const AVFilter *atempo = avfilter_get_by_name("atempo");
		const AVFilter *asetrate = avfilter_get_by_name("asetrate");
		const AVFilter *aecho = avfilter_get_by_name("aecho");
		const AVFilter *volume_filter = avfilter_get_by_name("volume");
		const AVFilter *aformat = avfilter_get_by_name("aformat");
		const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
		
		if (!abuffer || !atempo || !aecho || !volume_filter || !aformat || !abuffersink) {
			std::string missing = !abuffer ? "abuffer" : !atempo ? "atempo" : !volume_filter ? "volume" : !aformat ? "aformat" : !abuffersink ? "abuffersink" : "[ERR]";
			result.error_description = "filter \"" + missing + "\" not found";
			goto fail;
		}
		std::vector<AVFilterContext *> filter_sequence;
		char option_buffer[256] = { 0 };
		// abuffer (source)
		snprintf(option_buffer, 256, "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64, 
			audio_context->time_base.num, audio_context->time_base.den, audio_context->sample_rate,
			av_get_sample_fmt_name(audio_context->sample_fmt), audio_context->channel_layout);
		ffmpeg_result = avfilter_graph_create_filter(&audio_filter_src, abuffer, NULL, option_buffer, NULL, audio_filter_graph);
		if (ffmpeg_result < 0) {
			result.error_description = "abuffer creation failed";
			goto fail;
		}
		filter_sequence.push_back(audio_filter_src);
		// asetrate (simultaneous tempo/pitch shift)
		if (std::abs(1.0 - pitch) >= 0.01) {
			tempo /= pitch;
			AVFilterContext *audio_pitch_filter;
			snprintf(option_buffer, 256, "sample_rate=%f", audio_context->sample_rate * pitch);
			ffmpeg_result = avfilter_graph_create_filter(&audio_pitch_filter, asetrate, NULL, option_buffer, NULL, audio_filter_graph);
			if (ffmpeg_result < 0) {
				result.error_description = "asetrate creation failed";
				goto fail;
			}
			filter_sequence.push_back(audio_pitch_filter);
		}
		
		// atempo (tempo filter)
		// atempo does not support a tempo lower than 0.5, so chain multiple atempo filters in that case
		while (std::abs(1.0 - tempo) >= 0.01) {
			double cur_tempo = std::max(0.5, tempo);
			tempo /= cur_tempo;
			AVFilterContext *audio_tempo_filter;
			snprintf(option_buffer, 256, "tempo=%f", cur_tempo);
			ffmpeg_result = avfilter_graph_create_filter(&audio_tempo_filter, atempo, NULL, option_buffer, NULL, audio_filter_graph);
			if (ffmpeg_result < 0) {
				result.error_description = "atempo creation failed";
				goto fail;
			}
			filter_sequence.push_back(audio_tempo_filter);
		}
		// aecho (echo filter)
		/*
		snprintf(option_buffer, 256, "");
		Util_log_save("decoder", std::string("aecho : ") + option_buffer);
		ffmpeg_result = avfilter_graph_create_filter(&audio_echo_filter, aecho, NULL, option_buffer, NULL, audio_filter_graph);
		if (ffmpeg_result < 0) {
			result.error_description = "aecho creation failed";
			goto fail;
		}*/
	
		// volume
		if (std::abs(1.0 - volume) >= 0.01) {
			AVFilterContext *audio_volume_filter;
			snprintf(option_buffer, 256, "volume=%f", volume);
			ffmpeg_result = avfilter_graph_create_filter(&audio_volume_filter, volume_filter, NULL, option_buffer, NULL, audio_filter_graph);
			if (ffmpeg_result < 0) {
				result.error_description = "volume creation failed";
				goto fail;
			}
			filter_sequence.push_back(audio_volume_filter);
		}
		// aformat
		AVFilterContext *audio_format_filter;
		snprintf(option_buffer, 256, "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%" PRIx64,
			av_get_sample_fmt_name(audio_context->sample_fmt), 44100, (uint64_t) AV_CH_LAYOUT_STEREO);
		ffmpeg_result = avfilter_graph_create_filter(&audio_format_filter, aformat, NULL, option_buffer, NULL, audio_filter_graph);
		if (ffmpeg_result < 0) {
			result.error_description = "aformat creation failed";
			goto fail;
		}
		filter_sequence.push_back(audio_format_filter);
		// abuffersink (sink)
		ffmpeg_result = avfilter_graph_create_filter(&audio_filter_sink, abuffersink, NULL, NULL, NULL, audio_filter_graph);
		if (ffmpeg_result < 0) {
			result.error_description = "abuffersink creation failed";
			goto fail;
		}
		filter_sequence.push_back(audio_filter_sink);
	
		// linking & configuring
		for (size_t i = 0; i + 1 < filter_sequence.size() && ffmpeg_result >= 0; i++)
			ffmpeg_result = avfilter_link(filter_sequence[i], 0, filter_sequence[i + 1], 0);
		if (ffmpeg_result < 0) {
			result.error_description = "filter graph linking failed";
			goto fail;
		}
		ffmpeg_result = avfilter_graph_config(audio_filter_graph, NULL);
		if (ffmpeg_result < 0) {
			result.error_description = "avfilter_graph_config() failed : " + std::to_string(ffmpeg_result);
			goto fail;
		}
	}
	
	return result;
	
	fail :
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
Result_with_string NetworkDecoderFilterData::process_audio_frame(AVFrame *input) {
	Result_with_string result;
	int ffmpeg_result = 0;
	
	ffmpeg_result = av_buffersrc_write_frame(audio_filter_src, input);
	if (ffmpeg_result < 0) {
		result.error_description = "av_buffersrc_write_frame() failed";
		goto fail;
	}
	
	av_frame_unref(output_frame);
	ffmpeg_result = av_buffersink_get_frame(audio_filter_sink, output_frame);
	if (ffmpeg_result < 0) {
		result.error_description = "av_buffersink_get_frame() failed";
		goto fail;
	}
	
	return result;
	
	fail :
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}






/* ********************************************************* */
/*                       NetworkDecoder                      */
/* ********************************************************* */

void NetworkDecoder::deinit() {
	ready = false;
	
	for (int type = 0; type < 2; type++) {
		for (auto i : packet_buffer[type]) av_packet_free(&i);
		packet_buffer[type].clear();
	}
	// for HW decoder
	for (auto i : video_mvd_tmp_frames.deinit()) free(i);
	linearFree_concurrent(mvd_frame);
	mvd_frame = NULL;
	buffered_pts_list.clear();
	// for SW decoder
	for (auto i : video_tmp_frames.deinit()) av_frame_free(&i);
	free(sw_video_output_tmp);
	sw_video_output_tmp = NULL;
	
	// its members should be freed by NetworkMultipleDecoder, not here
	// just to prevent use-after-free, we set the pointer to NULL
	io = NULL;
	
	// deinit decoder
	for (int type = 0; type < 2; type++) avcodec_free_context(&decoder_context[type]);
	swr_free(&swr_context);
}

Result_with_string NetworkDecoder::init_output_buffer(bool is_mvd) {
	Result_with_string result;
	int width, height;
	
	// init output buffer
	width = decoder_context[VIDEO]->width;
	height = decoder_context[VIDEO]->height;
	if (width % 16) width += 16 - width % 16;
	if (height % 16) height += 16 - height % 16;
	const size_t MAX_RAW_BUFFER_SIZE = var_is_new3ds ? NEW_MAX_RAW_BUFFER_SIZE : OLD_MAX_RAW_BUFFER_SIZE;
	if (is_mvd) {
		int buffer_size = MAX_RAW_BUFFER_SIZE / (width * height * 2);
		if (buffer_size <= 10) Util_log_save("decoder", "mvd buffer size too low:" + std::to_string(buffer_size));
		
		std::vector<u8 *> init(buffer_size);
		for (auto &i : init) {
			i = (u8 *) malloc(width * height * 2);
			if (!i) {
				result.error_description = "malloc() failed while preallocating ";
				goto fail;
			}
		}
		video_mvd_tmp_frames.init(init);
		
		mvd_frame = (u8 *) linearAlloc_concurrent(width * height * 2);
		if (!mvd_frame) {
			result.error_description = "malloc() failed while preallocating ";
			goto fail;
		}
	} else {
		int buffer_size = MAX_RAW_BUFFER_SIZE / (width * height * 1.5);
		if (buffer_size <= 10) Util_log_save("decoder", "buffer size too low:" + std::to_string(buffer_size));
		
		std::vector<AVFrame *> init(buffer_size);
		for (auto &i : init) {
			i = av_frame_alloc();
			if (!i) {
				result.error_description = "av_frame_alloc() failed while preallocating ";
				goto fail;
			}
		}
		video_tmp_frames.init(init);
		
		sw_video_output_tmp = (u8 *) malloc(width * height * 1.5);
		if (!sw_video_output_tmp) {
			result.error_description = "malloc() failed while preallocating ";
			goto fail;
		}
	}
	
	return result;
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
Result_with_string NetworkDecoder::init_decoder(int type) {
	Result_with_string result;
	int ffmpeg_result;
	
	// initialize decoder
	codec[type] = avcodec_find_decoder(get_stream(type)->codecpar->codec_id);
	if(!codec[type]) {
		result.error_description = "avcodec_find_decoder() failed";
		goto fail;
	}

	decoder_context[type] = avcodec_alloc_context3(codec[type]);
	if(!decoder_context[type]) {
		result.error_description = "avcodec_alloc_context3() failed";
		goto fail;
	}

	ffmpeg_result = avcodec_parameters_to_context(decoder_context[type], get_stream(type)->codecpar);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_parameters_to_context() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	if ((is_av_separate() ? (type == VIDEO) : (type == BOTH))) {
		decoder_context[type]->lowres = 0;
		decoder_context[type]->flags = AV_CODEC_FLAG_OUTPUT_CORRUPT;
		
		if (codec[type]->capabilities & AV_CODEC_CAP_FRAME_THREADS) decoder_context[type]->thread_type = FF_THREAD_FRAME;
		else if(codec[type]->capabilities & AV_CODEC_CAP_SLICE_THREADS) decoder_context[type]->thread_type = FF_THREAD_SLICE;
		else decoder_context[type]->thread_type = 0;
		
		if (decoder_context[type]->thread_type == FF_THREAD_FRAME) {
			Util_fake_pthread_set_enabled_core(frame_cores_enabled);
			decoder_context[type]->thread_count = std::accumulate(std::begin(frame_cores_enabled), std::end(frame_cores_enabled), 0);
		} else if (decoder_context[type]->thread_type == FF_THREAD_SLICE) {
			Util_fake_pthread_set_enabled_core(slice_cores_enabled);
			decoder_context[type]->thread_count = std::accumulate(std::begin(slice_cores_enabled), std::end(slice_cores_enabled), 0);
		} else decoder_context[type]->thread_count = 1;
		
		decoder_context[type]->thread_safe_callbacks = 1;
	}
	ffmpeg_result = avcodec_open2(decoder_context[type], codec[type], NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_open2() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	if (type == AUDIO) {
		swr_context = swr_alloc();
		if (!swr_context) {
			result.error_description = "swr_alloc() failed ";
			goto fail;
		}
		if (!swr_alloc_set_opts(swr_context, av_get_default_channel_layout(decoder_context[AUDIO]->channels), AV_SAMPLE_FMT_S16, decoder_context[AUDIO]->sample_rate,
			av_get_default_channel_layout(decoder_context[AUDIO]->channels), decoder_context[AUDIO]->sample_fmt, decoder_context[AUDIO]->sample_rate, 0, NULL))
		{
			result.error_description = "swr_alloc_set_opts() failed ";
			goto fail;
		}

		ffmpeg_result = swr_init(swr_context);
		if (ffmpeg_result != 0) {
			result.error_description = "swr_init() failed " + std::to_string(ffmpeg_result);
			goto fail;
		}
	}
	return result;
	
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::init(bool request_hw_decoder) {
	Result_with_string result;
	
	hw_decoder_enabled = request_hw_decoder;
	interrupt = false;
	
	// init decoder
	if (!is_audio_only()) RETURN_WITH_PREFIX_ON_ERROR(init_decoder(VIDEO), "[v] ");
	RETURN_WITH_PREFIX_ON_ERROR(init_decoder(AUDIO), "[a] ");
	
	// init buffers based on decoder info
	if (!is_audio_only()) {
		result = init_output_buffer(request_hw_decoder);
		if (result.code != 0) {
			result.error_description = "[out buf] " + result.error_description;
			return result;
		}
	}
	
	mvd_first = true;
	ready = true;
	return result;
}
void NetworkDecoder::clear_buffer() {
	for (int type = 0; type < 2; type++) {
		for (auto i : packet_buffer[type]) av_packet_free(&i);
		packet_buffer[type].clear();
	}
	video_mvd_tmp_frames.clear();
	video_tmp_frames.clear();
	buffered_pts_list.clear();
}

NetworkDecoder::VideoFormatInfo NetworkDecoder::get_video_info() {
	VideoFormatInfo res;
	if (is_audio_only()) {
		res.width = res.height = res.framerate = res.duration = 0;
		res.format_name = "N/A";
	} else {
		res.width = decoder_context[VIDEO]->width;
		res.height = decoder_context[VIDEO]->height;
		res.framerate = av_q2d(get_stream(VIDEO)->avg_frame_rate);
		res.format_name = codec[VIDEO]->long_name;
		res.duration = (double) io->format_context[is_av_separate() ? VIDEO : BOTH]->duration / AV_TIME_BASE;
	}
	return res;
}
NetworkDecoder::AudioFormatInfo NetworkDecoder::get_audio_info() {
	AudioFormatInfo res;
	res.bitrate = decoder_context[AUDIO]->bit_rate;
	res.sample_rate = decoder_context[AUDIO]->sample_rate;
	res.ch = decoder_context[AUDIO]->channels;
	res.format_name = codec[AUDIO]->long_name;
	res.duration = (double) io->format_context[is_av_separate() ? AUDIO : BOTH]->duration / AV_TIME_BASE;
	return res;
}
std::vector<std::pair<double, std::vector<double> > > NetworkDecoder::get_buffering_progress_bars(int bar_len) {
	std::vector<std::pair<double, std::vector<double> > > res;
	if (is_av_separate()) {
		if (io->network_stream[VIDEO] && !io->network_stream[VIDEO]->quit_request)
			res.push_back({(double) io->network_stream[VIDEO]->read_head / io->network_stream[VIDEO]->len, io->network_stream[VIDEO]->get_buffering_progress_bar(bar_len)});
		else res.push_back({0, {}});
		if (io->network_stream[AUDIO] && !io->network_stream[AUDIO]->quit_request)
			res.push_back({(double) io->network_stream[AUDIO]->read_head / io->network_stream[AUDIO]->len, io->network_stream[AUDIO]->get_buffering_progress_bar(bar_len)});
		else res.push_back({0, {}});
	} else {
		if (io->network_stream[BOTH] && !io->network_stream[BOTH]->quit_request)
			res.push_back({(double) io->network_stream[BOTH]->read_head / io->network_stream[BOTH]->len, io->network_stream[BOTH]->get_buffering_progress_bar(bar_len)});
		else res.push_back({0, {}});
	}
	return res;
}

Result_with_string NetworkDecoder::read_packet(int type) {
	Result_with_string result;
	int ffmpeg_result;
	
	AVPacket *tmp_packet = av_packet_alloc();
	if (!tmp_packet) {
		result.code = DEF_ERR_OUT_OF_MEMORY;
		result.string = DEF_ERR_OUT_OF_MEMORY_STR;
		result.error_description = "av_packet_alloc() failed";
		return result;
	}
	
	ffmpeg_result = av_read_frame(io->format_context[type], tmp_packet);
	if (ffmpeg_result != 0) {
		result.error_description = "av_read_frame() failed";
		goto ffmpeg_fail;
	}
	if (!tmp_packet->buf) Util_log_save("debug", "!!!!!!!!!!!!!!!!!!! --------------------- !!!!!!!!!!!!!!!!!!!");
	
	if (--io->packets_until_next_reinit <= 0) {
		for (int i = 0; i < 2; i++) avformat_reinit_request[i] = true;
		io->packets_until_next_reinit = DECODER_REINIT_INTERVAL_PACKETS;
	}
	if (is_av_separate() && avformat_reinit_request[type] && tmp_packet->flags & AV_PKT_FLAG_KEY) {
		result = io->reinit_stream(type, tmp_packet->dts);
		avformat_reinit_request[type] = false;
	}
	if (!is_av_separate() && avformat_reinit_request[VIDEO] && tmp_packet->stream_index == io->stream_index[VIDEO] && tmp_packet->flags & AV_PKT_FLAG_KEY) {
		result = io->reinit_stream(VIDEO, tmp_packet->dts);
		avformat_reinit_request[VIDEO] = avformat_reinit_request[AUDIO] = false;
	}
	if (result.code != 0) goto reinit_fail;
	{
		int packet_type = is_av_separate() ? type : (tmp_packet->stream_index == io->stream_index[VIDEO] ? VIDEO : AUDIO);
		packet_buffer[packet_type].push_back(tmp_packet);
		return result;
	}
	
	ffmpeg_fail :
	av_packet_free(&tmp_packet);
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
	reinit_fail :
	Util_log_save("debug", "avformat reinit fail : " + result.error_description);
	return result;
}
NetworkDecoder::PacketType NetworkDecoder::next_decode_type() {
	if (is_av_separate()) {
		for (int type = 0; type < 2; type++) if (!packet_buffer[type].size())
			read_packet(type);
	} else {
		while ((!is_audio_only() && !packet_buffer[VIDEO].size()) || !packet_buffer[AUDIO].size()) {
			Result_with_string result = read_packet(BOTH);
			if (result.code != 0) break;
		}
	}
	if (!packet_buffer[VIDEO].size() && !packet_buffer[AUDIO].size()) return PacketType::EoF;
	if (!packet_buffer[AUDIO].size()) return PacketType::VIDEO;
	if (!packet_buffer[VIDEO].size()) return PacketType::AUDIO;
	double video_dts = packet_buffer[VIDEO][0]->dts * av_q2d(get_stream(VIDEO)->time_base);
	double audio_dts = packet_buffer[AUDIO][0]->dts * av_q2d(get_stream(AUDIO)->time_base);
	return video_dts <= audio_dts ? PacketType::VIDEO : PacketType::AUDIO;
}
static std::string debug_str = "";
Result_with_string NetworkDecoder::mvd_decode(int *width, int *height) {
	Result_with_string result;
	
	*width = decoder_context[VIDEO]->width;
	*height = decoder_context[VIDEO]->height;
	if (*width % 16 != 0) *width += 16 - *width % 16;
	if (*height % 16 != 0) *height += 16 - *height % 16;
	
	MVDSTD_Config config;
	mvdstdGenerateDefaultConfig(&config, *width, *height, *width, *height, NULL, NULL, NULL);
	
	int offset = 0;
	int source_offset = 0;

	AVPacket *packet_read = packet_buffer[VIDEO][0];
	u8 *mvd_packet = (u8 *) linearAlloc_concurrent(packet_read->size);
	if(mvd_first)
	{
		//set extra data

		offset = 0;
		memset(mvd_packet, 0x0, 0x2);
		offset += 2;
		memset(mvd_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(mvd_packet + offset, decoder_context[VIDEO]->extradata + 8, *(decoder_context[VIDEO]->extradata + 7));
		offset += *(decoder_context[VIDEO]->extradata + 7);

		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		if (!MVD_CHECKNALUPROC_SUCCESS(result.code)) Util_log_save("mvd", "0 : mvdstdProcessVideoFrame() : " + std::to_string(result.code));

		offset = 0;
		memset(mvd_packet, 0x0, 0x2);
		offset += 2;
		memset(mvd_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(mvd_packet + offset, decoder_context[VIDEO]->extradata + 11 + *(decoder_context[VIDEO]->extradata + 7), *(decoder_context[VIDEO]->extradata + 10 + *(decoder_context[VIDEO]->extradata + 7)));
		offset += *(decoder_context[VIDEO]->extradata + 10 + *(decoder_context[VIDEO]->extradata + 7));
		
		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		if (!MVD_CHECKNALUPROC_SUCCESS(result.code)) Util_log_save("mvd", "1 : mvdstdProcessVideoFrame() : " + std::to_string(result.code));
	}
	
	offset = 0;
	source_offset = 0;

	while(source_offset + 4 < packet_read->size)
	{
		//get nal size
		int size = *((int*)(packet_read->data + source_offset));
		size = __builtin_bswap32(size);
		source_offset += 4;

		//set nal prefix 0x0 0x0 0x1
		memset(mvd_packet + offset, 0x0, 0x2);
		offset += 2;
		memset(mvd_packet + offset, 0x1, 0x1);
		offset += 1;

		//copy raw nal data
		memcpy(mvd_packet + offset, (packet_read->data + source_offset), size);
		offset += size;
		source_offset += size;
	}
	
	config.physaddr_outdata0 = osConvertVirtToPhys(mvd_frame);
	
	result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
	
	if(mvd_first)
	{
		//Do I need to send same nal data at first frame?
		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		if (!MVD_CHECKNALUPROC_SUCCESS(result.code)) Util_log_save("mvd", "1 : mvdstdProcessVideoFrame() : " + std::to_string(result.code));
	}

	if (MVD_CHECKNALUPROC_SUCCESS(result.code)) {
		double cur_pos;
		double time_base = av_q2d(get_stream(VIDEO)->time_base);
		if (packet_read->pts != AV_NOPTS_VALUE) cur_pos = packet_read->pts * time_base;
		else cur_pos = packet_read->dts * time_base;
		
		buffered_pts_list_lock.lock();
		buffered_pts_list.insert(cur_pos + timestamp_offset);
		buffered_pts_list_lock.unlock();
	}
	if (result.code == MVD_STATUS_FRAMEREADY) {
		result.code = 0;
		mvdstdRenderVideoFrame(&config, true);
		
		if (!mvd_first) { // when changing video, it somehow outputs a frame of previous video, so ignore the first one
			memcpy_asm(video_mvd_tmp_frames.get_next_pushed(), mvd_frame, (*width * *height * 2) / 32 * 32);
			video_mvd_tmp_frames.push();
		}
	} else Util_log_save("", "mvdstdProcessVideoFrame()...", result.code);
	
	mvd_first = false;
	linearFree_concurrent(mvd_packet);
	mvd_packet = NULL;
	av_packet_free(&packet_read);
	packet_buffer[VIDEO].pop_front();
	// refill the packet buffer
	while (!packet_buffer[VIDEO].size() && read_packet(is_av_separate() ? VIDEO : BOTH).code == 0);
	
	return result;
}
Result_with_string NetworkDecoder::decode_video(int *width, int *height, bool *key_frame, double *cur_pos) {
	Result_with_string result;
	int ffmpeg_result = 0;
	
	AVPacket *packet_read = packet_buffer[VIDEO][0];
	*key_frame = (packet_read->flags & AV_PKT_FLAG_KEY);
	
	if (hw_decoder_enabled) {
		if (video_mvd_tmp_frames.full()) {
			result.code = DEF_ERR_NEED_MORE_OUTPUT;
			return result;
		}
		double time_base = av_q2d(get_stream(VIDEO)->time_base);
		if (packet_read->pts != AV_NOPTS_VALUE) *cur_pos = packet_read->pts * time_base;
		else *cur_pos = packet_read->dts * time_base;
		*cur_pos += timestamp_offset;
		
		auto tmp = mvd_decode(width, height);
		return tmp;
	}
	
	if (video_tmp_frames.full()) {
		result.code = DEF_ERR_NEED_MORE_OUTPUT;
		return result;
	}
	
	*width = 0;
	*height = 0;
	
	
	AVFrame *cur_frame = video_tmp_frames.get_next_pushed();
	
	ffmpeg_result = avcodec_send_packet(decoder_context[VIDEO], packet_read);
	if(ffmpeg_result == 0) {
		ffmpeg_result = avcodec_receive_frame(decoder_context[VIDEO], cur_frame);
		if (ffmpeg_result == 0) {
			*width = cur_frame->width;
			*height = cur_frame->height;
			
			double time_base = av_q2d(get_stream(VIDEO)->time_base);
			if (cur_frame->pts != AV_NOPTS_VALUE) *cur_pos = cur_frame->pts * time_base;
			else *cur_pos = cur_frame->pkt_dts * time_base;
			*cur_pos += timestamp_offset;
			
			buffered_pts_list_lock.lock();
			buffered_pts_list.insert(*cur_pos);
			buffered_pts_list_lock.unlock();
			
			video_tmp_frames.push();
		} else if (ffmpeg_result == AVERROR(EAGAIN)) result.code = DEF_ERR_NEED_MORE_INPUT;
		else {
			result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
			result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
			result.error_description = "avcodec_receive_frame() failed " + std::to_string(ffmpeg_result);
		}
	} else {
		result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
		result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
		result.error_description = "avcodec_send_packet() failed " + std::to_string(ffmpeg_result);
	}
	
	av_packet_free(&packet_read);
	packet_buffer[VIDEO].pop_front();
	// refill the packet buffer
	while (!packet_buffer[VIDEO].size() && read_packet(is_av_separate() ? VIDEO : BOTH).code == 0);
	
	return result;
}
Result_with_string NetworkDecoder::decode_audio(int *size, u8 **data, double *cur_pos) {
	int ffmpeg_result = 0;
	Result_with_string result;
	*size = 0;
	
	AVPacket *packet_read = packet_buffer[AUDIO][0];
	
	AVFrame *cur_frame = av_frame_alloc();
	if (!cur_frame) {
		result.error_description = "av_frame_alloc() failed";
		goto fail;
	}
	
	ffmpeg_result = avcodec_send_packet(decoder_context[AUDIO], packet_read);
	if(ffmpeg_result == 0) {
		ffmpeg_result = avcodec_receive_frame(decoder_context[AUDIO], cur_frame);
		if(ffmpeg_result == 0) {
			auto tmp_result = filter.process_audio_frame(cur_frame); // failure in filtering is normal for the first few frames after initialization, so ignore it
			auto out_frame = tmp_result.code == 0 ? filter.output_frame : cur_frame;
			*data = (u8 *) malloc(out_frame->nb_samples * 2 * decoder_context[AUDIO]->channels);
			*size = swr_convert(swr_context, data, out_frame->nb_samples, (const u8 **) out_frame->data, out_frame->nb_samples);
			*size *= 2;
			
			double time_base = av_q2d(get_stream(AUDIO)->time_base);
			my_assert(out_frame->pts != AV_NOPTS_VALUE);
			*cur_pos = out_frame->pts * time_base + timestamp_offset;
		} else {
			result.error_description = "avcodec_receive_frame() failed " + std::to_string(ffmpeg_result);
			goto fail;
		}
	} else {
		result.error_description = "avcodec_send_packet() failed " + std::to_string(ffmpeg_result) + " " + std::to_string(AVERROR(EAGAIN));
		goto fail;
	}

	av_packet_free(&packet_read);
	packet_buffer[AUDIO].pop_front();
	while (!packet_buffer[AUDIO].size() && read_packet(is_av_separate() ? AUDIO : BOTH).code == 0);
	av_frame_free(&cur_frame);
	return result;
	
	fail:
	
	av_packet_free(&packet_read);
	packet_buffer[AUDIO].pop_front();
	while (!packet_buffer[AUDIO].size() && read_packet(is_av_separate() ? AUDIO : BOTH).code == 0);
	av_frame_free(&cur_frame);
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::get_decoded_video_frame(int width, int height, u8** data, double *cur_pos) {
	Result_with_string result;
	
	if (hw_decoder_enabled) {
		if (video_mvd_tmp_frames.empty()) {
			result.code = DEF_ERR_NEED_MORE_INPUT;
			return result;
		}
		*data = video_mvd_tmp_frames.get_next_poped(); // it's valid until the next pop() is called
		video_mvd_tmp_frames.pop();
	} else {
		if (video_tmp_frames.empty()) {
			result.code = DEF_ERR_NEED_MORE_INPUT;
			return result;
		}
		AVFrame *cur_frame = video_tmp_frames.get_next_poped();
		video_tmp_frames.pop();
		
		int cpy_size[2] = { 0, 0, };

		cpy_size[0] = (width * height);
		cpy_size[1] = cpy_size[0] / 4;
		cpy_size[0] -= cpy_size[0] % 32;
		cpy_size[1] -= cpy_size[1] % 32;
		
		memcpy_asm(sw_video_output_tmp, cur_frame->data[0], cpy_size[0]);
		memcpy_asm(sw_video_output_tmp + (width * height), cur_frame->data[1], cpy_size[1]);
		memcpy_asm(sw_video_output_tmp + (width * height) + (width * height / 4), cur_frame->data[2], cpy_size[1]);
		
		*data = sw_video_output_tmp;
		
		av_frame_unref(cur_frame);
	}
	
	buffered_pts_list_lock.lock();
	if (!buffered_pts_list.size()) {
		Util_log_save("decoder", "SET EMPTY");
	} else {
		*cur_pos = *buffered_pts_list.begin();
		buffered_pts_list.erase(buffered_pts_list.begin());
	}
	buffered_pts_list_lock.unlock();
	
	return result;
}

Result_with_string NetworkDecoder::seek(s64 microseconds) {
	Result_with_string result;
	
	clear_buffer();
	
	s64 min_ts = std::max<s64>(0, microseconds - 1000000);
	s64 max_ts = microseconds + 500000;
	
	if (is_av_separate()) {
		int ffmpeg_result = avformat_seek_file(io->format_context[VIDEO], -1, min_ts, microseconds, max_ts, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ?
		for (int i = 2; i <= 3 && ffmpeg_result < 0; i++) { // retry with wider range backward
			min_ts = std::max<s64>(0, microseconds - i * 1000000);
			ffmpeg_result = avformat_seek_file(io->format_context[VIDEO], -1, min_ts, microseconds, max_ts, AVSEEK_FLAG_FRAME);
			if (ffmpeg_result >= 0) Util_log_save("seek", "succeeded at " + std::to_string(ffmpeg_result));
		}
		if (ffmpeg_result < 0) {
			result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
			result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
			result.error_description = "avformat_seek_file() for video failed " + std::to_string(ffmpeg_result);
			return result;
		}
		avcodec_flush_buffers(decoder_context[VIDEO]);
		// refill the next packets
		result = read_packet(VIDEO);
		if (result.code != 0) return result;
		
		// once successfully sought on video, perform an exact seek on audio
		double time_base = av_q2d(get_stream(VIDEO)->time_base);
		if (packet_buffer[VIDEO][0]->pts != AV_NOPTS_VALUE) microseconds = packet_buffer[VIDEO][0]->pts * time_base * 1000000;
		else microseconds = packet_buffer[VIDEO][0]->dts * time_base * 1000000;
		
		ffmpeg_result = avformat_seek_file(io->format_context[AUDIO], -1, microseconds, microseconds, microseconds, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ???
		if(ffmpeg_result < 0) {
			result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
			result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
			result.error_description = "avformat_seek_file() for audio failed " + std::to_string(ffmpeg_result);
			return result;
		}
		avcodec_flush_buffers(decoder_context[AUDIO]);
		result = read_packet(AUDIO);
		if (result.code != 0) return result;
		return result;
	} else {
		int ffmpeg_result = avformat_seek_file(io->format_context[BOTH], -1, min_ts, microseconds, max_ts, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ?
		for (int i = 2; i <= 3 && ffmpeg_result < 0; i++) { // retry with wider range backward
			min_ts = std::max<s64>(0, microseconds - i * 1000000);
			ffmpeg_result = avformat_seek_file(io->format_context[BOTH], -1, min_ts, microseconds, max_ts, AVSEEK_FLAG_FRAME);
			if (ffmpeg_result >= 0) Util_log_save("seek", "succeeded at " + std::to_string(ffmpeg_result));
		}
		if(ffmpeg_result < 0) {
			result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
			result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
			result.error_description = "avformat_seek_file() failed " + std::to_string(ffmpeg_result);
			return result;
		}
		if (!is_audio_only()) avcodec_flush_buffers(decoder_context[VIDEO]);
		avcodec_flush_buffers(decoder_context[AUDIO]);
		while ((!is_audio_only() && !packet_buffer[VIDEO].size()) || !packet_buffer[AUDIO].size()) {
			result = read_packet(BOTH);
			if (result.code != 0) return result;
		}
		return result;
	}
}
