/*
 * FFmpegListener.java
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

public interface FFmpegListener {
	void onFFDataSourceLoaded(FFmpegError err, FFmpegStreamInfo[] streams);

	void onFFResume(NotPlayingException result);

	void onFFPause(NotPlayingException err);

	void onFFStop();

	void onFFUpdateTime(long mCurrentTimeUs, long mVideoDurationUs, boolean isFinished);

	void onFFSeeked(NotPlayingException result);

}
