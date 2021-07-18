#include "headers.hpp"
#include "system/util/network_io.hpp"

// mostly stolen from decoder.cpp

extern "C" void memcpy_asm(u8*, u8*, int);

void NetworkDecoder::deinit() {
	ready = false;
	
	for (int type = 0; type < 2; type++) {
		delete opaque[type];
		opaque[type] = NULL;
		avcodec_free_context(&decoder_context[type]);
		if (io_context[type]) av_freep(&io_context[type]->buffer);
		av_freep(&io_context[type]);
		for (auto i : packet_buffer[type]) av_packet_free(&i);
		packet_buffer[type].clear();
		avformat_close_input(&format_context[type]);
	}
	if (hw_decoder_enabled) {
		if (mvd_inited) {
			mvdstdExit();
			mvd_inited = false;
		}
		for (auto i : video_mvd_tmp_frames.deinit()) free(i);
		linearFree(mvd_frame);
		mvd_frame = NULL;
		buffered_pts_list.clear();
	} else {
		for (auto i : video_tmp_frames.deinit()) av_frame_free(&i);
		free(sw_video_output_tmp);
		sw_video_output_tmp = NULL;
	}
	swr_free(&swr_context);
}

static int read_network_stream(void *opaque, u8 *buf, int buf_size_) { // size or AVERROR_EOF
	NetworkDecoder *decoder = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->first;
	NetworkStream *stream = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->second;
	size_t buf_size = buf_size_;
	
	if (stream->read_head >= stream->len) return AVERROR_EOF;
	// network_waiting_status = cacher->waiting_status;
	bool cpu_limited = false;
	while (!stream->is_data_available(stream->read_head, std::min(buf_size, stream->len - stream->read_head))) {
		if (decoder->is_locked) {
			decoder->need_reinit = true;
			goto fail;
		}
		decoder->network_waiting_status = "Reading stream";
		if (!cpu_limited) {
			cpu_limited = true;
			add_cpu_limit(25);
		}
		usleep(20000);
		if (stream->error || stream->quit_request) goto fail;
	}
	if (cpu_limited) {
		cpu_limited = false;
		remove_cpu_limit(25);
	}
	decoder->network_waiting_status = NULL;
	
	{
		auto tmp = stream->get_data(stream->read_head, std::min(buf_size, stream->len - stream->read_head));
		size_t read_size = tmp.size();
		stream->read_head += read_size;
		memcpy(buf, &tmp[0], read_size);
		if (!read_size) return AVERROR_EOF;
		return read_size;
	}
	
	fail :
	if (cpu_limited) {
		cpu_limited = false;
		remove_cpu_limit(25);
	}
	return AVERROR_EOF;
}
static int64_t seek_network_stream(void *opaque, s64 offset, int whence) { // size or AVERROR_EOF
	NetworkDecoder *decoder = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->first;
	NetworkStream *stream = ((std::pair<NetworkDecoder *, NetworkStream *> *) opaque)->second;
	
	(void) decoder;
	
	if (whence == AVSEEK_SIZE) return stream->len;
	
	size_t new_pos = 0;
	if (whence == SEEK_SET) new_pos = offset;
	else if (whence == SEEK_CUR) new_pos = stream->read_head + offset;
	else if (whence == SEEK_END) new_pos = stream->len + offset;
	
	if (new_pos > stream->len) return -1;
	
	stream->read_head = new_pos;
	
	return stream->read_head;
}

