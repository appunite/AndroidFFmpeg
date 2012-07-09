package com.appunite.ffmpeg;

public class NotPlayingException extends Exception {
	private static final long serialVersionUID = 1L;
	
	public NotPlayingException(String detailMessage, Throwable throwable) {
		super(detailMessage, throwable);
	}
	
	public NotPlayingException(String detailMessage) {
		super(detailMessage);
	}
	
	public NotPlayingException() {
		super();
	}

}
