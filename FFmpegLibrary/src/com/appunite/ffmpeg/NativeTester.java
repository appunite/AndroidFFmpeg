package com.appunite.ffmpeg;

public class NativeTester {
	static {
		System.loadLibrary("nativetester");
	}
	
	native boolean isNeon();
	native boolean isVfpv3();
}
