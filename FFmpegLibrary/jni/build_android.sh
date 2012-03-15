if [ "$NDK" = "" ]; then
	echo NDK variable not set, exiting
	echo "Use: export NDK=/your/path/to/android-ndk"
	exit 1
fi


function build_one
{
	cd ffmpeg
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	CC=$PREBUILT/bin/$EABIARCH-gcc
	CROSS_PREFIX=$PREBUILT/bin/$EABIARCH-
	NM=$PREBUILT/bin/$EABIARCH-nm
	./configure --target-os=linux \
	    --prefix=$PREFIX \
	    --enable-cross-compile \
	    --extra-libs="-lgcc" \
	    --arch=$ARCH \
	    --cc=$CC \
	    --cross-prefix=$CROSS_PREFIX \
	    --nm=$NM \
	    --sysroot=$PLATFORM \
	    --extra-cflags=" -O3 -fpic -DANDROID -DHAVE_SYS_UIO_H=1 -Dipv6mr_interface=ipv6mr_ifindex -fasm -Wno-psabi -fno-short-enums  -fno-strict-aliasing -finline-limit=300 $OPTIMIZE_CFLAGS " \
	    --disable-shared \
	    --enable-static \
	    --extra-ldflags="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog" \
	    --disable-everything \
	    --enable-demuxer=mov \
	    --enable-demuxer=h264 \
	    --disable-ffplay \
	    --enable-protocol=file \
	    --enable-avformat \
	    --enable-avcodec \
	    --enable-decoder=rawvideo \
	    --enable-decoder=mjpeg \
	    --enable-decoder=h263 \
	    --enable-decoder=mpeg4 \
	    --enable-decoder=h264 \
	    --enable-parser=h264 \
	    --disable-network \
	    --enable-zlib \
	    --disable-avfilter \
	    --disable-avdevice \
	    $ADDITIONAL_CONFIGURE_FLAG

	make clean
	make  -j4 install

	$PREBUILT/bin/$EABIARCH-ar d libavcodec/libavcodec.a inverse.o

	$PREBUILT/bin/$EABIARCH-ld -rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -soname libffmpeg.so -shared -nostdlib  -z,noexecstack -Bsymbolic --whole-archive --no-undefined -o $PREFIX/libffmpeg.so libavcodec/libavcodec.a libavformat/libavformat.a libavutil/libavutil.a libswscale/libswscale.a -lc -lm -lz -ldl -llog  --warn-once  --dynamic-linker=/system/bin/linker $PREBUILT/lib/gcc/$EABIARCH/4.4.3/libgcc.a
}

#arm v6
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv6
OPTIMIZE_CFLAGS="-marm -march=$CPU"
PREFIX=../ffmpeg-build/armeabi
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
PLATFORM_VERSION=android-5
#build_one

#x86
EABIARCH=i686-android-linux
ARCH=x86
OPTIMIZE_CFLAGS="-m32"
PREFIX=../ffmpeg-build/x86
ADDITIONAL_CONFIGURE_FLAG=--disable-asm
PREBUILT=$NDK/toolchains/x86-4.4.3/prebuilt/linux-x86
PLATFORM_VERSION=android-9
#build_one

#arm v7vfpv3
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv7-a
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfpv3-d16 -marm -march=$CPU "
PREFIX=../ffmpeg-build/armeabi-v7a
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
PLATFORM_VERSION=android-5
build_one



#arm v7vfp
#EABIARCH=arm-linux-androideabi
#ARCH=arm
#CPU=armv7-a
#OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfp -marm -march=$CPU "
#PREFIX=../ffmpeg-build/armeabi-v7a-vfp
#ADDITIONAL_CONFIGURE_FLAG=
#PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
#PLATFORM_VERSION=android-5
#build_one

#arm v7n
#EABIARCH=arm-linux-androideabi
#ARCH=arm
#CPU=armv7-a
#OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=neon -marm -march=$CPU -mtune=cortex-a8"
#PREFIX=../ffmpeg-build/armeabi-v7a-neon
#ADDITIONAL_CONFIGURE_FLAG=--enable-neon
#PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
#PLATFORM_VERSION=android-5
#build_one
