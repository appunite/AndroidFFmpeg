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

import android.app.Activity;
import android.app.AlertDialog;
import android.app.AlertDialog.Builder;
import android.content.DialogInterface;
import android.content.pm.ActivityInfo;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ProgressBar;

import com.appunite.ffmpeg.FFmpegDisplay;
import com.appunite.ffmpeg.FFmpegError;
import com.appunite.ffmpeg.FFmpegListener;
import com.appunite.ffmpeg.FFmpegPlayer;

public class MainActivity extends Activity implements OnClickListener,
		FFmpegListener {

	private FFmpegPlayer mpegPlayer;
	private static boolean isSurfaceView = true;
	private ProgressBar progressBar;
	protected boolean mPlay;

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

		progressBar = (ProgressBar) this.findViewById(R.id.progress);

		View playPauseView = this.findViewById(R.id.play_pause);
		playPauseView.setOnClickListener(this);

		FFmpegDisplay videoView = (FFmpegDisplay) this
				.findViewById(R.id.video_view);
		this.mpegPlayer = new FFmpegPlayer(videoView, this, this);
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
		this.mpegPlayer.stop();
	}

	private void play() {
		Thread thread = new Thread(new Runnable() {

			@Override
			public void run() {
				try {
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
					mpegPlayer.resume();
					mPlay = true;

				} catch (final FFmpegError e) {
					e.printStackTrace();
					MainActivity.this.runOnUiThread(new Runnable() {

						@Override
						public void run() {

							String format = getResources().getString(
									R.string.main_could_not_open_stream);
							String message = String.format(format,
									e.getMessage());

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
						}
					});
				}
			}
		});
		thread.start();
	}

	@Override
	public void onClick(View v) {
		int viewId = v.getId();
		switch (viewId) {
		case R.id.play_pause:
			if (mPlay == true) {
				mpegPlayer.pause();
				mPlay = false;
			} else {
				mpegPlayer.resume();
				mPlay = true;
			}
			return;

		default:
			throw new RuntimeException();
		}
	}

	@Override
	public void onUpdateTime(int currentTimeS, int videoDurationS) {
		progressBar.setMax(videoDurationS);
		progressBar.setProgress(currentTimeS);
	}
}
