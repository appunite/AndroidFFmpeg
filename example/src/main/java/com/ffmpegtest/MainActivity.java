package com.ffmpegtest;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.View;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.EditText;
import android.widget.ListView;

import com.ffmpegtest.adapter.ItemsAdapter;
import com.ffmpegtest.adapter.VideoItem;

public class MainActivity extends Activity implements OnItemClickListener {

	private ItemsAdapter adapter;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.main_activity);

		final ListView listView = (ListView) findViewById(R.id.main_activity_list);
		final EditText editText = (EditText) findViewById(R.id.main_activity_video_url);
		final View button = findViewById(R.id.main_activity_play_button);

		final UserPreferences userPreferences = new UserPreferences(this);
		if (savedInstanceState == null) {
			editText.setText(userPreferences.getUrl());
		}
		adapter = new ItemsAdapter(LayoutInflater.from(this));
		adapter.swapItems(getVideoItems());

		listView.setAdapter(adapter);
		listView.setOnItemClickListener(this);

		button.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				final String url = String.valueOf(editText.getText());
				playVideo(url);
				userPreferences.setUrl(url);
			}
		});
	}

	private void playVideo(String url) {
		final Intent intent = new Intent(AppConstants.VIDEO_PLAY_ACTION)
                .putExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_URL, url);
		startActivity(intent);
	}

	@NonNull
	private List<VideoItem> getVideoItems() {
		final List<VideoItem> items = new ArrayList<VideoItem>();
		items.add(new VideoItem(
				items.size(),
				"\"localfile.mp4\" on sdcard",
				getSDCardFile("localfile.mp4"),
				null));
		items.add(new VideoItem(
				items.size(),
				"Apple sample",
				"http://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_4x3/bipbop_4x3_variant.m3u8",
				null));
		items.add(new VideoItem(
				items.size(),
				"Apple advenced sample",
				"https://devimages.apple.com.edgekey.net/resources/http-streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8",
				null));
		items.add(new VideoItem(
				items.size(),
				"IP camera",
				"rtsp://ip.appunite-local.net:554",
				null));
		return items;
	}

	private static String getSDCardFile(String file) {
		File videoFile = new File(Environment.getExternalStorageDirectory(),
				file);
		return "file://" + videoFile.getAbsolutePath();
	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		getMenuInflater().inflate(R.menu.main_activity, menu);
		return true;
	}

	@Override
	public void onItemClick(AdapterView<?> listView, View view, int position, long id) {
		final VideoItem videoItem = adapter.getItem(position);
		final Intent intent = new Intent(AppConstants.VIDEO_PLAY_ACTION)
				.putExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_URL, videoItem.video())
				.putExtra(AppConstants.VIDEO_PLAY_ACTION_EXTRA_ENCRYPTION_KEY, videoItem.video());
		startActivity(intent);
	}

}
