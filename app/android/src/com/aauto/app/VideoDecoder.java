package com.aauto.app;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import java.nio.ByteBuffer;

/**
 * H.264 video decoder using Android MediaCodec.
 * Receives compressed NALUs from aa-engine daemon via AIDL callback,
 * decodes and renders to the provided Surface.
 *
 * Input (feedData) and output (drain) run on separate threads:
 * - feedData: called from Binder thread, submits to decoder input queue
 * - outputThread: continuously drains decoded frames to Surface
 */
public class VideoDecoder {
    private static final String TAG = "AA.VideoDecoder";
    private static final String MIME_H264 = "video/avc";
    private static final long INPUT_TIMEOUT_US = 5000;   // 5ms
    private static final long OUTPUT_TIMEOUT_US = 10000;  // 10ms

    private int videoWidth = 800;
    private int videoHeight = 480;
    private MediaCodec codec;
    private Surface surface;
    private volatile boolean configured;
    private volatile boolean running;
    private Thread outputThread;
    private int inputCount;
    private int outputCount;

    public void setSurface(Surface surface) {
        this.surface = surface;
        Log.i(TAG, "surface set");
    }

    public void setVideoSize(int width, int height) {
        this.videoWidth = width;
        this.videoHeight = height;
    }

    public void feedData(byte[] data, long timestampUs, boolean isConfig) {
        if (surface == null) return;

        if (isConfig) {
            configureCodec(data);
            return;
        }

        if (codec == null || !configured) return;

        try {
            int idx = codec.dequeueInputBuffer(INPUT_TIMEOUT_US);
            if (idx < 0) return;

            ByteBuffer buf = codec.getInputBuffer(idx);
            if (buf == null) return;

            buf.clear();
            buf.put(data);
            codec.queueInputBuffer(idx, 0, data.length, timestampUs, 0);

            inputCount++;
            if (inputCount % 60 == 0) {
                Log.i(TAG, "frames: in=" + inputCount + " out=" + outputCount
                        + " drop=" + (inputCount - outputCount));
            }
        } catch (Exception e) {
            Log.w(TAG, "feedData error", e);
        }
    }

    public void release() {
        running = false;
        if (outputThread != null) {
            try { outputThread.join(1000); }
            catch (InterruptedException ignored) {}
            outputThread = null;
        }
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
        release();

        try {
            MediaFormat format = MediaFormat.createVideoFormat(
                    MIME_H264, videoWidth, videoHeight);
            format.setByteBuffer("csd-0", ByteBuffer.wrap(csd));

            codec = MediaCodec.createDecoderByType(MIME_H264);
            codec.configure(format, surface, null, 0);
            codec.start();
            configured = true;

            // Start output drain thread
            running = true;
            outputThread = new Thread(this::outputLoop, "VideoDecoder-output");
            outputThread.start();

            Log.i(TAG, "decoder configured: " + videoWidth + "x" + videoHeight);
        } catch (Exception e) {
            Log.e(TAG, "failed to configure decoder", e);
            codec = null;
            configured = false;
        }
    }

    private void outputLoop() {
        Log.d(TAG, "output thread started");
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();

        while (running && codec != null) {
            try {
                int idx = codec.dequeueOutputBuffer(info, OUTPUT_TIMEOUT_US);
                if (idx >= 0) {
                    codec.releaseOutputBuffer(idx, System.nanoTime());
                    outputCount++;
                } else if (idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    Log.i(TAG, "output format: " + codec.getOutputFormat());
                }
            } catch (Exception e) {
                if (running) {
                    Log.w(TAG, "output drain error", e);
                }
                break;
            }
        }
        Log.d(TAG, "output thread exited");
    }
}
