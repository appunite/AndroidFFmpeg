package com.appunite.ffmpeg;

public class FpsCounter {
	private final int frameCount;
	private int counter = 0;
	boolean start = true;

	private long startTime = 0;

	private String tick = "- fps";

	public FpsCounter(int frameCount) {
		this.frameCount = frameCount;
	}

	public String tick() {
		if (this.start) {
			this.start = false;
			this.startTime = System.currentTimeMillis();
		}
		if (this.counter++ < this.frameCount) {
			return this.tick;
		}

		long stopTime = System.currentTimeMillis();
		float fps =
				(float) this.frameCount / (float) (stopTime - this.startTime)
						* 1000.0f;
		this.startTime = stopTime;
		this.counter = 0;

		this.tick = String.format("%.2f fps", fps);
		return this.tick;
	}
}
