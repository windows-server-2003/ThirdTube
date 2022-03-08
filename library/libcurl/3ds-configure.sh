THIRDTUBE_PATH=C:/path/to/ThirdTube


LIBCTRU_PATH=$THIRDTUBE_PATH/library/libctru
LIBBROTLI_PATH=$THIRDTUBE_PATH/library/libbrotli
NGHTTP2_PATH=$THIRDTUBE_PATH/library/nghttp2
PORTLIBS_PATH=$DEVKITPRO/portlibs/3ds

INCLUDE_OPTION="-I$PORTLIBS_PATH/include -I$LIBCTRU_PATH/include -I$LIBBROTLI_PATH/include"
LIBRARY_OPTION="-L$PORTLIBS_PATH/lib -L$LIBCTRU_PATH/lib -L$LIBBROTLI_PATH/lib"

./configure CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -mword-relocations -ffunction-sections -fdata-sections" CPPFLAGS="-D_3DS -D__3DS__ $INCLUDE_OPTION" LDFLAGS="$LIBRARY_OPTION" LIBS="-lctru" --host=arm-none-eabi --disable-shared --enable-static --disable-ipv6 --disable-unix-sockets --disable-manual --disable-ntlm-wb --disable-threaded-resolver --without-ssl --without-zstd --with-mbedtls --with-nghttp2=$NGHTTP2_PATH
