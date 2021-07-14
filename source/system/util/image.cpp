#include "headers.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

// returns in BGR565 format, should be freed
u8 *Image_decode(u8 *input, size_t input_len, int* width, int* height)
{
	int image_ch = 0;
	u8 *rgb_image = stbi_load_from_memory(input, input_len, width, height, &image_ch, STBI_rgb);
	if (!rgb_image) {
		Util_log_save("image-dec", "stbi load failed : " + std::string(stbi_failure_reason()));
		return NULL;
	}
	u8 *bgr_image = (u8 *) malloc(*width * *height * 2);
	if (!bgr_image) {
		stbi_image_free(rgb_image);
		return NULL;
	}
	for (int i = 0; i < *width * *height; i++) {
		u8 r = rgb_image[i * 3 + 0];
		u8 g = rgb_image[i * 3 + 1];
		u8 b = rgb_image[i * 3 + 2];
		r >>= 3;
		g >>= 2;
		b >>= 3;
		bgr_image[i * 2 + 0] = b | g << 5;
		bgr_image[i * 2 + 1] = g >> 3 | r << 3;
	}
	
	
	stbi_image_free(rgb_image);
	return bgr_image;
}
