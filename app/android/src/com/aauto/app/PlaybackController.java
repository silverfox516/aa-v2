package com.aauto.app;

import android.view.Surface;

class PlaybackController {
    private VideoDecoder videoDecoder;
    private final AudioPlayer audioPlayer = new AudioPlayer();
    private Surface pendingSurface;
    private int videoWidth = 800;
    private int videoHeight = 480;

    void onSessionConfig(int width, int height) {
        videoWidth = width;
        videoHeight = height;
        if (videoDecoder != null) {
            videoDecoder.setVideoSize(width, height);
        }
    }

    void onVideoData(byte[] data, long timestampUs, boolean isConfig) {
        if (videoDecoder == null) return;
        // Open decoder on first data if not already open
        if (!videoDecoder.open()) return;
        videoDecoder.feedData(data);
    }

    void onAudioData(int streamType, byte[] data) {
        audioPlayer.feedData(streamType, data);
    }

    void setSurface(Surface surface) {
        pendingSurface = surface;
        if (surface != null) {
            if (videoDecoder == null) {
                videoDecoder = new VideoDecoder();
                videoDecoder.setVideoSize(videoWidth, videoHeight);
            }
            videoDecoder.setSurface(surface);
        } else if (videoDecoder != null) {
            videoDecoder.release();
            videoDecoder = null;
        }
    }

    void clearSessionPlayback() {
        if (videoDecoder != null) {
            videoDecoder.release();
            videoDecoder = null;
        }
        audioPlayer.release();
    }

    void shutdown() {
        clearSessionPlayback();
        pendingSurface = null;
    }

    int getVideoWidth() {
        return videoWidth;
    }

    int getVideoHeight() {
        return videoHeight;
    }
}
