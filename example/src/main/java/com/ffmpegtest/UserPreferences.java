package com.ffmpegtest;

import android.content.Context;
import android.content.SharedPreferences;
import android.support.annotation.Nullable;

import javax.annotation.Nonnull;

public class UserPreferences {

    public static final String USER_PREFERENCES = "USER_PREFERENCES";
    private static final String KEY_URL = "url";
    private final SharedPreferences preferences;

    public UserPreferences(@Nonnull Context context) {
        preferences = context.getSharedPreferences(USER_PREFERENCES, 0);
    }

    public void setUrl(@Nullable String url) {
        preferences.edit().putString(KEY_URL, url).apply();
    }

    @Nullable
    public String getUrl() {
        return preferences.getString(KEY_URL, "rtsp://ip.inter.appunite.net:554");
    }
}
