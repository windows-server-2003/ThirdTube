#pragma once
//https://github.com/smealum/3ds_hb_menu/blob/master/source/utils.h

static inline void unicodeToChar(char* dst, u16* src, int max)
{
	if(!src || !dst)return;
	int n=0;
	while(*src && n<max-1){*(dst++)=(*(src++))&0xFF;n++;}
	*dst=0x00;
}
