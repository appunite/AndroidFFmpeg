package com.appunite.ffmpeg;

import com.appunite.ffmpeg.FFmpegPlayer.RenderedFrame;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class FFmpegSurfaceView extends SurfaceView implements FFmpegDisplay, SurfaceHolder.Callback {
	
	private FFmpegPlayer mpegPlayer;
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
		this.mpegPlayer = fFmpegPlayer;		
	}
	
	class TutorialThread extends Thread {
	    private SurfaceHolder mSurfaceHolder;
	    private boolean mRun = false;
	    private int mSurfaceWidth;
	    private int mSurfaceHeight;
	 
	    public TutorialThread(SurfaceHolder surfaceHolder) {
	        mSurfaceHolder = surfaceHolder;
	    }
	    
	    public void setSurfaceParams( int width, int height) {
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
	    	while(isRunning()) {
	    		RenderedFrame renderFrame = mpegPlayer.renderFrame();
	    		Canvas canvas = mSurfaceHolder.lockCanvas();
	    		if (canvas == null)
	    			return;
	    		canvas.save();
				float ratiow = mSurfaceWidth / (float) renderFrame.width;
				float ratioh = mSurfaceHeight / (float) renderFrame.height;
				float ratio = ratiow > ratioh ? ratioh : ratiow;
				float moveX = ((renderFrame.width * ratio - mSurfaceWidth) / 2.0f);
				float moveY = ((renderFrame.height * ratio - mSurfaceHeight) / 2.0f);
				canvas.translate(-moveX, -moveY);
				canvas.scale(ratio, ratio);

				canvas.drawBitmap(renderFrame.bitmap, 0, 0, null);
				mpegPlayer.releaseFrame();
				canvas.restore();
				
				String fps = fpsCounter.tick();
				canvas.drawText(fps, 40 - moveX, 40 - moveY, mPaint);
				
	    		mSurfaceHolder.unlockCanvasAndPost(canvas);
	    	}
	    }
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width,
			int height) {
		surfaceDestroyed(holder);
		
		mThread.setRunning(true);
		mThread.setSurfaceParams(width, height);
		mThread.start();
		
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		boolean retry = true;
		mThread.setRunning(false);
	    while (retry) {
	        try {
	        	mThread.join();
	            retry = false;
	        } catch (InterruptedException e) {
	            // we will try it again and again...
	        }
	    }
	}
	
	

}
