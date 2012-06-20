# AndroidFFmpegLibrary
This project aims to create **working** library providing playing and converting video files in android via ffmpeg libraries.
 
## Info
This project aim to simplify compilation of FFmpeg for android diffrent architecutres to one big apk file.

## License
Coypyright (C) 2012 Appunite.com
Licensed under the Apache License, Verision 2.0

FFmpeg, libvo-aacenc, vo-amrwbenc, yuv2rgb and others libraries projects are distributed on theirs own license.

## Patent desclimer
We do not grant of patent rigts.
Some codecs use patented techniques and before use those parts of library you have to buy thrid-party patents.

## Pre-requirments
on mac: you have to install xcode and command tools from xcode preferences
you have to install (on mac you can use brew command from homebrew):
you have to install:
- autoconf
- autoconf-archive
- automake

on Debian/Ubuntu - you can use apt-get

on Mac - you can use tool brew from homebrew project. You have additionally install xcode. 


## Instalation
downloading source code 

	git clone git://github.com/appunite/AndroidFFmpeg.git AndroidFFmpeg
	cd AndroidFFmpeg
	git submodule init
	git submodule update
	cd FFmpegLibrary
	cd jni

setup vo-aacenc environment

	cd vo-aacenc
	autoreconf
	cd ..

setup vo-amrwbenc environment

	cd vo-amrwbenc
	autoreconf
	cd ..

build external libraries

	export NDK=/your/path/to/android-ndk
	./build_android.sh
	
make sure that files FFmpegLibrary/libs/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble

build ndk jni library

	ndk-build

build your project

	android update lib-project -p FFmpegLibrary
	android update project -p FFmpegExample
	cd FFmpegExample
	ant debug
	ant installd

or create new projects from FFmpegLibrary and FFmpegExample source directories in your eclipse. 
Run FFmpegExample as your android project.
If you have adt >= 20.0 you can click right mouse button on project and FFmpegLibrary project and "Android->Add native support".

## More codecs
If you nead more codecs:
- edit build_android.sh
- add more codecs in ffmpeg configuration section
- remove old ffmpeg-build directory by

		rm -r ffmpeg-build
	
- build ffmpeg end supporting libraries

		./build_android.sh
		
	During this process make sure that ffmpeg configuation goes without error.
	
	After build make sure that files FFmpegLibrary/libs/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble

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
