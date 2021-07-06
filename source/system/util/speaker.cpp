#include "headers.hpp"

ndspWaveBuf util_ndsp_buffer[24][60];
double util_ndsp_buffer_timestamp[24][60]; // {pts, sample rate}

void Util_speaker_init(int play_ch, int music_ch, int sample_rate)
{
	float mix[12] = { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

	ndspChnReset(play_ch);
	ndspChnWaveBufClear(play_ch);
	ndspChnSetMix(play_ch, mix);
	if(music_ch == 2)
	{
		ndspChnSetFormat(play_ch, NDSP_FORMAT_STEREO_PCM16);
		ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	}
	else
	{
		ndspChnSetFormat(play_ch, NDSP_FORMAT_MONO_PCM16);
		ndspSetOutputMode(NDSP_OUTPUT_MONO);
	}
	
	ndspChnSetInterp(play_ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(play_ch, sample_rate);
	memset(util_ndsp_buffer[play_ch], 0, sizeof(util_ndsp_buffer[play_ch]));
	for(int i = 0; i < 60; i++)
		util_ndsp_buffer[play_ch][i].data_vaddr = NULL;
}

Result_with_string Util_speaker_add_buffer(int play_ch, int music_ch, u8* buffer, int size, double pts)
{
	Result_with_string result;
	int free_queue = -1;

	for(int i = 0; i < 60; i++)
	{
		if(util_ndsp_buffer[play_ch][i].status == NDSP_WBUF_FREE || util_ndsp_buffer[play_ch][i].status == NDSP_WBUF_DONE)
		{
			free_queue = i;
			break;
		}
	}

	if(free_queue == -1)
	{
		result.code = DEF_ERR_OTHER;
		result.string = "[Error] Queues are full ";
		return result;
	}

	linearFree((void*)util_ndsp_buffer[play_ch][free_queue].data_vaddr);
	util_ndsp_buffer[play_ch][free_queue].data_vaddr = NULL;
	util_ndsp_buffer[play_ch][free_queue].data_vaddr = (u8*)linearAlloc(size * music_ch);
	if(util_ndsp_buffer[play_ch][free_queue].data_vaddr == NULL)
	{
		result.code = DEF_ERR_OUT_OF_LINEAR_MEMORY;
		result.string = DEF_ERR_OUT_OF_LINEAR_MEMORY_STR;
		return result;
	}

	memcpy((void*)util_ndsp_buffer[play_ch][free_queue].data_vaddr, buffer, size * music_ch);

	util_ndsp_buffer[play_ch][free_queue].nsamples = size / 2;
	ndspChnWaveBufAdd(play_ch, &util_ndsp_buffer[play_ch][free_queue]);
	
	util_ndsp_buffer_timestamp[play_ch][free_queue] = pts;
	return result;
}

double Util_speaker_get_current_timestamp(int play_ch, int sample_rate)
{
	if (!Util_speaker_is_playing(play_ch)) return -1;
	double queued_min = INFINITY;
	for (int i = 0; i < 60; i++) {
		if (util_ndsp_buffer[play_ch][i].status == NDSP_WBUF_PLAYING)
			return util_ndsp_buffer_timestamp[play_ch][i] + (double) ndspChnGetSamplePos(play_ch) / sample_rate;
		if (util_ndsp_buffer[play_ch][i].status == NDSP_WBUF_QUEUED)
			queued_min = std::min(queued_min, util_ndsp_buffer_timestamp[play_ch][i]);
	}
	if (queued_min != INFINITY) return queued_min;
	// weired...
	if (!Util_speaker_is_playing(play_ch)) return -1;
	// really weired...
	Util_log_save("speaker", "unexpected behavior in get_current_timestamp");
	return -1;
}

void Util_speaker_clear_buffer(int play_ch)
{
	ndspChnWaveBufClear(play_ch);
	while (Util_speaker_is_playing(play_ch)) usleep(10000);
	for (int i = 0; i < 60; i++) {
		util_ndsp_buffer[play_ch][i].status = NDSP_WBUF_FREE;
		util_ndsp_buffer_timestamp[play_ch][i] = 0.0;
	}
}

void Util_speaker_pause(int play_ch)
{
	ndspChnSetPaused(play_ch, true);
}

void Util_speaker_resume(int play_ch)
{
	ndspChnSetPaused(play_ch, false);
}

bool Util_speaker_is_paused(int play_ch)
{
	return ndspChnIsPaused(play_ch);
}

bool Util_speaker_is_playing(int play_ch)
{
	return ndspChnIsPlaying(play_ch);
}

void Util_speaker_exit(int play_ch)
{
	ndspChnWaveBufClear(play_ch);
	ndspChnSetPaused(play_ch, false);
	for(int i = 0; i < 60; i++)
	{
		linearFree((void*)util_ndsp_buffer[play_ch][i].data_vaddr);
		util_ndsp_buffer[play_ch][i].data_vaddr = NULL;
	}
}
