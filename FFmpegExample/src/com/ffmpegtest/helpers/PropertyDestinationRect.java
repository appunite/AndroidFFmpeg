package com.ffmpegtest.helpers;

import com.appunite.ffmpeg.FFmpegSurfaceView;

import android.annotation.SuppressLint;
import android.graphics.RectF;
import android.util.Property;

@SuppressLint("NewApi")
public final class PropertyDestinationRect extends
		Property<FFmpegSurfaceView, RectF> {
	public PropertyDestinationRect() {
		super(RectF.class, "destinationRect");
	}

	@Override
	public RectF get(FFmpegSurfaceView object) {
		return object.getDestinationRect();
	}

	@Override
	public void set(FFmpegSurfaceView object, RectF value) {
		object.setDestinationRect(value);
	}
}