#define NETWORK_BUFFER_SIZE 0x8000
Result_with_string NetworkDecoder::init_(int type, AVMediaType expected_codec_type) {
	Result_with_string result;
	int ffmpeg_result;
	
	if (video_audio_seperate || type == BOTH) {
		network_stream[type]->read_head = 0;
		
		opaque[type] = new std::pair<NetworkDecoder *, NetworkStream *>(this, network_stream[type]);
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
			} else if (format_context[type]->streams[0]->codecpar->codec_type != expected_codec_type) {
				result.error_description = "stream type wrong : " + std::to_string(format_context[type]->streams[0]->codecpar->codec_type);
				goto fail;
			}
		}
	}
	if (!video_audio_seperate) {
		stream_index[type] = -1;
		for (size_t i = 0; i < format_context[BOTH]->nb_streams; i++) {
			if (format_context[BOTH]->streams[i]->codecpar->codec_type == expected_codec_type) {
				stream_index[type] = i;
			}
		}
		if (stream_index[type] == -1) {
			result.error_description = "stream of the expected type not found";
			goto fail;
		}
	} else stream_index[type] = 0;
	
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

	if ((video_audio_seperate ? (type == VIDEO) : (type == BOTH))) decoder_context[type]->lowres = 0; // <-------
	ffmpeg_result = avcodec_open2(decoder_context[type], codec[type], NULL);
	if (ffmpeg_result != 0) {
		result.error_description = "avcodec_open2() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	return result;
	
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::init_video_decoder(bool &is_mvd) {
	Result_with_string result;
	int width, height;
	
	if (is_mvd && codec[VIDEO]->long_name != std::string("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10")) {
		Util_log_save("decoder", "not H.264, disabling hw decoder");
		is_mvd = false;
	}
	
	// init output buffer
	width = decoder_context[VIDEO]->width;
	height = decoder_context[VIDEO]->height;
	if (width % 16) width += 16 - width % 16;
	if (height % 16) height += 16 - height % 16;
	if (is_mvd) {
		std::vector<u8 *> init(11);
		for (auto &i : init) {
			i = (u8 *) malloc(width * height * 2);
			if (!i) {
				result.error_description = "malloc() failed while preallocating ";
				goto fail;
			}
		}
		video_mvd_tmp_frames.init(init);
		
		mvd_frame = (u8 *) linearAlloc(width * height * 2);
		if (!mvd_frame) {
			result.error_description = "malloc() failed while preallocating ";
			goto fail;
		}
	} else {
		std::vector<AVFrame *> init(11);
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
Result_with_string NetworkDecoder::init_audio_decoder() {
	int ffmpeg_result;
	Result_with_string result;
	
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
	
	return result;
	fail:
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}

Result_with_string NetworkDecoder::init(NetworkStream *video_stream, NetworkStream *audio_stream, bool request_hw_decoder) {
	Result_with_string result;
	
	video_audio_seperate = video_stream != audio_stream;
	need_reinit = false;
	is_locked = false;
	this->network_stream[VIDEO] = video_stream;
	this->network_stream[AUDIO] = audio_stream;
	
	if (request_hw_decoder) {
		if (!mvd_inited) {
			Util_log_save("dec/init", "free linear space : " + std::to_string(linearSpaceFree() / 1000) + " KB");
			Result mvd_result = -1;
			for (int mb = 15; mb >= 5; mb--) {
				mvd_result = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_BGR565, 1000000 * mb, NULL);
				if (mvd_result == 0) {
					Util_log_save("dec/init", "mvdstdInit ok at " + std::to_string(mb) + " MB");
					break;
				}
			}
			if (mvd_result != 0) Util_log_save("dec/init", "mvdstdInit returned " + std::to_string(mvd_result) + " with a minimum size of work buffer");
			else mvd_inited = true;
		}
		mvd_first = true;
	}
	hw_decoder_enabled = request_hw_decoder;
	
	result = init_(VIDEO, AVMEDIA_TYPE_VIDEO);
	if (result.code != 0) {
		result.error_description = "[video] " + result.error_description;
		return result;
	}
	result = init_(AUDIO, AVMEDIA_TYPE_AUDIO);
	if (result.code != 0) {
		result.error_description = "[audio] " + result.error_description;
		return result;
	}
	
	if (video_audio_seperate) {
		for (int type = 0; type < 2; type++) {
			auto result = read_packet(type);
			if (result.code != 0) {
				result.error_description = (type == VIDEO ? "[video] " : "[audio] ") + result.error_description;
				return result;
			}
		}
	} else {
		while (!packet_buffer[VIDEO].size() || !packet_buffer[AUDIO].size()) {
			auto result = read_packet(BOTH);
			if (result.code != 0) return result;
		}
	}
	
	
	svcCreateMutex(&buffered_pts_list_lock, false);
	
	result = init_video_decoder(request_hw_decoder);
	if (result.code != 0) {
		result.error_description = "[video] " + result.error_description;
		return result;
	}
	hw_decoder_enabled = request_hw_decoder;
	result = init_audio_decoder();
	if (result.code != 0) {
		result.error_description = "[audio] " + result.error_description;
		return result;
	}
	
	ready = true;
	return result;
}
Result_with_string NetworkDecoder::init(NetworkStream *both_stream, bool request_hw_decoder) {
	return init(both_stream, both_stream, request_hw_decoder);
}

NetworkDecoder::VideoFormatInfo NetworkDecoder::get_video_info() {
	VideoFormatInfo res;
	res.width = decoder_context[VIDEO]->width;
	res.height = decoder_context[VIDEO]->height;
	res.framerate = av_q2d(get_stream(VIDEO)->avg_frame_rate);
	res.format_name = codec[VIDEO]->long_name;
	res.duration = (double) format_context[video_audio_seperate ? VIDEO : BOTH]->duration / AV_TIME_BASE;
	return res;
}
NetworkDecoder::AudioFormatInfo NetworkDecoder::get_audio_info() {
	AudioFormatInfo res;
	res.bitrate = decoder_context[AUDIO]->bit_rate;
	res.sample_rate = decoder_context[AUDIO]->sample_rate;
	res.ch = decoder_context[AUDIO]->channels;
	res.format_name = codec[AUDIO]->long_name;
	res.duration = (double) format_context[video_audio_seperate ? AUDIO : BOTH]->duration / AV_TIME_BASE;
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
	
	ffmpeg_result = av_read_frame(format_context[type], tmp_packet);
	if (ffmpeg_result != 0) {
		result.error_description = "av_read_frame() failed";
		goto fail;
	}
	{
		int packet_type = video_audio_seperate ? type : (tmp_packet->stream_index == stream_index[VIDEO] ? VIDEO : AUDIO);
		packet_buffer[packet_type].push_back(tmp_packet);
		return result;
	}
	
	fail :
	av_packet_free(&tmp_packet);
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
std::string NetworkDecoder::next_decode_type() {
	if (!packet_buffer[VIDEO].size() && !packet_buffer[AUDIO].size()) return "EOF";
	if (!packet_buffer[AUDIO].size()) return "video";
	if (!packet_buffer[VIDEO].size()) return "audio";
	double video_dts = packet_buffer[VIDEO][0]->dts * av_q2d(get_stream(VIDEO)->time_base);
	double audio_dts = packet_buffer[AUDIO][0]->dts * av_q2d(get_stream(AUDIO)->time_base);
	return video_dts <= audio_dts ? "video" : "audio";
}
static std::string debug_str = "";
Result_with_string NetworkDecoder::mvd_decode(int *width, int *height) {
	Result_with_string result;
	int log_num;
	
	*width = decoder_context[VIDEO]->width;
	*height = decoder_context[VIDEO]->height;
	if (*width % 16 != 0) *width += 16 - *width % 16;
	if (*height % 16 != 0) *height += 16 - *height % 16;
	
	MVDSTD_Config config;
	mvdstdGenerateDefaultConfig(&config, *width, *height, *width, *height, NULL, NULL, NULL);
	
	int offset = 0;
	int source_offset = 0;

	AVPacket *packet_read = packet_buffer[VIDEO][0];
	u8 *mvd_packet = (u8 *) linearAlloc(packet_read->size);
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
		//Util_file_save_to_file("0", "/test/", mvd_packet, offset, true);

		log_num = Util_log_save("", "mvdstdProcessVideoFrame()...");
		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		Util_log_add(log_num, "", result.code);

		offset = 0;
		memset(mvd_packet, 0x0, 0x2);
		offset += 2;
		memset(mvd_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(mvd_packet + offset, decoder_context[VIDEO]->extradata + 11 + *(decoder_context[VIDEO]->extradata + 7), *(decoder_context[VIDEO]->extradata + 10 + *(decoder_context[VIDEO]->extradata + 7)));
		offset += *(decoder_context[VIDEO]->extradata + 10 + *(decoder_context[VIDEO]->extradata + 7));

		/*memset(mvd_packet + offset, 0x0, 0x2);
		offset += 2;
		memset(mvd_packet + offset, 0x1, 0x1);
		offset += 1;
		memcpy(mvd_packet + offset, packet_read->data + 4 + *(packet_read->data + 3) + 4, *(packet_read->data + 4 + *(packet_read->data + 3) + 3));
		offset += *(packet_read->data + 4 + *(packet_read->data + 3) + 3);*/
		//Util_file_save_to_file("1", "/test/", mvd_packet, offset, true);

		log_num = Util_log_save("", "mvdstdProcessVideoFrame()...");
		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		Util_log_add(log_num, "", result.code);
	}

	//Util_log_save("", std::to_string(size));
	
	/*memcpy(mvd_packet + offset, decoder_context[VIDEO]->extradata + 8, *(decoder_context[VIDEO]->extradata + 7));
	offset += *(decoder_context[VIDEO]->extradata + 7);*/
	/*memset(mvd_packet + offset, 0x0, 0x2);
	offset += 2;
	memset(mvd_packet + offset, 0x1, 0x1);
	offset += 1;
	memcpy(mvd_packet + offset, decoder_context[VIDEO]->extradata + 8 + *(decoder_context[VIDEO]->extradata + 7) + 3, 4);
	offset += 4;*/

	//Util_log_save("", std::to_string(*(decoder_context[VIDEO]->extradata + 7)));

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

	//Util_log_save("", std::to_string(*(packet_read->data + 3)));
	/*Util_file_save_to_file("extra.data", "/test/", decoder_context[VIDEO]->extradata, decoder_context[VIDEO]->extradata_size, true);
	Util_file_save_to_file("mvd_packet.data", "/test/", mvd_packet, offset, true);*/

	//config.physaddr_outdata0 = osConvertVirtToPhys(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL));
	config.physaddr_outdata0 = osConvertVirtToPhys(mvd_frame);

	//log_num = Util_log_save("", "mvdstdProcessVideoFrame()...");
	//GSPGPU_FlushDataCache(mvd_packet, offset);
	result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
	//Util_log_save("", "mvdstdProcessVideoFrame()... ", result.code);
	//Util_log_add(log_num, "", result.code);

	if(mvd_first)
	{
		//Do I need to send same nal data at first frame?
		log_num = Util_log_save("", "mvdstdProcessVideoFrame()...");
		result.code = mvdstdProcessVideoFrame(mvd_packet, offset, 0, NULL);
		Util_log_add(log_num, "", result.code);
	}

	if (MVD_CHECKNALUPROC_SUCCESS(result.code)) {
		double cur_pos;
		double time_base = av_q2d(get_stream(VIDEO)->time_base);
		if (packet_read->pts != AV_NOPTS_VALUE) cur_pos = packet_read->pts * time_base;
		else cur_pos = packet_read->dts * time_base;
		
		svcWaitSynchronization(buffered_pts_list_lock, std::numeric_limits<s64>::max());
		buffered_pts_list.insert(cur_pos);
		svcReleaseMutex(buffered_pts_list_lock);
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
	linearFree(mvd_packet);
	mvd_packet = NULL;
	av_packet_free(&packet_read);
	packet_buffer[VIDEO].pop_front();
	// refill the packet buffer
	while (!packet_buffer[VIDEO].size() && read_packet(video_audio_seperate ? VIDEO : BOTH).code == 0);
	
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
		
		return mvd_decode(width, height);
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
		if(ffmpeg_result == 0) {
			*width = cur_frame->width;
			*height = cur_frame->height;
			double time_base = av_q2d(get_stream(VIDEO)->time_base);
			if (cur_frame->pts != AV_NOPTS_VALUE) *cur_pos = cur_frame->pts * time_base;
			else *cur_pos = cur_frame->pkt_dts * time_base;
			video_tmp_frames.push();
		} else {
			result.error_description = "avcodec_receive_frame() failed " + std::to_string(ffmpeg_result);
			goto fail;
		}
	} else {
		result.error_description = "avcodec_send_packet() failed " + std::to_string(ffmpeg_result);
		goto fail;
	}
	
	av_packet_free(&packet_read);
	packet_buffer[VIDEO].pop_front();
	// refill the packet buffer
	while (!packet_buffer[VIDEO].size() && read_packet(video_audio_seperate ? VIDEO : BOTH).code == 0);
	
	return result;
	
	fail:
	
	av_packet_free(&packet_read);
	packet_buffer[VIDEO].pop_front();
	// refill the packet buffer
	while (!packet_buffer[VIDEO].size() && read_packet(video_audio_seperate ? VIDEO : BOTH).code == 0);
	
	result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
	result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
	return result;
}
Result_with_string NetworkDecoder::decode_audio(int *size, u8 **data, double *cur_pos) {
	int ffmpeg_result = 0;
	Result_with_string result;
	*size = 0;
	
	AVPacket *packet_read = packet_buffer[AUDIO][0];
	
	double time_base = av_q2d(get_stream(AUDIO)->time_base);
	if (packet_read->pts != AV_NOPTS_VALUE) *cur_pos = packet_read->pts * time_base;
	else *cur_pos = packet_read->dts * time_base;
	
	AVFrame *cur_frame = av_frame_alloc();
	if (!cur_frame) {
		result.error_description = "av_frame_alloc() failed";
		goto fail;
	}
	
	ffmpeg_result = avcodec_send_packet(decoder_context[AUDIO], packet_read);
	if(ffmpeg_result == 0) {
		ffmpeg_result = avcodec_receive_frame(decoder_context[AUDIO], cur_frame);
		if(ffmpeg_result == 0) {
			*data = (u8 *) malloc(cur_frame->nb_samples * 2 * decoder_context[AUDIO]->channels);
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

	av_packet_free(&packet_read);
	packet_buffer[AUDIO].pop_front();
	while (!packet_buffer[AUDIO].size() && read_packet(video_audio_seperate ? AUDIO : BOTH).code == 0);
	av_frame_free(&cur_frame);
	return result;
	
	fail:
	
	av_packet_free(&packet_read);
	packet_buffer[AUDIO].pop_front();
	while (!packet_buffer[AUDIO].size() && read_packet(video_audio_seperate ? AUDIO : BOTH).code == 0);
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
		
		svcWaitSynchronization(buffered_pts_list_lock, std::numeric_limits<s64>::max());
		if (!buffered_pts_list.size()) {
			Util_log_save("decoder", "SET EMPTY");
		} else {
			*cur_pos = *buffered_pts_list.begin();
			// Util_log_save("decoder", "set poped : " + std::to_string(buffered_pts_list.size()));
			buffered_pts_list.erase(buffered_pts_list.begin());
		}
		svcReleaseMutex(buffered_pts_list_lock);
		return result;
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
		
		double time_base = av_q2d(get_stream(VIDEO)->time_base);
		if (cur_frame->pts != AV_NOPTS_VALUE) *cur_pos = cur_frame->pts * time_base;
		else *cur_pos = cur_frame->pkt_dts * time_base;
		
		*data = sw_video_output_tmp;
		
		av_frame_unref(cur_frame);
		return result;
	}
}

Result_with_string NetworkDecoder::seek(s64 microseconds) {
	Result_with_string result;
	
	// clear buffer
	for (int type = 0; type < 2; type++) {
		for (auto i : packet_buffer[type]) av_packet_free(&i);
		packet_buffer[type].clear();
	}
	if (hw_decoder_enabled) video_mvd_tmp_frames.clear();
	else video_tmp_frames.clear();
	buffered_pts_list.clear();
	
	if (video_audio_seperate) {
		int ffmpeg_result = avformat_seek_file(format_context[VIDEO], -1, microseconds - 1000000, microseconds, microseconds + 1000000, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ?
		if(ffmpeg_result < 0) {
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
		
		ffmpeg_result = avformat_seek_file(format_context[AUDIO], -1, microseconds, microseconds, microseconds, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ???
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
		int ffmpeg_result = avformat_seek_file(format_context[BOTH], -1, microseconds - 1000000, microseconds, microseconds + 1000000, AVSEEK_FLAG_FRAME); // AVSEEK_FLAG_FRAME <- ?
		if(ffmpeg_result < 0) {
			result.code = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS;
			result.string = DEF_ERR_FFMPEG_RETURNED_NOT_SUCCESS_STR;
			result.error_description = "avformat_seek_file() failed " + std::to_string(ffmpeg_result);
			return result;
		}
		avcodec_flush_buffers(decoder_context[VIDEO]);
		avcodec_flush_buffers(decoder_context[AUDIO]);
		while (!packet_buffer[VIDEO].size() || !packet_buffer[AUDIO].size()) {
			result = read_packet(BOTH);
			if (result.code != 0) return result;
		}
		return result;
	}
}
