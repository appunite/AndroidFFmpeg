if [ "$NDK" = "" ]; then
	echo NDK variable not set, exiting
	echo "Use: export NDK=/your/path/to/android-ndk"
	exit 1
fi

#OS=linux
OS=darwin

function build_aac
{
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	export PATH=${PATH}:$PREBUILT/bin/
	CROSS_COMPILE=$PREBUILT/bin/$EABIARCH-
#CFLAGS=" -I$ARM_INC -fpic -DANDROID -fpic -mthumb-interwork -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5TE__  -Wno-psabi -march=armv5te -mtune=xscale -msoft-float -mthumb -Os -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 -DANDROID  -Wa,--noexecstack -MMD -MP "
	export CPPFLAGS="$CFLAGS"
	export CFLAGS="$CFLAGS"
	export CXXFLAGS="$CFLAGS"
	export CXX="${CROSS_COMPILE}g++ --sysroot=$PLATFORM"
	export LDFLAGS="$LDFLAGS"
	export CC="${CROSS_COMPILE}gcc --sysroot=$PLATFORM"
	export NM="${CROSS_COMPILE}nm"
	export STRIP="${CROSS_COMPILE}strip"
	export RANLIB="${CROSS_COMPILE}ranlib"
	export AR="${CROSS_COMPILE}ar"

	export LDFLAGS="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog"
	cd vo-aacenc
	./configure \
	    --enable-cross-compile \
	    --prefix=$(pwd)/$PREFIX \
	    --host=$ARCH-linux \
	    --disable-dependency-tracking \
	    --disable-shared \
	    --enable-static \
	    --with-pic \
	    $ADDITIONAL_CONFIGURE_FLAG

	make clean
	make  -j4 install
	cd ..
}
function build_one
{
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	CC=$PREBUILT/bin/$EABIARCH-gcc
	CROSS_PREFIX=$PREBUILT/bin/$EABIARCH-
	NM=$PREBUILT/bin/$EABIARCH-nm
	cd ffmpeg
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
	    --extra-ldflags="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog -L$PREFIX/lib" \
	    --extra-cflags="-I$PREFIX/include" \
	    --disable-everything \
	    --enable-demuxer=mov \
	    --enable-demuxer=h264 \
	    --enable-demuxer=mpegvideo \
	    --enable-demuxer=h263 \
	    --enable-demuxer=mpegps \
	    --enable-demuxer=mjpeg \
	    --enable-muxer=mp4 \
	    --enable-muxer=mov \
	    --enable-muxer=mjpeg \
	    --disable-ffplay \
	    --enable-protocol=file \
	    --enable-avformat \
	    --enable-avcodec \
	    --enable-decoder=rawvideo \
	    --enable-encoder=rawvideo \
	    --enable-decoder=mjpeg \
	    --enable-encoder=mjpeg \
	    --enable-decoder=h263 \
	    --enable-decoder=mpeg4 \
	    --enable-encoder=mpeg4 \
	    --enable-decoder=h264 \
	    --enable-encoder=h264 \
	    --enable-decoder=aac \
	    --enable-encoder=aac \
	    --enable-parser=h264 \
	    --disable-network \
	    --enable-zlib \
	    --disable-avfilter \
	    --disable-avdevice \
	    --enable-nonfree \
	    --enable-libvo-aacenc \
	    --enable-version3 \
	    $ADDITIONAL_CONFIGURE_FLAG
	make clean
	make  -j4 install

#$PREBUILT/bin/$EABIARCH-ar d libavcodec/libavcodec.a inverse.o
	$PREBUILT/bin/$EABIARCH-ld -rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib -L$PREFIX/lib  -soname libffmpeg.so -shared -nostdlib  -z,noexecstack -Bsymbolic --whole-archive --no-undefined -o $PREFIX/libffmpeg.so -lavcodec -lavformat -lavutil -lswscale -lvo-aacenc -lc -lm -lz -ldl -llog  --warn-once  --dynamic-linker=/system/bin/linker -zmuldefs $PREBUILT/lib/gcc/$EABIARCH/4.4.3/libgcc.a
	cd ..
}

#arm v6
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv6
OPTIMIZE_CFLAGS="-marm -march=$CPU"
PREFIX=../ffmpeg-build/armeabi
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-5
build_aac
build_one

#x86
EABIARCH=i686-android-linux
ARCH=x86
OPTIMIZE_CFLAGS="-m32"
PREFIX=../ffmpeg-build/x86
ADDITIONAL_CONFIGURE_FLAG=--disable-asm
PREBUILT=$NDK/toolchains/x86-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-9
build_aac
build_one

#arm v7vfpv3
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv7-a
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfpv3-d16 -marm -march=$CPU "
PREFIX=../ffmpeg-build/armeabi-v7a
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-5
build_aac
build_one



#arm v7vfp
#EABIARCH=arm-linux-androideabi
#ARCH=arm
#CPU=armv7-a
#OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfp -marm -march=$CPU "
#PREFIX=../ffmpeg-build/armeabi-v7a-vfp
#ADDITIONAL_CONFIGURE_FLAG=
#PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
#PLATFORM_VERSION=android-5
#build_one

#arm v7n
#EABIARCH=arm-linux-androideabi
#ARCH=arm
#CPU=armv7-a
#OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=neon -marm -march=$CPU -mtune=cortex-a8"
#PREFIX=../ffmpeg-build/armeabi-v7a-neon
#ADDITIONAL_CONFIGURE_FLAG=--enable-neon
#PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
#PLATFORM_VERSION=android-5
#build_one
