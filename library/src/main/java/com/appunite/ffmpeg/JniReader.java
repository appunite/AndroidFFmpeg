/*
 * JniReader.java
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

import java.io.UnsupportedEncodingException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

import android.util.Log;

public class JniReader {
	
	private static final String TAG = JniReader.class.getCanonicalName();
	
	private byte[] value = new byte[16];
	private int position;

	public JniReader(String url, int flags) {
		Log.d(TAG, String.format("Reading: %s", url));
		try {
			byte[] key = "dupadupadupadupa".getBytes("UTF-8");
			
			MessageDigest m = MessageDigest.getInstance("MD5");
			m.update(key);
			System.arraycopy(m.digest(), 0, value, 0, 16);

		} catch (UnsupportedEncodingException e) {
			throw new RuntimeException(e);
		} catch (NoSuchAlgorithmException e) {
			throw new RuntimeException(e);
		}
		position = 0;
	}
	
	public int read(byte[] buffer) {
		int end = position + buffer.length;
		if (end >= value.length)
			end = value.length;

		int length = end - position;
		System.arraycopy(value, position, buffer, 0, length);
		position += length;
		
		return length;
	}
	
	public int write(byte[] buffer) {
		return 0;
	}
	
	public int check(int mask) {
		return 0;
	}
	
	public long seek(long pos, int whence) {
		return -1;
	}
}
