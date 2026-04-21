package com.aauto.app;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import java.nio.ByteBuffer;

/**
 * H.264 video decoder using Android MediaCodec.
 * Receives compressed NALUs from aa-engine daemon via AIDL callback,
 * decodes and renders to the provided Surface.
 */
public class VideoDecoder {
    private static final String TAG = "AA.VideoDecoder";
    private static final String MIME_H264 = "video/avc";
    private static final int DEFAULT_WIDTH = 800;
    private static final int DEFAULT_HEIGHT = 480;
    private static final long DEQUEUE_TIMEOUT_US = 5000;

    private MediaCodec codec;
    private Surface surface;
    private boolean configured;
    private final MediaCodec.BufferInfo bufferInfo = new MediaCodec.BufferInfo();
    private int inputCount;
    private int outputCount;

    public void setSurface(Surface surface) {
        this.surface = surface;
        Log.i(TAG, "surface set");
    }

    public void feedData(byte[] data, long timestampUs, boolean isConfig) {
        if (surface == null) return;

        if (isConfig) {
            configureCodec(data);
            return;
        }

        if (codec == null || !configured) return;
        submitInputBuffer(data, timestampUs, 0);
        drainOutputBuffers();
        inputCount++;
        if (inputCount % 30 == 0) {
            Log.d(TAG, "frames: in=" + inputCount + " out=" + outputCount
                    + " size=" + data.length);
        }
    }

    public void release() {
        if (codec != null) {
            try {
                codec.stop();
                codec.release();
            } catch (Exception e) {
                Log.w(TAG, "release error", e);
            }
            codec = null;
            configured = false;
        }
        Log.i(TAG, "decoder released");
    }

    private void configureCodec(byte[] csd) {
        if (codec != null) {
            release();
        }

        try {
            MediaFormat format = MediaFormat.createVideoFormat(
                    MIME_H264, DEFAULT_WIDTH, DEFAULT_HEIGHT);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(csd));

            codec = MediaCodec.createDecoderByType(MIME_H264);
            codec.configure(format, surface, null, 0);
            codec.start();
            configured = true;
            Log.i(TAG, "decoder configured: " + DEFAULT_WIDTH + "x" + DEFAULT_HEIGHT);
        } catch (Exception e) {
            Log.e(TAG, "failed to configure decoder", e);
            codec = null;
            configured = false;
        }
    }

    private void submitInputBuffer(byte[] data, long timestampUs, int flags) {
        int idx = codec.dequeueInputBuffer(DEQUEUE_TIMEOUT_US);
        if (idx < 0) return;

        ByteBuffer buf = codec.getInputBuffer(idx);
        if (buf == null) return;

        buf.clear();
        buf.put(data);
        codec.queueInputBuffer(idx, 0, data.length, timestampUs, flags);
    }

    private void drainOutputBuffers() {
        while (true) {
            int idx = codec.dequeueOutputBuffer(bufferInfo, 0);
            if (idx >= 0) {
                // Use explicit render timestamp to avoid mul-overflow in
                // TCC803x libstagefright (ubsan: mul-overflow on timestamp*1000).
                codec.releaseOutputBuffer(idx, System.nanoTime());
                outputCount++;
            } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                MediaFormat fmt = codec.getOutputFormat();
                Log.i(TAG, "output format: " + fmt);
            } else {
                break;
            }
        }
    }
}
