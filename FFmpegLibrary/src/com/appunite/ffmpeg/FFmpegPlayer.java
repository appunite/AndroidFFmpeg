/*
 * FFmpegPlayer.java
 * Copyright (c) 2012 Jacek Marchwicki
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package com.appunite.ffmpeg;

import java.util.Map;

import android.app.Activity;
import android.graphics.Bitmap;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.AsyncTask;

public class FFmpegPlayer {
	private static class StopTask extends AsyncTask<Void, Void, Void> {

		private final FFmpegPlayer player;

		public StopTask(FFmpegPlayer player) {
			this.player = player;
		}

		@Override
		protected Void doInBackground(Void... params) {
			player.stopNative();
			return null;
		}

		@Override
		protected void onPostExecute(Void result) {
			if (player.mpegListener != null)
				player.mpegListener.onFFStop();
		}

	}

	private static class SetDataSourceTaskResult {
		FFmpegError error;
		FFmpegStreamInfo[] streams;
	}

	private static class SetDataSourceTask extends
			AsyncTask<Object, Void, SetDataSourceTaskResult> {

		private final FFmpegPlayer player;

		public SetDataSourceTask(FFmpegPlayer player) {
			this.player = player;
		}

		@Override
		protected SetDataSourceTaskResult doInBackground(Object... params) {
			String url = (String) params[0];
			@SuppressWarnings("unchecked")
			Map<String, String> map = (Map<String, String>) params[1];
			FFmpegStreamInfo videoStream = (FFmpegStreamInfo) params[2];
			FFmpegStreamInfo audioStream = (FFmpegStreamInfo) params[3];
			FFmpegStreamInfo subtitleStream = (FFmpegStreamInfo) params[4];
			
			int videoStreamNo = videoStream == null ? -1 : videoStream.getStreamNumber();
			int audioStreamNo = audioStream == null ? -1 : audioStream.getStreamNumber();
			int subtitleStreamNo = subtitleStream == null ? -1 : subtitleStream.getStreamNumber();
			
			int err = player.setDataSourceNative(url, map, videoStreamNo, audioStreamNo, subtitleStreamNo);
			SetDataSourceTaskResult result = new SetDataSourceTaskResult();
			if (err < 0) {
				result.error = new FFmpegError(err);
				result.streams = null;
			} else {
				result.error = null;
				result.streams = player.getStreamsInfo();
			}
			return result;
		}

		@Override
		protected void onPostExecute(SetDataSourceTaskResult result) {
			if (player.mpegListener != null)
				player.mpegListener.onFFDataSourceLoaded(result.error,
						result.streams);
		}

	}

	private static class SeekTask extends
			AsyncTask<Integer, Void, NotPlayingException> {

		private final FFmpegPlayer player;

		public SeekTask(FFmpegPlayer player) {
			this.player = player;
		}

		@Override
		protected NotPlayingException doInBackground(Integer... params) {
			try {
				player.seekNative(params[0].intValue());
			} catch (NotPlayingException e) {
				return e;
			}
			return null;
		}

		@Override
		protected void onPostExecute(NotPlayingException result) {
			if (player.mpegListener != null)
				player.mpegListener.onFFSeeked(result);
		}

	}

	private static class PauseTask extends
			AsyncTask<Void, Void, NotPlayingException> {

		private final FFmpegPlayer player;

		public PauseTask(FFmpegPlayer player) {
			this.player = player;
		}

		@Override
		protected NotPlayingException doInBackground(Void... params) {
			try {
				player.pauseNative();
				return null;
			} catch (NotPlayingException e) {
				return e;
			}
		}

		@Override
		protected void onPostExecute(NotPlayingException result) {
			if (player.mpegListener != null)
				player.mpegListener.onFFPause(result);
		}

	}

	private static class ResumeTask extends
			AsyncTask<Void, Void, NotPlayingException> {

		private final FFmpegPlayer player;

		public ResumeTask(FFmpegPlayer player) {
			this.player = player;
		}

		@Override
		protected NotPlayingException doInBackground(Void... params) {
			try {
				player.resumeNative();
				return null;
			} catch (NotPlayingException e) {
				return e;
			}
		}

		@Override
		protected void onPostExecute(NotPlayingException result) {
			if (player.mpegListener != null)
				player.mpegListener.onFFResume(result);
		}

	}

	static {
		NativeTester nativeTester = new NativeTester();
		if (nativeTester.isNeon()) {
			System.loadLibrary("ffmpeg-neon");
			System.loadLibrary("ffmpeg-jni-neon");
		} else {
			System.loadLibrary("ffmpeg");
			System.loadLibrary("ffmpeg-jni");
		}
	}

	private FFmpegListener mpegListener = null;
	private final RenderedFrame mRenderedFrame = new RenderedFrame();

	private int mNativePlayer;
	private final Activity activity;

	private Runnable updateTimeRunnable = new Runnable() {

		@Override
		public void run() {
			if (mpegListener != null) {
				mpegListener.onFFUpdateTime(mCurrentTimeS,
					mVideoDurationS, mIsFinished);
			}
		}

	};

	private int mCurrentTimeS;
	private int mVideoDurationS;
	private FFmpegStreamInfo[] mStreamsInfos = null;
	private boolean mIsFinished = false;

	static class RenderedFrame {
		public Bitmap bitmap;
		public int height;
		public int width;
	}

	public FFmpegPlayer(FFmpegDisplay videoView, Activity activity) {
		this.activity = activity;
		int error = initNative();
		if (error != 0)
			throw new RuntimeException(String.format(
					"Could not initialize player: %d", error));
		videoView.setMpegPlayer(this);
	}

	@Override
	protected void finalize() throws Throwable {
		deallocNative();
		super.finalize();
	}

	private native int initNative();

	private native void deallocNative();

	private native int setDataSourceNative(String url,
			Map<String, String> dictionary, int videoStreamNo,
			int audioStreamNo, int subtitleStreamNo);

	private native void stopNative();

	native void renderFrameStart();

	native void renderFrameStop();

	native Bitmap renderFrameNative() throws InterruptedException;

	native void releaseFrame();

	private native void seekNative(int position) throws NotPlayingException;

	private native int getVideoDurationNative();

	/**
	 * 
	 * @param streamsInfos
	 *            - could be null
	 */
	private void setStreamsInfo(FFmpegStreamInfo[] streamsInfos) {
		this.mStreamsInfos = streamsInfos;
	}

	/**
	 * Return streamsInfo
	 * 
	 * @return return streams info after successful setDataSource or null
	 */
	protected FFmpegStreamInfo[] getStreamsInfo() {
		return mStreamsInfos;
	}

	public void stop() {
		new StopTask(this).execute();
	}

	private native void pauseNative() throws NotPlayingException;

	private native void resumeNative() throws NotPlayingException;

	public void pause() {
		new PauseTask(this).execute();
	}

	public void seek(int position) {
		new SeekTask(this).execute(Integer.valueOf(position));
	}

	public void resume() {
		new ResumeTask(this).execute();
	}

	private Bitmap prepareFrame(int width, int height) {
		// Bitmap bitmap =
		// Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
		Bitmap bitmap = Bitmap.createBitmap(width, height,
				Bitmap.Config.RGB_565);
		this.mRenderedFrame.height = height;
		this.mRenderedFrame.width = width;
		return bitmap;
	}

	private void onUpdateTime(int currentSec, int maxSec, boolean isFinished) {

		this.mCurrentTimeS = currentSec;
		this.mVideoDurationS = maxSec;
		this.mIsFinished  = isFinished;
		activity.runOnUiThread(updateTimeRunnable);
	}

	private AudioTrack prepareAudioTrack(int sampleRateInHz,
			int numberOfChannels) {

		for (;;) {
			int channelConfig;
			if (numberOfChannels == 1) {
				channelConfig = AudioFormat.CHANNEL_OUT_MONO;
			} else if (numberOfChannels == 2) {
				channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
			} else if (numberOfChannels == 3) {
				channelConfig = AudioFormat.CHANNEL_OUT_FRONT_CENTER
						| AudioFormat.CHANNEL_OUT_FRONT_RIGHT
						| AudioFormat.CHANNEL_OUT_FRONT_LEFT;
			} else if (numberOfChannels == 4) {
				channelConfig = AudioFormat.CHANNEL_OUT_QUAD;
			} else if (numberOfChannels == 5) {
				channelConfig = AudioFormat.CHANNEL_OUT_QUAD
						| AudioFormat.CHANNEL_OUT_LOW_FREQUENCY;
			} else if (numberOfChannels == 6) {
				channelConfig = AudioFormat.CHANNEL_OUT_5POINT1;
			} else if (numberOfChannels == 8) {
				channelConfig = AudioFormat.CHANNEL_OUT_7POINT1;
			} else {
				channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
			}
			try {
				int minBufferSize = AudioTrack.getMinBufferSize(sampleRateInHz,
						channelConfig, AudioFormat.ENCODING_PCM_16BIT);
				AudioTrack audioTrack = new AudioTrack(
						AudioManager.STREAM_MUSIC, sampleRateInHz,
						channelConfig, AudioFormat.ENCODING_PCM_16BIT,
						minBufferSize, AudioTrack.MODE_STREAM);
				return audioTrack;
			} catch (IllegalArgumentException e) {
				if (numberOfChannels > 2) {
					numberOfChannels = 2;
				} else if (numberOfChannels > 1) {
					numberOfChannels = 1;
				} else {
					throw e;
				}
			}
		}
	}

	private void setVideoListener(FFmpegListener mpegListener) {
		this.setMpegListener(mpegListener);
	}

	public void setDataSource(String url) {
		setDataSource(url, null, null, null, null);
	}

	public void setDataSource(String url, Map<String, String> dictionary,
			FFmpegStreamInfo videoStream, FFmpegStreamInfo audioStream,
			FFmpegStreamInfo subtitlesStream) {
		new SetDataSourceTask(this).execute(url, dictionary, videoStream, audioStream, subtitlesStream);
	}

	RenderedFrame renderFrame() throws InterruptedException {
		this.mRenderedFrame.bitmap = this.renderFrameNative();
		return this.mRenderedFrame;
	}

	public FFmpegListener getMpegListener() {
		return mpegListener;
	}

	public void setMpegListener(FFmpegListener mpegListener) {
		this.mpegListener = mpegListener;
	}
}
