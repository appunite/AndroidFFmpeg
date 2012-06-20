package com.ffmpegtest;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.AlertDialog.Builder;
import android.content.DialogInterface;
import android.content.pm.ActivityInfo;
import android.graphics.PixelFormat;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.Window;
import android.view.WindowManager;
import android.widget.ProgressBar;

import com.appunite.ffmpeg.FFmpegDisplay;
import com.appunite.ffmpeg.FFmpegError;
import com.appunite.ffmpeg.FFmpegListener;
import com.appunite.ffmpeg.FFmpegPlayer;
import com.appunite.ffmpeg.FFmpegView;

public class MainActivity extends Activity implements OnClickListener,
		FFmpegListener {

	private FFmpegPlayer mpegPlayer;
	private static boolean isSurfaceView = true;

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


		View playPauseView = this.findViewById(R.id.play_pause);
		playPauseView.setOnClickListener(this);

		FFmpegDisplay videoView = (FFmpegDisplay) this.findViewById(R.id.video_view);
		this.mpegPlayer = new FFmpegPlayer(videoView);

		this.mpegPlayer.setVideoListener(this);
	}

	@Override
	protected void onPause() {
		super.onPause();
		this.mpegPlayer.stop();
	};

	@Override
	protected void onResume() {
		super.onResume();
		try {
			boolean http = true;
//			String video = "trailer.mp4";
//			String video = "sample.mp4";
			String video = "HungerGamesTrailer1200.mp4";
			//String video = "HungerGamesTrailer1200.mp4.enc";
			//String video = "HungerGamesTrailer800.mp4";
			//String video = "HungerGamesTrailer800.mp4.enc";
			String url;
			if (http) {
				url =
						String.format(
								"http://192.168.1.116:1935/vod/mp4:%s/playlist.m3u8",
								video);
			} else {
				url =
						String.format("rtsp://192.168.1.116:1935/vod/mp4:%s",
								video);
			}
			this.mpegPlayer.setDataSource(url);
			// this.mpegPlayer
			// .setDataSource("http://192.168.1.116:1935/vod/mp4:trailer.mp4/playlist.m3u8");
			// this.mpegPlayer
			// .setDataSource("ffp");
			// this.mpegPlayer
			// .setDataSource("rtsp://192.168.1.116:1935/vod/mp4:trailer.mp4");
			// this.mpegPlayer
			// .setDataSource("/sdcard/Movies/App207/MOV__20120508_121616.mov");

			this.mpegPlayer.play();
			
		} catch (FFmpegError e) {

			String format =
					this.getResources().getString(
							R.string.main_could_not_open_stream);
			String message = String.format(format, e.getMessage());

			Builder builder = new AlertDialog.Builder(this);
			builder.setTitle(R.string.app_name)
					.setMessage(message)
					.setOnCancelListener(
							new DialogInterface.OnCancelListener() {

								@Override
								public void onCancel(DialogInterface dialog) {
									MainActivity.this.finish();
								}
							}).show();
			e.printStackTrace();
		}
	}

	@Override
	public void onClick(View v) {
		int viewId = v.getId();
		switch (viewId) {
		case R.id.play_pause:
			this.mpegPlayer.playPause();
			return;

		default:
			throw new RuntimeException();
		}
	}
}
