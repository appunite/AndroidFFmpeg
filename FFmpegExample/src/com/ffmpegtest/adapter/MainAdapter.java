package com.ffmpegtest.adapter;

import com.ffmpegtest.R;

import android.content.Context;
import android.database.Cursor;
import android.provider.BaseColumns;
import android.support.v4.widget.CursorAdapter;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

public class MainAdapter extends CursorAdapter {
	
	public static final String[] PROJECTION = {BaseColumns._ID, "name", "url", "encryption_key"};
	private static int PROJECTION_NAME = 1;
	public static int PROJECTION_URL = 2;
	public static int PROJECTION_ENCRYPTION_KEY = 3;
	private LayoutInflater mInflater;
	
	public static class ViewHolder {
		TextView text1;
	}

	public MainAdapter(Context context) {
		super(context, null, 0);
		mInflater = LayoutInflater.from(context);
	}

	@Override
	public void bindView(View view, Context context, Cursor cursor) {
		ViewHolder holder = (ViewHolder) view.getTag();
		String name = cursor.getString(PROJECTION_NAME);
		holder.text1.setText(name);
	}

	@Override
	public View newView(Context context, Cursor cursor, ViewGroup container) {
		ViewHolder holder = new ViewHolder();
		View view = mInflater.inflate(R.layout.main_list_item, container, false);
		holder.text1 = (TextView) view.findViewById(android.R.id.text1);
		
		view.setTag(holder);
		return view;
	}

}
