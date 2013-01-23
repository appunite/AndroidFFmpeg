/*
 * FFmpegStreamInfo.java
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

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

public class FFmpegStreamInfo {
	public enum CodecType {
		UNKNOWN, AUDIO, VIDEO, SUBTITLE, ATTACHMENT, NB, DATA;
	}
	
	private static Map<String, Locale> sLocaleMap;
	static {
		String[] languages = Locale.getISOLanguages();
		sLocaleMap = new HashMap<String, Locale>(languages.length);
		for (String language : languages) {
		    Locale locale = new Locale(language);
		    sLocaleMap.put(locale.getISO3Language(), locale);
		}
	}

	private Map<String, String> mMetadata;
	private CodecType mMediaType;
	private int mStreamNumber;

	public void setMetadata(Map<String, String> metadata) {
		this.mMetadata = metadata;
	}

	void setMediaTypeInternal(int mediaTypeInternal) {
		mMediaType = CodecType.values()[mediaTypeInternal];
	}
	
	void setStreamNumber(int streamNumber) {
		this.mStreamNumber = streamNumber;
	}
	
	public int getStreamNumber() {
		return this.mStreamNumber;
	}
	
	/**
	 * Return stream language locale
	 * @return locale or null if not known
	 */
	public Locale getLanguage() {
		if (mMetadata == null)
			return null;
		String iso3Langugae = mMetadata.get("language");
		if (iso3Langugae == null)
			return null;
		return sLocaleMap.get(iso3Langugae);
	}

	public CodecType getMediaType() {
		return mMediaType;
	}

	public Map<String, String> getMetadata() {
		return mMetadata;
	}

	@Override
	public String toString() {
		Locale language = getLanguage();
		String languageName = language == null ? "unknown" : language.getDisplayName();
		return new StringBuilder().
				append("{\n")
				.append("\tmediaType: ")
				.append(mMediaType)
				.append("\n")
				.append("\tlanguage: ")
				.append(languageName)
				.append("\n")
				.append("\tmetadata ")
				.append(mMetadata)
				.append("\n")
				.append("}")
				.toString();
	}

}
