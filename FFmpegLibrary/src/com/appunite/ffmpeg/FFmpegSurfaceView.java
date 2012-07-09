/*
 * FFmpegSurfaceView.java
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

import com.appunite.ffmpeg.FFmpegPlayer.RenderedFrame;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class FFmpegSurfaceView extends SurfaceView implements FFmpegDisplay,
		SurfaceHolder.Callback {

	private FFmpegPlayer mMpegPlayer = null;
	private Object mMpegPlayerLock = new Object();
	private TutorialThread mThread;
	private Paint mPaint;
	private FpsCounter fpsCounter;

	public FFmpegSurfaceView(Context context) {
		this(context, null, 0);
	}

	public FFmpegSurfaceView(Context context, AttributeSet attrs) {
		this(context, attrs, 0);
	}

	public FFmpegSurfaceView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);

		getHolder().addCallback(this);
		mThread = new TutorialThread(getHolder());

		this.mPaint = new Paint();
		this.mPaint.setTextSize(32);
		this.mPaint.setColor(Color.RED);
		this.fpsCounter = new FpsCounter(10);
	}

	@Override
	public void setMpegPlayer(FFmpegPlayer fFmpegPlayer) {
		if (mMpegPlayer != null)
			throw new RuntimeException("setMpegPlayer could not be called twice");
		
		synchronized (mMpegPlayerLock) {
			this.mMpegPlayer = fFmpegPlayer;
			mMpegPlayerLock.notifyAll();
		}
	}

	class TutorialThread extends Thread {
		private SurfaceHolder mSurfaceHolder;
		private boolean mRun = false;
		private int mSurfaceWidth;
		private int mSurfaceHeight;

		public TutorialThread(SurfaceHolder surfaceHolder) {
			mSurfaceHolder = surfaceHolder;
		}

		public void setSurfaceParams(int width, int height) {
			mSurfaceHeight = height;
			mSurfaceWidth = width;
		}

		public synchronized void setRunning(boolean run) {
			mRun = run;
		}

		public synchronized boolean isRunning() {
			return mRun;
		}

		@Override
		public void run() {
			while (isRunning()) {

				try {
					synchronized (mMpegPlayerLock) {
						while (mMpegPlayerLock == null)
							mMpegPlayerLock.wait();
						RenderedFrame renderFrame = mMpegPlayer.renderFrame();
						Canvas canvas = mSurfaceHolder.lockCanvas();
						if (canvas == null)
							return;
						canvas.save();
						float ratiow = mSurfaceWidth
								/ (float) renderFrame.width;
						float ratioh = mSurfaceHeight
								/ (float) renderFrame.height;
						float ratio = ratiow > ratioh ? ratioh : ratiow;
						float moveX = ((renderFrame.width * ratio - mSurfaceWidth) / 2.0f);
						float moveY = ((renderFrame.height * ratio - mSurfaceHeight) / 2.0f);
						canvas.translate(-moveX, -moveY);
						canvas.scale(ratio, ratio);

						canvas.drawBitmap(renderFrame.bitmap, 0, 0, null);
						mMpegPlayer.releaseFrame();
						canvas.restore();

						String fps = fpsCounter.tick();
						canvas.drawText(fps, 40 - moveX, 40 - moveY, mPaint);

						mSurfaceHolder.unlockCanvasAndPost(canvas);
					}
				} catch (InterruptedException e) {
				}

			}
		}
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width,
			int height) {
		surfaceDestroyed(holder);
		this.mMpegPlayer.renderFrameStart();
		mThread.setRunning(true);
		mThread.setSurfaceParams(width, height);
		mThread.start();

	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {

	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		mThread.setRunning(false);
		this.mMpegPlayer.renderFrameStop();
		mThread.interrupt();
	}

}
