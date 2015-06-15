/*
 * FpsCounter.java
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
			this.startTime = System.nanoTime();
		}
		if (this.counter++ < this.frameCount) {
			return this.tick;
		}

		long stopTime = System.nanoTime();
		double fps = (double) this.frameCount * (1000.0 * 1000.0 * 1000.0)
				/ (double) (stopTime - this.startTime);
		this.startTime = stopTime;
		this.counter = 0;

		this.tick = String.format("%.2f fps", fps);
		return this.tick;
	}
}
