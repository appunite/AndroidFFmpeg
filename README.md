# AndroidFFmpegLibrary
This project aims to create **working** library providing playing video files in android via ffmpeg libraries. With some effort and NDK knowledge you can use this ffmpeg libraries build to convert video files.
We rather want to use ffmpeg library without modifications to facilitate updating of ffmpeg core.

![Application screenshot](http://s12.postimage.org/o528w8jst/Screenshot1.png)

This project aim to simplify compilation of FFmpeg for android different architectures to one big apk file.

I'm afraid this project is not prepared for android beginners - build it and using it requires some NDK skills. 

[![Build Status](https://travis-ci.org/appunite/AndroidFFmpeg.svg?branch=master)](https://travis-ci.org/appunite/AndroidFFmpeg)

## License
Copyright (C) 2012 Appunite.com
Licensed under the Apache License, Verision 2.0

FFmpeg, libvo-aacenc, vo-amrwbenc, libyuv and others libraries projects are distributed on theirs own license.

## Patent disclaimer
We do not grant of patent rights.
Some codecs use patented techniques and before use those parts of library you have to buy thrid-party patents.

## Pre-requirments
on mac: you have to install xcode and command tools from xcode preferences
you have to install (on mac you can use brew command from homebrew):
you have to install:
- autoconf
- libtool
- make
- autoconf-archive
- automake
- pkg-config
- git

on Debian/Ubuntu - you can use apt-get

on Mac - you can use tool brew from homebrew project. You have additionally install xcode. 

## Bug reporting and questions

**Please read instruciton very carefully**. A lot of people had trouble because they did not read this manual with attention. **If you have some problems or questions do not send me emails**. First: look on past issues on github. Than: try figure out problem with google. If you did not find solution then you can ask on github issue tracker.

## Installation

### Go to the work
downloading source code 

	git clone https://review.appunite.com/androidffmpeg AndroidFFmpeg
	cd AndroidFFmpeg
	git submodule init
	git submodule sync #if you are updating source code
	git submodule update
	cd library-jni
	cd jni

download libyuv and configure libs

	./fetch.sh

build external libraries
Download r8e ndk: https://dl.google.com/android/ndk/android-ndk-r8e-darwin-x86_64.tar.bz2 or
ttps://dl.google.com/android/ndk/android-ndk-r8e-linux-x86_64.tar.bz2

	export NDK=/your/path/to/android-ndk
	./build_android.sh
	
make sure that files library-jni/jni/ffmpeg-build/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble


build ndk jni library (in `library-jni` directory)

	export PATH="${PATH}:${NDK}"
	ndk-build

make sure that files library-jni/libs/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble

build your project

	./gradlew build

## More codecs
If you need more codecs:
- edit build_android.sh
- add more codecs in ffmpeg configuration section
- remove old ffmpeg-build directory by

		rm -r ffmpeg-build
	
- build ffmpeg end supporting libraries

		./build_android.sh
		
	During this process make sure that ffmpeg configuration goes without error.
	
	After build make sure that files FFmpegLibrary/jni/ffmpeg-build/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble

- build your ndk library

		ndk-build

- refresh your FFmpegLibrary project in eclipse!!!!
- build your FFmpegExample project 


## Credits
Library made by Jacek Marchwicki from Appunite.com

- Thanks to Martin Böhme for writing tutorial: http://www.inb.uni-luebeck.de/~boehme/libavcodec_update.html
- Thanks to Stephen Dranger for writing tutorial: http://dranger.com/ffmpeg/
- Thanks to Liu Feipeng for writing blog: http://www.roman10.net/how-to-port-ffmpeg-the-program-to-androidideas-and-thoughts/
- Thanks to ffmpeg team for writing cool stuff http://ffmpeg.org
- Thanks to Alvaro for writing blog: http://odroid.foros-phpbb.com/t338-ffmpeg-compiled-with-android-ndk
- Thanks to android-fplayer for sample code: http://code.google.com/p/android-fplayer/
- Thanks to best-video-player for sample code: http://code.google.com/p/best-video-player/
- Thanks to Robin Watts for his work in yuv2rgb converter http://wss.co.uk/pinknoise/yuv2rgb/
- Thanks to Mohamed Naufal (https://github.com/hexene) and Martin Storsjö (https://github.com/mstorsjo) for theirs work on sample code for stagefright/openmax integration layer.
- Thanks www.fourcc.org for theirs http://www.fourcc.org/yuv.php page
- Thanks to Cedric Fungfor his blog bost: http://vec.io/posts/use-android-hardware-decoder-with-omxcodec-in-ndk
- Thanks Google/Google chrome/Chromium teams for libyuv library https://code.google.com/p/libyuv/
- Thanks to Picker Wengs for this slides about android multimedia stack http://www.slideshare.net/pickerweng/android-multimedia-framework
