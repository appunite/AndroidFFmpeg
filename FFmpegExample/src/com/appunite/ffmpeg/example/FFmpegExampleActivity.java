package com.appunite.ffmpeg.example;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

import android.app.AlertDialog;
import android.app.AlertDialog.Builder;
import android.content.Context;
import android.content.DialogInterface;
import android.os.Bundle;
import android.support.v4.app.FragmentActivity;
import android.support.v4.app.LoaderManager.LoaderCallbacks;
import android.support.v4.content.AsyncTaskLoader;
import android.support.v4.content.Loader;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;

import com.appunite.ffmpeg.FFmpeg;

public class FFmpegExampleActivity extends FragmentActivity implements
		LoaderCallbacks<List<File>> {
	private static class LoadVideoFilesTaskLoader extends
			AsyncTaskLoader<List<File>> {
		private final String[] mVideoFileSuffixes;
		private final String mDirectoryName;

		public LoadVideoFilesTaskLoader(Context context, String directoryName) {
			super(context);
			this.mDirectoryName = directoryName;
			this.mVideoFileSuffixes = context.getResources().getStringArray(
					R.array.video_file_suffixes);
		}

		private void getVideosFromDirectory(File directory,
				List<File> videoFiles) {
			File[] listFiles = directory.listFiles();
			for (File file : listFiles) {
				if (file.isDirectory()) {
					this.getVideosFromDirectory(file, videoFiles);
					continue;
				}
				if (!this.isVideoFile(file))
					continue;
				videoFiles.add(file);
			}
		}

		private boolean isVideoFile(File file) {
			String fileName = file.getName();
			for (String suffix : this.mVideoFileSuffixes) {
				if (fileName.endsWith(suffix))
					return true;
			}
			return false;
		}

		@Override
		public List<File> loadInBackground() {
			File directory = new File(this.mDirectoryName);
			if (!directory.isDirectory())
				return null;
			List<File> videoFiles = new ArrayList<File>();
			this.getVideosFromDirectory(directory, videoFiles);
			return videoFiles;
		}

		@Override
		protected void onStartLoading() {
			this.forceLoad();
		}

		@Override
		protected void onStopLoading() {
			this.cancelLoad();
		}

	}

	/** Called when the activity is first created. */

	private class VideosAdapter extends ArrayAdapter<File> {

		private class ViewHandler {
			TextView textView;
		}

		private final LayoutInflater mLayoutInflater;

		public VideosAdapter(Context context) {
			super(context, android.R.layout.simple_list_item_1);
			this.mLayoutInflater = (LayoutInflater) context
					.getSystemService(Context.LAYOUT_INFLATER_SERVICE);
		}

		@Override
		public View getView(int position, View convertView, ViewGroup parent) {
			ViewHandler viewHandler;
			View view;
			if (convertView == null) {
				view = this.mLayoutInflater.inflate(
						android.R.layout.simple_list_item_1, parent, false);
				viewHandler = new ViewHandler();
				view.setTag(viewHandler);

				viewHandler.textView = (TextView) view
						.findViewById(android.R.id.text1);
			} else {
				view = convertView;
				viewHandler = (ViewHandler) view.getTag();
			}

			File file = this.getItem(position);
			viewHandler.textView.setText(file.getName());
			return view;
		}

		public void setData(List<File> data) {
			this.clear();
			if (data != null) {
				for (File file : data) {
					this.add(file);
				}
			}
		}

	}

	private ListView mListView;
	private ProgressBar mProgressBar;

	private View mEmptyView;

	private VideosAdapter mAdapter;
	private TextView mFileDescTextView;

	protected void movieSelected(File item) {
		// could take some while and should be done in background
		String videoFilename = item.getAbsolutePath();
		String displayText;
		int error = FFmpeg.naInit(videoFilename);
		if (error == 0) {
			int[] prVideoRes = FFmpeg.naGetVideoResolution();
			String prVideoCodecName = FFmpeg.naGetVideoCodecName();
			String prVideoFormatName = FFmpeg.naGetVideoFormatName();
			FFmpeg.naClose();
			displayText = "FFmpeg Video: " + videoFilename + "\n";
			displayText += "Video Resolution: " + prVideoRes[0] + "x"
					+ prVideoRes[1] + "\n";
			displayText += "Video Codec: " + prVideoCodecName + "\n";
			displayText += "Video Format: " + prVideoFormatName + "\n";
		} else {
			displayText = "error: " + error;
		}
		this.mFileDescTextView.setText(displayText);
	}

	protected void nothingSelected() {
		this.mFileDescTextView.setText(R.string.select_item);
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		this.setContentView(R.layout.main);
		this.mListView = (ListView) this.findViewById(android.R.id.list);
		this.mProgressBar = (ProgressBar) this
				.findViewById(android.R.id.progress);
		this.mEmptyView = this.findViewById(android.R.id.empty);
		this.mFileDescTextView = (TextView) this
				.findViewById(android.R.id.text1);
		this.mAdapter = new VideosAdapter(this);
		this.mListView.setAdapter(this.mAdapter);
		this.mListView.setChoiceMode(ListView.CHOICE_MODE_NONE);
		this.mListView.setOnItemClickListener(new OnItemClickListener() {

			@Override
			public void onItemClick(AdapterView<?> parent, View view,
					int position, long id) {
				File item = FFmpegExampleActivity.this.mAdapter
						.getItem(position);
				FFmpegExampleActivity.this.movieSelected(item);
			}
		});
		this.getSupportLoaderManager().restartLoader(0x00, null, this);
	}

	@Override
	public Loader<List<File>> onCreateLoader(int arg0, Bundle arg1) {
		this.nothingSelected();
		this.mProgressBar.setVisibility(View.VISIBLE);
		this.mListView.setVisibility(View.GONE);
		this.mEmptyView.setVisibility(View.GONE);
		return new LoadVideoFilesTaskLoader(this, "/sdcard");
	}

	@Override
	public void onLoaderReset(Loader<List<File>> arg0) {
		this.mAdapter.setData(null);
		this.mProgressBar.setVisibility(View.VISIBLE);
		this.mListView.setVisibility(View.GONE);
		this.mEmptyView.setVisibility(View.GONE);
		this.nothingSelected();
	}

	@Override
	public void onLoadFinished(Loader<List<File>> loader, List<File> data) {
		this.nothingSelected();
		this.mAdapter.setData(data);
		this.mProgressBar.setVisibility(View.GONE);
		if (data != null && data.size() > 0) {
			this.mListView.setVisibility(View.VISIBLE);
			this.mEmptyView.setVisibility(View.GONE);
		} else {
			this.mListView.setVisibility(View.GONE);
			this.mEmptyView.setVisibility(View.VISIBLE);
		}

		if (data == null) {
			Builder builder = new AlertDialog.Builder(this);
			builder.setMessage(R.string.could_not_read_videos_directory);
			builder.setIcon(android.R.drawable.ic_dialog_alert);
			builder.setTitle(R.string.error);
			builder.setPositiveButton(android.R.string.ok,
					new DialogInterface.OnClickListener() {

						@Override
						public void onClick(DialogInterface dialog, int which) {
							dialog.cancel();
							FFmpegExampleActivity.this.finish();
						}
					});
			AlertDialog alertDialog = builder.create();
			alertDialog.show();
		}
	}

	@Override
	protected void onPause() {

		super.onPause();
	}

	@Override
	protected void onResume() {
		super.onResume();
	}
}