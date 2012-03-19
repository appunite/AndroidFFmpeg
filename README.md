INFO
==============
This project aim to simplify compilation of FFmpeg for android diffrent architecutres to one big apk

LICENSE
==============
This application and library are distributed on the Apache 2.0 License (file LICENSE), but ffmpeg project are distributed on its own license.

INSTALATION
==============
git clone git://github.com/appunite/AndroidFFmpeg.git AndroidFFmpeg
cd AndroidFFmpeg
git submodule init
git submodule update
cd FFmpegLibrary
cd jni
cd vo-aacenc
autoreconf
cd ..
export NDK=/your/path/to/android-ndk
./build_android.sh
make sure that files FFmpegLibrary/libs/{armeabi,armeabi-v7a,x86}/libffmpeg.so was created, otherwise you are in truble

Import FFmpegLibrary and FFmpegExample to your eclipse
Run FFmpegExample as your android project 

CREDITS
=============
Port made by Jacek Marchwicki from Appunite.com

Thanks to roman10 for writing blog: http://www.roman10.net/how-to-port-ffmpeg-the-program-to-androidideas-and-thoughts/
Thanks to ffmpeg team for writing cool stuff http://ffmpeg.org
Thanks to Alvaro for writing blog: http://odroid.foros-phpbb.com/t338-ffmpeg-compiled-with-android-ndk
Thanks to best-video-player for sample code: http://code.google.com/p/best-video-player/
