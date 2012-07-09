/*
 * FFmpegView.java
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

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.View;

import com.appunite.ffmpeg.FFmpegPlayer.RenderedFrame;

public class FFmpegView extends View implements FFmpegDisplay {

	// private static final String TAG = FFmpegView.class.getCanonicalName();
	private FFmpegPlayer mpegPlayer = null;
	private FpsCounter fpsCounter;
	private Paint mPaint;

	public FFmpegView(Context context) {
		super(context);
		this.init();
	}

	private void init() {
		this.mPaint = new Paint();
		this.mPaint.setTextSize(32);
		this.mPaint.setColor(Color.RED);
		this.fpsCounter = new FpsCounter(10);
	}

	public FFmpegView(Context context, AttributeSet attrs) {
		super(context, attrs);
		this.init();
	}

	public FFmpegView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
		this.init();
	}

	@Override
	public void setMpegPlayer(FFmpegPlayer mpegPlayer) {
		this.mpegPlayer = mpegPlayer;
		this.invalidate();
	}
	
	@Override
	protected void onAttachedToWindow() {
		super.onAttachedToWindow();
		
		this.mpegPlayer.renderFrameStart();
	}
	
	@Override
	protected void onDetachedFromWindow() {
		super.onDetachedFromWindow();
		
		this.mpegPlayer.renderFrameStop();
	}

	@Override
	protected void onDraw(Canvas canvas) {
		canvas.drawRGB(0, 0, 0);

		RenderedFrame renderFrame;
		try {
			renderFrame = this.mpegPlayer.renderFrame();
			canvas.save();
			int width = this.getWidth();
			int height = this.getHeight();
			float ratiow = width / (float) renderFrame.width;
			float ratioh = height / (float) renderFrame.height;
			float ratio = ratiow > ratioh ? ratioh : ratiow;
			float moveX = ((renderFrame.width * ratio - width) / 2.0f);
			float moveY = ((renderFrame.height * ratio - height) / 2.0f);
			canvas.translate(-moveX, -moveY);
			canvas.scale(ratio, ratio);

			canvas.drawBitmap(renderFrame.bitmap, 0, 0, null);
			this.mpegPlayer.releaseFrame();
			canvas.restore();
		} catch (InterruptedException e) {
		}
	

		String fps = this.fpsCounter.tick();
		canvas.drawText(fps, 40, 40, this.mPaint);
		// force a redraw, with a different time-based pattern.
		this.invalidate();
	}

}
