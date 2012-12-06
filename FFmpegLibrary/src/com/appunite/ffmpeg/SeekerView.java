/*
 * SeekerView.java
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
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import com.appunite.ffmpeg.R;

public class SeekerView extends View {
	
	public static interface OnProgressChangeListener {
		void onProgressChange(boolean finished, int currentValue, int maxValue);
	}

	private int mBorderWidth;
	private int mBorderColor;
	private int mBorderPadding;
	
	private int mBarMinHeight;
	private int mBarMinWidth;
	private int mBarColor;

	private Paint mBorderPaint = new Paint();
	private Paint mBarPaint = new Paint();
	
	private Rect mBorderRect = new Rect();
	private Rect mBarRect = new Rect();
	private OnProgressChangeListener mOnProgressChangeListener = null;
	
	private int mMaxValue = 100;
	private int mCurrentValue = 10;
	
	public void setOnProgressChangeListener(OnProgressChangeListener onProgressChangeListener) {
		this.mOnProgressChangeListener = onProgressChangeListener;
	}
	
	public SeekerView(Context context, AttributeSet attrs, int defStyle) {
		super(context, attrs, defStyle);
		TypedArray a = this.getContext().obtainStyledAttributes(attrs,
				R.styleable.SeekerView, defStyle, 0);

		final float scale = getResources().getDisplayMetrics().density;

		mBorderWidth = a.getDimensionPixelSize(
				R.styleable.SeekerView_borderWidth, (int) (1 * scale + 0.5f));
		mBorderColor = a.getColor(R.styleable.SeekerView_barColor, Color.CYAN);
		mBorderPadding = a.getColor(R.styleable.SeekerView_borderPadding,
				(int) (1 * scale + 0.5f));
		
		mBarMinHeight = a.getDimensionPixelSize(
				R.styleable.SeekerView_barMinHeight, (int) (10 * scale + 0.5f));
		mBarMinWidth = a.getDimensionPixelSize(
				R.styleable.SeekerView_barMinWidth, (int) (50 * scale + 0.5f));
		mBarColor = a.getColor(R.styleable.SeekerView_barColor, Color.BLUE);

		mBorderPaint.setDither(true);
		mBorderPaint.setColor(mBorderColor);
		mBorderPaint.setStyle(Paint.Style.STROKE);
		mBorderPaint.setStrokeJoin(Paint.Join.ROUND);
		mBorderPaint.setStrokeCap(Paint.Cap.ROUND);
		mBorderPaint.setStrokeWidth(mBorderWidth);
		
		mBarPaint.setDither(true);
		mBarPaint.setColor(mBarColor);
		mBarPaint.setStyle(Paint.Style.FILL);
		mBarPaint.setStrokeJoin(Paint.Join.ROUND);
		mBarPaint.setStrokeCap(Paint.Cap.ROUND);
		
		mMaxValue = a.getInt(R.styleable.SeekerView_maxValue, mMaxValue);
		mCurrentValue = a.getInt(R.styleable.SeekerView_currentValue, mCurrentValue);
	}

	public SeekerView(Context context, AttributeSet attrs) {
		this(context, attrs, 0);
	}

	public SeekerView(Context context) {
		this(context, null);
	}
	
	public void setMaxValue(int maxValue) {
		this.mMaxValue = maxValue;
		this.invalidate();
	}
	
	public int maxValue() {
		return mMaxValue;
	}
	
	public void setCurrentValue(int currentValue) {
		this.mCurrentValue = currentValue;
		this.invalidate();
	}
	
	public int currentValue() {
		return this.mCurrentValue;
	}

	@Override
	protected void onDraw(Canvas canvas) {
		super.onDraw(canvas);
		canvas.drawRect(mBorderRect, mBorderPaint);
		canvas.drawRect(mBarRect, mBarPaint);
	}
	
	@Override
	public boolean onTouchEvent(MotionEvent event) {
		int action = event.getActionMasked();
		
		boolean superResult = super.onTouchEvent(event);
		boolean grab = false;
		boolean finished = false;
		
		if (action == MotionEvent.ACTION_DOWN) {
			grab = true;
		} else if (action == MotionEvent.ACTION_MOVE) {
			grab = true;
		} else if (action == MotionEvent.ACTION_UP) {
			grab = true;
			finished = true;
		}
		if (grab) {
			
			float eventX = event.getX();
			int padding = mBorderWidth + mBorderPadding;
			int barLeft = padding;
			int barWidth = getWidth() - 2*padding;
			float x = eventX - barLeft;
			if (x < 0.0f)
				x = 0.0f;
			if (x > barWidth)
				x = barWidth;
			x /= (float)barWidth;
			mCurrentValue = (int) (mMaxValue * x);
			
			if (mOnProgressChangeListener != null) {
				mOnProgressChangeListener.onProgressChange(finished, mCurrentValue, mMaxValue);
			}
			calculateBarRect();
			this.invalidate();
			return true;
		} else {
			return superResult;
		}
	}

	@Override
	protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
		int dw = 0;
		int dh = 0;

		dw = (mBorderWidth + mBorderPadding) * 2 + mBarMinWidth;
		dh = (mBorderWidth + mBorderPadding) * 2 + mBarMinHeight;
		this.setMeasuredDimension(
				ViewCompat.resolveSizeAndState(dw, widthMeasureSpec, 0),
				ViewCompat.resolveSizeAndState(dh, heightMeasureSpec, 0));
	}
	
	private void calculateBarRect() {
		int width = getWidth();
		int height = getHeight();
		int barPadding = mBorderWidth + mBorderPadding;
		
		int maxBarWidth = width - barPadding;
		float pos = (float) mCurrentValue / mMaxValue;
		int barWidth = (int) (maxBarWidth * pos);
		mBarRect.set(
				barPadding,
				barPadding,
				barWidth,
				height - barPadding);
	}

	@Override
	protected void onLayout(boolean changed, int left, int top, int right,
			int bottom) {
		super.onLayout(changed, left, top, right, bottom);
		
		if (changed) {
			int width = right-left;
			int height = bottom-top;
			mBorderRect.set(0, 0, width, height);
			calculateBarRect();
		}
	}

}
