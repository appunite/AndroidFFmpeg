package com.appunite.ffmpeg;

public class FFmpeg {
	static {
		System.loadLibrary("ffmpeg");
		System.loadLibrary("ffmpeg-jni");
	}

	public static native void naClose();

	public static native int naConvertMovToMpeg(String inputFileName,
			String outputFileName);

	public static native int naConvertMpegToMov(String inputFileName,
			String outputFileName);

	public static native String naGetVideoCodecName();

	public static native String naGetVideoFormatName();

	public static native int[] naGetVideoResolution();

	public static native int naInit(String _videoFileName);
}
