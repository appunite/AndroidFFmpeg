package com.appunite.ffmpeg;

public class FFmpegError extends Throwable {

	public FFmpegError(int err) {
		super(String.format("FFmpegPlayer error %d", err));
	}

	/**
	 * 
	 */
	private static final long serialVersionUID = 1L;

}
