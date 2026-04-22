package com.aauto.app;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;

import java.util.HashMap;
import java.util.Map;

/**
 * PCM audio player using Android AudioTrack.
 * Manages one AudioTrack per stream type (media, guidance, system).
 */
public class AudioPlayer {
    private static final String TAG = "AA.AudioPlayer";

    // Stream type constants matching IAudioSink::AudioStreamType
    private static final int STREAM_MEDIA    = 1;
    private static final int STREAM_GUIDANCE = 2;
    private static final int STREAM_SYSTEM   = 3;

    private final Map<Integer, AudioTrack> tracks = new HashMap<>();

    public void feedData(int streamType, byte[] data) {
        AudioTrack track = tracks.get(streamType);
        if (track == null) {
            track = createTrack(streamType);
            if (track == null) return;
            tracks.put(streamType, track);
            track.play();
            Log.i(TAG, "stream " + streamType + " started");
        }
        track.write(data, 0, data.length);
    }

    public void release() {
        for (AudioTrack track : tracks.values()) {
            track.stop();
            track.release();
        }
        tracks.clear();
        Log.i(TAG, "all streams released");
    }

    private AudioTrack createTrack(int streamType) {
        int sampleRate;
        int channelCount;
        int usage;

        switch (streamType) {
            case STREAM_MEDIA:
                sampleRate = 48000;
                channelCount = 2;
                usage = AudioAttributes.USAGE_MEDIA;
                break;
            case STREAM_GUIDANCE:
                sampleRate = 16000;
                channelCount = 1;
                usage = AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE;
                break;
            case STREAM_SYSTEM:
                sampleRate = 16000;
                channelCount = 1;
                usage = AudioAttributes.USAGE_NOTIFICATION;
                break;
            default:
                Log.w(TAG, "unknown stream type: " + streamType);
                return null;
        }

        int channelMask = channelCount == 2
                ? AudioFormat.CHANNEL_OUT_STEREO
                : AudioFormat.CHANNEL_OUT_MONO;
        int encoding = AudioFormat.ENCODING_PCM_16BIT;

        int bufSize = AudioTrack.getMinBufferSize(sampleRate, channelMask, encoding);
        bufSize = Math.max(bufSize, sampleRate * channelCount * 2 / 10); // at least 100ms

        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(usage)
                .setContentType(AudioAttributes.CONTENT_TYPE_UNKNOWN)
                .build();

        AudioFormat format = new AudioFormat.Builder()
                .setSampleRate(sampleRate)
                .setChannelMask(channelMask)
                .setEncoding(encoding)
                .build();

        try {
            return new AudioTrack(attrs, format, bufSize,
                    AudioTrack.MODE_STREAM, AudioManager.AUDIO_SESSION_ID_GENERATE);
        } catch (Exception e) {
            Log.e(TAG, "failed to create AudioTrack for stream " + streamType, e);
            return null;
        }
    }
}
