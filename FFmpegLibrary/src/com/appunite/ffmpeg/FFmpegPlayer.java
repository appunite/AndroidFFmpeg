package com.appunite.ffmpeg;

import android.graphics.Bitmap;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

public class FFmpegPlayer {
	private FFmpegListener mpegListener;
	private final RenderedFrame mRenderedFrame = new RenderedFrame();

	private int mNativePlayer;

	static {
		System.loadLibrary("ffmpeg");
		System.loadLibrary("ffmpeg-jni");
	}

	private native int setDataSourceNative(String url);

	private native void releaseFrameNative();

	private native Bitmap renderFrameNative();

	private native int playNative();

	private native void stopNative();

	private native int decodeVideoNative();

	private native int decodeAudioNative();

	public static class RenderedFrame {
		public Bitmap bitmap;
		public int height;
		public int width;
	}

	public FFmpegPlayer(FFmpegDisplay view) {
		view.setMpegPlayer(this);
	}

	private Bitmap prepareFrame(int width, int height) {
		// Bitmap bitmap =
		// Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
		Bitmap bitmap =
				Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565);
		this.mRenderedFrame.height = height;
		this.mRenderedFrame.width = width;
		return bitmap;
	}

	private AudioTrack prepareAudioTrack(int sampleRateInHz,
			int numberOfChannels) {

		int channelConfig;

		if (numberOfChannels == 1) {
			channelConfig = AudioFormat.CHANNEL_OUT_MONO;
		} else if (numberOfChannels == 2) {
			channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
		} else if (numberOfChannels == 3) {
			channelConfig =
					AudioFormat.CHANNEL_OUT_FRONT_CENTER
							| AudioFormat.CHANNEL_OUT_FRONT_RIGHT
							| AudioFormat.CHANNEL_OUT_FRONT_LEFT;
		} else if (numberOfChannels == 4) {
			channelConfig = AudioFormat.CHANNEL_OUT_QUAD;
		} else if (numberOfChannels == 5) {
			channelConfig =
					AudioFormat.CHANNEL_OUT_QUAD
							| AudioFormat.CHANNEL_OUT_LOW_FREQUENCY;
		} else if (numberOfChannels == 6) {
			channelConfig = AudioFormat.CHANNEL_OUT_5POINT1;
		} else if (numberOfChannels == 8) {
			channelConfig = AudioFormat.CHANNEL_OUT_7POINT1;
		} else {
			// TODO
			throw new RuntimeException(
					String.format(
							"Could not play audio track with this number of channels: %d",
							numberOfChannels));
		}
		int minBufferSize =
				AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig,
						AudioFormat.ENCODING_PCM_16BIT);
		AudioTrack audioTrack =
				new AudioTrack(AudioManager.STREAM_MUSIC, sampleRateInHz,
						channelConfig, AudioFormat.ENCODING_PCM_16BIT,
						minBufferSize, AudioTrack.MODE_STREAM);
		audioTrack.play();
		return audioTrack;
	}

	public void setVideoListener(FFmpegListener mpegListener) {
		this.mpegListener = mpegListener;
	}

	public void setDataSource(String url) throws FFmpegError {
		int err = this.setDataSourceNative(url);
		if (err != 0)
			throw new FFmpegError(err);
	}

	public void play() {
		Thread threadPlay = new Thread(new Runnable() {

			@Override
			public void run() {
				playNative();
			}
		});
		threadPlay.start();

		Thread threadDecodeAudio = new Thread(new Runnable() {

			@Override
			public void run() {
				decodeAudioNative();
			}
		});
		threadDecodeAudio.start();

		Thread threadDecodeVideo = new Thread(new Runnable() {

			@Override
			public void run() {
				decodeVideoNative();
			}
		});
		threadDecodeVideo.start();
	}

	public void stop() {
		this.stopNative();
	}

	public void playPause() {
		// TODO
	}

	public RenderedFrame renderFrame() {
		this.mRenderedFrame.bitmap = this.renderFrameNative();
		return this.mRenderedFrame;
	}

	public void releaseFrame() {
		this.releaseFrameNative();
	}
}
