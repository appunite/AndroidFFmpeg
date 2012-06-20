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
	protected void onDraw(Canvas canvas) {
		canvas.drawRGB(0, 0, 0);

		RenderedFrame renderFrame = this.mpegPlayer.renderFrame();
		if (renderFrame != null) {
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
		}

		String fps = this.fpsCounter.tick();
		canvas.drawText(fps, 40, 40, this.mPaint);
		// force a redraw, with a different time-based pattern.
		this.invalidate();
	}

}
