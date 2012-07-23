/*
 * MainActivity.java
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

package com.ffmpegtest;

import java.io.File;

import android.annotation.TargetApi;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.AlertDialog.Builder;
import android.content.DialogInterface;
import android.content.pm.ActivityInfo;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;

import com.appunite.ffmpeg.FFmpegDisplay;
import com.appunite.ffmpeg.FFmpegError;
import com.appunite.ffmpeg.FFmpegListener;
import com.appunite.ffmpeg.FFmpegPlayer;
import com.appunite.ffmpeg.NotPlayingException;

public class MainActivity extends Activity implements OnClickListener,
		FFmpegListener, OnSeekBarChangeListener {

	private FFmpegPlayer mpegPlayer;
	private static boolean isSurfaceView = true;
	protected boolean mPlay = false;
	private View controlsView;
	private View loadingView;
	private SeekBar seekBar;
	private View videoView;
	private Button playPauseButton;
	private boolean mTracking = false;

	@Override
	public void onCreate(Bundle savedInstanceState) {
		this.getWindow().requestFeature(Window.FEATURE_NO_TITLE);
		getWindow().setFormat(PixelFormat.RGB_565);
		getWindow().clearFlags(WindowManager.LayoutParams.FLAG_DITHER);

		super.onCreate(savedInstanceState);

		this.getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
		this.getWindow().clearFlags(
				WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
		this.getWindow().setBackgroundDrawable(null);

		this.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

		if (isSurfaceView)
			this.setContentView(R.layout.main_surfaceview);
		else
			this.setContentView(R.layout.main_view);

		seekBar = (SeekBar) this.findViewById(R.id.seek_bar);
		seekBar.setOnSeekBarChangeListener(this);

		playPauseButton = (Button) this.findViewById(R.id.play_pause);
		playPauseButton.setOnClickListener(this);
		
		controlsView = this.findViewById(R.id.controls);
		loadingView = this.findViewById(R.id.loading_view);

		videoView = this
				.findViewById(R.id.video_view);
		this.mpegPlayer = new FFmpegPlayer( (FFmpegDisplay) videoView, this);
		this.mpegPlayer.setMpegListener(this);
		play();
	}

	@Override
	protected void onPause() {
		super.onPause();
	};

	@Override
	protected void onResume() {
		super.onResume();
	}
	
	@Override
	protected void onDestroy() {
		super.onDestroy();
		this.mpegPlayer.setMpegListener(null);
		stop();
	}

	private void play() {
		boolean http = true;
		boolean encrypted = false;
		// String video = "trailer.mp4";
		// String video = "sample.mp4";
		// String video = "HungerGamesTrailer1200.mp4";
		// String video = "HungerGamesTrailer1200.mp4.enc";
		String video = "HungerGamesTrailer800.mp4";
		// String video = "HungerGamesTrailer800.mp4.enc";
		String host = "192.168.1.105";
		String url;
		String base;
		if (encrypted) {
			base = "inflight";
		} else {
			base = "vod";
		}
		if (http) {
			url = String.format(
					"http://%s:1935/%s/mp4:%s/playlist.m3u8", host,
					base, video);
		} else {
			url = String.format("rtsp://%s:1935/%s/mp4:%s", host,
					base, video);
		}
		// url =
		// "http://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_4x3/bipbop_4x3_variant.m3u8";
		// // apple simple
		// url =
		// "https://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8";
		// // apple advanced
		File missionFile = new File(Environment.getExternalStorageDirectory(), "mission.mp4");
		url = missionFile.getAbsolutePath();
		
		mpegPlayer.setDataSource(url);
		
	}

	@Override
	public void onClick(View v) {
		int viewId = v.getId();
		switch (viewId) {
		case R.id.play_pause:
			resumePause();
			return;

		default:
			throw new RuntimeException();
		}
	}

	@Override
	public void onFFUpdateTime(int currentTimeS, int videoDurationS) {
		if (!mTracking) {
			seekBar.setMax(videoDurationS);
			seekBar.setProgress(currentTimeS);
		}
	}

	@Override
	public void onFFDataSourceLoaded(FFmpegError err) {
		if (err != null) {
			String format = getResources().getString(
					R.string.main_could_not_open_stream);
			String message = String.format(format,
					err.getMessage());

			Builder builder = new AlertDialog.Builder(
					MainActivity.this);
			builder.setTitle(R.string.app_name)
					.setMessage(message)
					.setOnCancelListener(
							new DialogInterface.OnCancelListener() {

								@Override
								public void onCancel(
										DialogInterface dialog) {
									MainActivity.this.finish();
								}
							}).show();
			return;
		}
		playPauseButton.setBackgroundResource(android.R.drawable.ic_media_play);
		playPauseButton.setEnabled(true);
		this.controlsView.setVisibility(View.VISIBLE);
		this.loadingView.setVisibility(View.GONE);
		this.videoView.setVisibility(View.VISIBLE);
	}
	
	private void displaySystemMenu(boolean visible) {
		if (Build.VERSION.SDK_INT >= 14) {
			displaySystemMenu14(visible);
		} else if (Build.VERSION.SDK_INT >= 11) {
			displaySystemMenu11(visible);
		}
	}
	
	@TargetApi(11)
	private void displaySystemMenu11(boolean visible) {
		if (visible) {
			this.videoView.setSystemUiVisibility(View.STATUS_BAR_VISIBLE);	
		} else {
			this.videoView.setSystemUiVisibility(View.STATUS_BAR_HIDDEN);			
		}
	}
	
	@TargetApi(14)
	private void displaySystemMenu14(boolean visible) {
		if (visible) {
			this.videoView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_VISIBLE);
		} else {
			this.videoView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);	
		}
	}
	
	public void resumePause() {
		this.playPauseButton.setEnabled(false);
		if (mPlay) {
			mpegPlayer.pause();
		} else {
			mpegPlayer.resume();
			displaySystemMenu(true);
		}
		mPlay = !mPlay;
	}

	@Override
	public void onFFResume(NotPlayingException result) {
		this.playPauseButton.setBackgroundResource(android.R.drawable.ic_media_pause);
		this.playPauseButton.setEnabled(true);

		displaySystemMenu(false);
		mPlay = true;
	}

	@Override
	public void onFFPause(NotPlayingException err) {
		this.playPauseButton.setBackgroundResource(android.R.drawable.ic_media_play);
		this.playPauseButton.setEnabled(true);
		mPlay = false;
	}
	
	private void stop() {
		this.controlsView.setVisibility(View.GONE);
		this.loadingView.setVisibility(View.VISIBLE);
		this.videoView.setVisibility(View.INVISIBLE);
	}

	@Override
	public void onFFStop() {
	}

	@Override
	public void onFFSeeked(NotPlayingException result) {
		if (result != null)
			throw new RuntimeException(result);
	}

	@Override
	public void onProgressChanged(SeekBar seekBar, int progress,
			boolean fromUser) {
		if (fromUser) {
			mpegPlayer.seek(progress);
		}
	}

	@Override
	public void onStartTrackingTouch(SeekBar seekBar) {
		mTracking = true;
	}

	@Override
	public void onStopTrackingTouch(SeekBar seekBar) {
		mTracking = false;
	}
}
