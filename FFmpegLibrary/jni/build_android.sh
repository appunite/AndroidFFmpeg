#!/bin/bash
#
# build_android.sh
# Copyright (c) 2012 Jacek Marchwicki
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [ "$NDK" = "" ]; then
	echo NDK variable not set, exiting
	echo "Use: export NDK=/your/path/to/android-ndk"
	exit 1
fi

OS=`uname -s | tr '[A-Z]' '[a-z]'`
function build_x264
{
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	export PATH=${PATH}:$PREBUILT/bin/
	CROSS_COMPILE=$PREBUILT/bin/$EABIARCH-
	CFLAGS=$OPTIMIZE_CFLAGS
#CFLAGS=" -I$ARM_INC -fpic -DANDROID -fpic -mthumb-interwork -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5TE__  -Wno-psabi -march=armv5te -mtune=xscale -msoft-float -mthumb -Os -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 -DANDROID  -Wa,--noexecstack -MMD -MP "
	export CPPFLAGS="$CFLAGS"
	export CFLAGS="$CFLAGS $OPTIMIZE_CFLAGS"
	export CXXFLAGS="$CFLAGS"
	export CXX="${CROSS_COMPILE}g++ --sysroot=$PLATFORM"
	export LDFLAGS="$LDFLAGS"
	export AS="${CROSS_COMPILE}gcc --sysroot=$PLATFORM"
	export CC="${CROSS_COMPILE}gcc --sysroot=$PLATFORM"
	export NM="${CROSS_COMPILE}nm"
	export STRIP="${CROSS_COMPILE}strip"
	export RANLIB="${CROSS_COMPILE}ranlib"
	export AR="${CROSS_COMPILE}ar"

	export LDFLAGS="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog"
	cd x264
	./configure --prefix=$(pwd)/$PREFIX --host=$ARCH-linux --enable-static $ADDITIONAL_CONFIGURE_FLAG || exit 1

	make clean || exit 1
	make -j4 install || exit 1
	cd ..
}

function build_amr
{
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	export PATH=${PATH}:$PREBUILT/bin/
	CROSS_COMPILE=$PREBUILT/bin/$EABIARCH-
	CFLAGS=$OPTIMIZE_CFLAGS
#CFLAGS=" -I$ARM_INC -fpic -DANDROID -fpic -mthumb-interwork -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5E__ -D__ARM_ARCH_5TE__  -Wno-psabi -march=armv5te -mtune=xscale -msoft-float -mthumb -Os -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 -DANDROID  -Wa,--noexecstack -MMD -MP "
	export CPPFLAGS="$CFLAGS"
	export CFLAGS="$CFLAGS $OPTIMIZE_CFLAGS"
	export CXXFLAGS="$CFLAGS"
	export CXX="${CROSS_COMPILE}g++ --sysroot=$PLATFORM"
	export LDFLAGS="$LDFLAGS"
	export CC="${CROSS_COMPILE}gcc --sysroot=$PLATFORM"
	export NM="${CROSS_COMPILE}nm"
	export STRIP="${CROSS_COMPILE}strip"
	export RANLIB="${CROSS_COMPILE}ranlib"
	export AR="${CROSS_COMPILE}ar"

	export LDFLAGS="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog"
	cd vo-amrwbenc
	./configure \
	    --prefix=$(pwd)/$PREFIX \
	    --host=$ARCH-linux \
	    --disable-dependency-tracking \
	    --disable-shared \
	    --enable-static \
	    --with-pic \
	    $ADDITIONAL_CONFIGURE_FLAG \
	    || exit 1

	make clean || exit 1
	make -j4 install || exit 1
	cd ..
}

