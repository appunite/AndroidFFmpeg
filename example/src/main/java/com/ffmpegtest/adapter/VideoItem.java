package com.ffmpegtest.adapter;

import android.support.annotation.Nullable;

import javax.annotation.Nonnull;

public class VideoItem {
    private final long id;
    @Nullable
    private final String text;
    @Nonnull
    private String video;
    @Nullable
    private String key;

    public VideoItem(long id,
                     @Nullable String text,
                     @Nonnull String video,
                     @Nullable String key) {
        this.id = id;
        this.text = text;
        this.video = video;
        this.key = key;
    }

    public long id() {
        return id;
    }

    @Nullable
    public String text() {
        return text;
    }

    @Nonnull
    public String video() {
        return video;
    }

    @Nullable
    public String key() {
        return key;
    }
}
