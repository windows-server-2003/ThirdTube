THIRDTUBE_PATH=C:/path/to/ThirdTube

LIBCTRU_PATH=$THIRDTUBE_PATH/library/libctru
PORTLIBS_PATH=$DEVKITPRO/portlibs/3ds

./configure CFLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -mword-relocations -ffunction-sections -fdata-sections" CPPFLAGS="-D_3DS -D__3DS__ -I$PORTLIBS_PATH/include -I$LIBCTRU_PATH/include" LDFLAGS="-L$PORTLIBS_PATH/lib -L$LIBCTRU_PATH/lib" LIBS="-lctru" --host=arm-none-eabi --disable-shared --enable-static --disable-python-bindings --without-openssl