function build_aac
{
	PLATFORM=$NDK/platforms/$PLATFORM_VERSION/arch-$ARCH/
	export PATH=${PATH}:$PREBUILT/bin/
	CROSS_COMPILE=$PREBUILT/bin/$EABIARCH-
	CFLAGS=$OPTIMIZE_CFLAGS
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
	    --prefix=$(pwd)/$PREFIX \
	    --host=$ARCH-linux \
	    --disable-dependency-tracking \
	    --disable-shared \
	    --enable-static \
	    --with-pic \
	    $ADDITIONAL_CONFIGURE_FLAG \
	    || exit 1

	make clean || exit 1
	make -j4 install || exit 1
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
	    --enable-runtime-cpudetect \
	    --extra-ldflags="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib  -nostdlib -lc -lm -ldl -llog -L$PREFIX/lib" \
	    --extra-cflags="-I$PREFIX/include" \
	    --disable-everything \
	    --enable-hwaccel=h264_vaapi \
	    --enable-hwaccel=h264_vaapi \
	    --enable-hwaccel=h264_dxva2 \
	    --enable-hwaccel=mpeg4_vaapi \
	    --enable-demuxer=mov \
	    --enable-demuxer=h264 \
	    --enable-demuxer=mpegvideo \
	    --enable-demuxer=h263 \
	    --enable-demuxer=mpegps \
	    --enable-demuxer=mjpeg \
	    --enable-demuxer=rtsp \
	    --enable-demuxer=rtp \
	    --enable-demuxer=hls \
	    --enable-muxer=rtsp \
	    --enable-muxer=mp4 \
	    --enable-muxer=mov \
	    --enable-muxer=mjpeg \
	    --enable-protocol=crypto \
	    --enable-protocol=jni \
	    --enable-protocol=file \
	    --enable-protocol=rtp \
	    --enable-protocol=tcp \
	    --enable-protocol=udp \
	    --enable-protocol=applehttp \
	    --enable-protocol=hls \
	    --enable-protocol=http \
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
	    --enable-encoder=mp2 \
	    --enable-decoder=mp2 \
	    --enable-encoder=libvo_amrwbenc \
	    --enable-decoder=amrwb \
	    --enable-muxer=mp2 \
	    --disable-doc \
	    --disable-ffplay \
	    --enable-decoders \
	    --enable-encoders \
	    --enable-parsers \
	    --enable-hwaccels \
	    --enable-muxers \
	    --disable-ffmpeg \
	    --disable-ffplay \
	    --disable-ffprobe \
	    --disable-ffserver \
	    --enable-zlib \
	    --disable-avfilter \
	    --disable-avdevice \
	    --enable-nonfree \
	    --enable-libvo-aacenc \
	    --enable-libvo-amrwbenc \
	    --enable-version3 \
	    --enable-memalign-hack \
	    --enable-asm \
	    $ADDITIONAL_CONFIGURE_FLAG \
	    || exit 1
	make clean || exit 1
	make -j4 install || exit 1

	$PREBUILT/bin/$EABIARCH-ld -rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib -L$PREFIX/lib  -soname libffmpeg.so -shared -nostdlib  -z,noexecstack -Bsymbolic --whole-archive --no-undefined -o $PREFIX/libffmpeg.so -lavcodec -lavformat -lavresample -lavutil -lswresample -lswscale -lvo-aacenc -lvo-amrwbenc -lc -lm -lz -ldl -llog  --warn-once  --dynamic-linker=/system/bin/linker -zmuldefs $PREBUILT/lib/gcc/$EABIARCH/4.4.3/libgcc.a || exit 1
	cd ..
}

#arm v5
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv5
OPTIMIZE_CFLAGS="-marm -march=$CPU"
PREFIX=../ffmpeg-build/armeabi
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-5
build_amr
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
build_amr
build_aac
build_one

#mips
EABIARCH=mipsel-linux-android
ARCH=mips
OPTIMIZE_CFLAGS="-EL -march=mips32 -mips32 -mhard-float"
PREFIX=../ffmpeg-build/mips
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/mipsel-linux-android-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-9
build_amr
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
build_amr
build_aac
build_one



#arm v7vfpv3-32
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv7-a
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=vfpv3 -marm -march=$CPU "
PREFIX=../ffmpeg-build/armeabi-v7a-vfpv3
ADDITIONAL_CONFIGURE_FLAG=
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-9
build_amr
build_aac
build_one
(cd ffmpeg-build/armeabi-v7a; ln -s ../armeabi-v7a-vfpv3/libffmpeg.so libffmpeg-vfpv3.so)

#arm v7n
EABIARCH=arm-linux-androideabi
ARCH=arm
CPU=armv7-a
OPTIMIZE_CFLAGS="-mfloat-abi=softfp -mfpu=neon -marm -march=$CPU -mtune=cortex-a8 -mthumb -D__thumb__ "
PREFIX=../ffmpeg-build/armeabi-v7a-neon
ADDITIONAL_CONFIGURE_FLAG=--enable-neon
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/$OS-x86
PLATFORM_VERSION=android-9
build_amr
build_aac
build_one
(cd ffmpeg-build/armeabi-v7a; ln -s ../armeabi-v7a-neon/libffmpeg.so libffmpeg-neon.so)
