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
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;

import android.annotation.TargetApi;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.AlertDialog.Builder;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemSelectedListener;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckedTextView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;

import com.appunite.ffmpeg.FFmpegDisplay;
import com.appunite.ffmpeg.FFmpegError;
import com.appunite.ffmpeg.FFmpegListener;
import com.appunite.ffmpeg.FFmpegPlayer;
import com.appunite.ffmpeg.FFmpegStreamInfo;
import com.appunite.ffmpeg.FFmpegStreamInfo.CodecType;
import com.appunite.ffmpeg.NotPlayingException;

public class VideoActivity extends Activity implements OnClickListener,
		FFmpegListener, OnSeekBarChangeListener, OnItemSelectedListener {

	private FFmpegPlayer mpegPlayer;
	private static boolean isSurfaceView = true;
	protected boolean mPlay = false;
	private View controlsView;
	private View loadingView;
	private SeekBar seekBar;
	private View videoView;
	private Button playPauseButton;
	private boolean mTracking = false;
	private View streamsView;
	private Spinner languageSpinner;
	private int languageSpinnerSelectedPosition = 0;
	private Spinner subtitleSpinner;
	private int subtitleSpinnerSelectedPosition = 0;
	private StreamAdapter languageAdapter;
	private StreamAdapter subtitleAdapter;

	private FFmpegStreamInfo audioStream = null;
	private FFmpegStreamInfo subtitleStream = null;

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
			this.setContentView(R.layout.video_surfaceview);
		else
			this.setContentView(R.layout.video_view);

		seekBar = (SeekBar) this.findViewById(R.id.seek_bar);
		seekBar.setOnSeekBarChangeListener(this);

		playPauseButton = (Button) this.findViewById(R.id.play_pause);
		playPauseButton.setOnClickListener(this);

		controlsView = this.findViewById(R.id.controls);
		streamsView = this.findViewById(R.id.streams);
		loadingView = this.findViewById(R.id.loading_view);
		languageSpinner = (Spinner) this.findViewById(R.id.language_spinner);
		subtitleSpinner = (Spinner) this.findViewById(R.id.subtitle_spinner);

		languageAdapter = new StreamAdapter(this,
				android.R.layout.simple_spinner_item,
				new ArrayList<FFmpegStreamInfo>(), StreamAdapterType.AUDIO);
		languageAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		languageSpinner.setAdapter(languageAdapter);
		languageSpinner.setOnItemSelectedListener(this);

		subtitleAdapter = new StreamAdapter(this,
				android.R.layout.simple_spinner_item,
				new ArrayList<FFmpegStreamInfo>(), StreamAdapterType.SUBTITLE);
		subtitleAdapter
				.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
		subtitleSpinner.setAdapter(subtitleAdapter);
		subtitleSpinner.setOnItemSelectedListener(this);

		videoView = this.findViewById(R.id.video_view);
		this.mpegPlayer = new FFmpegPlayer((FFmpegDisplay) videoView, this);
		this.mpegPlayer.setMpegListener(this);
		setDataSource();
	}

	private static enum StreamAdapterType {
		AUDIO, SUBTITLE
	};

	private static class StreamAdapter extends ArrayAdapter<FFmpegStreamInfo> {

		private final StreamAdapterType adapterType;

		public StreamAdapter(Context context, int textViewResourceId,
				List<FFmpegStreamInfo> objects, StreamAdapterType adapterType) {
			super(context, textViewResourceId, objects);
			this.adapterType = adapterType;
		}

		@Override
		public View getView(int position, View convertView, ViewGroup parent) {
			TextView view = (TextView) super.getView(position, convertView,
					parent);

			FFmpegStreamInfo item = getItem(position);
			Locale locale = item.getLanguage();

			String formatter;
			if (StreamAdapterType.AUDIO.equals(adapterType)) {
				formatter = getContext().getString(
						R.string.language_spinner_drop_down);
			} else {
				formatter = getContext().getString(
						R.string.subtitle_spinner_drop_down);
			}
			String languageName = locale == null ? getContext().getString(
					R.string.unknown) : locale.getDisplayLanguage();
			String text = String.format(formatter, languageName);
			view.setText(text);
			return view;
		}

		@Override
		public View getDropDownView(int position, View convertView,
				ViewGroup parent) {
			CheckedTextView view = (CheckedTextView) super.getDropDownView(
					position, convertView, parent);
			FFmpegStreamInfo item = getItem(position);
			Locale locale = item.getLanguage();
			String languageName = locale == null ? getContext().getString(
					R.string.unknown) : locale.getDisplayLanguage();
			view.setText(languageName);
			return view;
		}

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
		this.mpegPlayer.stop();
		stop();
	}

	private void setDataSource() {
		HashMap<String, String> params = new HashMap<String, String>();
		
		// set font for ass
		File assFont = new File(Environment.getExternalStorageDirectory(),
				"Roboto.ttf");
		params.put("ass_default_font_path", assFont.getAbsolutePath());
		
		Intent intent = getIntent();
		String url = intent
				.getStringExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_URL);
		if (url == null) {
			throw new IllegalArgumentException(String.format(
					"\"%s\" did not provided",
					AppConstants.VIDEO_PLAY_ACTION_EXTRA_URL));
		}
		if (intent
				.hasExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_ENCRYPTION_KEY)) {
			params.put("aeskey", intent.getStringExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_ENCRYPTION_KEY));
		}

		this.playPauseButton
		.setBackgroundResource(android.R.drawable.ic_media_play);
		this.playPauseButton.setEnabled(true);
		mPlay = false;

		mpegPlayer.setDataSource(url, params, null, audioStream,
				subtitleStream);

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
	public void onFFUpdateTime(int currentTimeS, int videoDurationS, boolean isFinished) {
		if (!mTracking) {
			seekBar.setMax(videoDurationS);
			seekBar.setProgress(currentTimeS);
		}
		
		if (isFinished) {
			new AlertDialog.Builder(this)
					.setTitle(R.string.dialog_end_of_video_title)
					.setMessage(R.string.dialog_end_of_video_message)
					.setCancelable(true).show();
		}
	}

	@Override
	public void onFFDataSourceLoaded(FFmpegError err, FFmpegStreamInfo[] streams) {
		if (err != null) {
			String format = getResources().getString(
					R.string.main_could_not_open_stream);
			String message = String.format(format, err.getMessage());

			Builder builder = new AlertDialog.Builder(VideoActivity.this);
			builder.setTitle(R.string.app_name)
					.setMessage(message)
					.setOnCancelListener(
							new DialogInterface.OnCancelListener() {

								@Override
								public void onCancel(DialogInterface dialog) {
									VideoActivity.this.finish();
								}
							}).show();
			return;
		}
		playPauseButton.setBackgroundResource(android.R.drawable.ic_media_play);
		playPauseButton.setEnabled(true);
		this.controlsView.setVisibility(View.VISIBLE);
		this.streamsView.setVisibility(View.VISIBLE);
		this.loadingView.setVisibility(View.GONE);
		this.videoView.setVisibility(View.VISIBLE);
		languageAdapter.clear();
		subtitleAdapter.clear();
		for (FFmpegStreamInfo streamInfo : streams) {
			CodecType mediaType = streamInfo.getMediaType();
			if (FFmpegStreamInfo.CodecType.AUDIO.equals(mediaType)) {
				languageAdapter.add(streamInfo);
			} else if (FFmpegStreamInfo.CodecType.SUBTITLE.equals(mediaType)) {
				subtitleAdapter.add(streamInfo);
			}
		}
	}

	private void displaySystemMenu(boolean visible) {
		if (Build.VERSION.SDK_INT >= 14) {
			displaySystemMenu14(visible);
		} else if (Build.VERSION.SDK_INT >= 11) {
			displaySystemMenu11(visible);
		}
	}

	@SuppressWarnings("deprecation")
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
			this.videoView
					.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);
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
		this.playPauseButton
				.setBackgroundResource(android.R.drawable.ic_media_pause);
		this.playPauseButton.setEnabled(true);

		displaySystemMenu(false);
		mPlay = true;
	}

	@Override
	public void onFFPause(NotPlayingException err) {
		this.playPauseButton
				.setBackgroundResource(android.R.drawable.ic_media_play);
		this.playPauseButton.setEnabled(true);
		mPlay = false;
	}

	private void stop() {
		this.controlsView.setVisibility(View.GONE);
		this.streamsView.setVisibility(View.GONE);
		this.loadingView.setVisibility(View.VISIBLE);
		this.videoView.setVisibility(View.INVISIBLE);
	}

	@Override
	public void onFFStop() {
	}

	@Override
	public void onFFSeeked(NotPlayingException result) {
//		if (result != null)
//			throw new RuntimeException(result);
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
	
	private void setDataSourceAndResumeState() {
		int progress = seekBar.getProgress();
		setDataSource();
		mpegPlayer.seek(progress);
		mpegPlayer.resume();
	}

	@Override
	public void onItemSelected(AdapterView<?> parentView,
			View selectedItemView, int position, long id) {
		FFmpegStreamInfo streamInfo = (FFmpegStreamInfo) parentView
				.getItemAtPosition(position);
		if (parentView == languageSpinner) {
			if (languageSpinnerSelectedPosition != position) {
				languageSpinnerSelectedPosition = position;
				audioStream = streamInfo;
				setDataSourceAndResumeState();
			}
		} else if (parentView == subtitleSpinner) {
			if (subtitleSpinnerSelectedPosition != position) {
				subtitleSpinnerSelectedPosition = position;
				subtitleStream = streamInfo;
				setDataSourceAndResumeState();
			}
		} else {
			throw new RuntimeException();
		}
	}

	@Override
	public void onNothingSelected(AdapterView<?> parentView) {
		// if (parentView == languageSpinner) {
		// audioStream = null;
		// } else if (parentView == subtitleSpinner) {
		// subtitleStream = null;
		// } else {
		// throw new RuntimeException();
		// }
		// play();
	}

}
