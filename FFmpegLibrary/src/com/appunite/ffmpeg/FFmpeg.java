package com.appunite.ffmpeg;

public class FFmpeg {
	static {
		NativeTester nativeTester = new NativeTester();
		if (nativeTester.isNeon()) {
			System.loadLibrary("ffmpeg-neon");			
		} else if (nativeTester.isVfpv3()) {
			System.loadLibrary("ffmpeg-vfpv3");
		} else {
			System.loadLibrary("ffmpeg");			
		}
		System.loadLibrary("ffmpeg-jni");
	}
}
