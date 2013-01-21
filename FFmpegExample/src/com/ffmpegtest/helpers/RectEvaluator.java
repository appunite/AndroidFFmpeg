package com.ffmpegtest.helpers;

import android.animation.TypeEvaluator;
import android.graphics.RectF;

public class RectEvaluator implements TypeEvaluator<RectF>  {
	
	private RectF mRectF;

	public RectEvaluator() {
		mRectF = new RectF();
	}
	
	private float evaluate(float fraction, float startValue, float endValue) {
		return startValue + fraction * (endValue - startValue);
	}

	@Override
	public RectF evaluate(float fraction, RectF startValue, RectF endValue) {
		float left = evaluate(fraction, startValue.left, endValue.left);
		float top = evaluate(fraction, startValue.top, endValue.top);
		float right = evaluate(fraction, startValue.right, endValue.right);
		float bottom = evaluate(fraction, startValue.bottom, endValue.bottom);
		mRectF.set(left, top, right, bottom);
		return mRectF;
	}
	
}