package com.aauto.app;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import java.nio.ByteBuffer;

/**
 * H.264 video decoder using Android MediaCodec.
 *
 * Single-threaded model (matching reference): pushData handles both
 * input queueing and output draining in one call. No separate output
 * thread, no threading issues, no IDR frame drops during reconfiguration.
 *
 * Called from Binder thread (AIDL callback).
 */
public class VideoDecoder {
    private static final String TAG = "AA.VideoDecoder";
    private static final String MIME_H264 = "video/avc";
    private static final long INPUT_TIMEOUT_US = 10000;  // 10ms

    private int videoWidth = 800;
    private int videoHeight = 480;
    private MediaCodec codec;
    private Surface surface;
    private boolean configured;

    public synchronized void setSurface(Surface surface) {
        this.surface = surface;
        Log.i(TAG, "surface set");
    }

    public synchronized void setVideoSize(int width, int height) {
        this.videoWidth = width;
        this.videoHeight = height;
    }

    /**
     * Open the decoder with the given dimensions.
     * CSD (SPS/PPS) is not set here — it arrives in the stream via feedData.
     * Synchronized against feedData()/release() to keep `codec` and
     * `configured` mutations atomic — open is called from a Binder
     * thread while release runs on main.
     */
    public synchronized boolean open() {
        if (configured) return true;
        if (surface == null || !surface.isValid()) {
            Log.w(TAG, "open(): no valid surface — skipping");
            return false;
        }
        MediaCodec localCodec = null;
        try {
            localCodec = MediaCodec.createDecoderByType(MIME_H264);
            if (localCodec == null) {
                Log.e(TAG, "createDecoderByType returned null");
                return false;
            }
            MediaFormat format = MediaFormat.createVideoFormat(
                    MIME_H264, videoWidth, videoHeight);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            // KEY_LOW_LATENCY (Android 11+; ignored on 10): hints the
            // decoder to skip frame reordering and present ASAP. Harmless
            // on Android 10 — unknown keys are ignored.
            format.setInteger("low-latency", 1);
            localCodec.configure(format, surface, null, 0);
            localCodec.start();
            // Only commit to the field after start() succeeds — feedData
            // checks `codec != null` and assumes it's started.
            codec = localCodec;
            configured = true;
            Log.i(TAG, "decoder opened: " + videoWidth + "x" + videoHeight);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "failed to open decoder", e);
            if (localCodec != null) {
                try { localCodec.release(); } catch (Exception ignored) {}
            }
            codec = null;
            configured = false;
            return false;
        }
    }

    /**
     * Feed compressed H.264 data (including SPS/PPS and frame data).
     * Queues input and immediately drains any available output to Surface.
     */
    public synchronized void feedData(byte[] data) {
        if (!configured || codec == null || data == null) return;

        try {
            int inputIdx = codec.dequeueInputBuffer(INPUT_TIMEOUT_US);
            if (inputIdx < 0) return;

            ByteBuffer inputBuf = codec.getInputBuffer(inputIdx);
            if (inputBuf == null) return;

            inputBuf.clear();
            int len = Math.min(data.length, inputBuf.capacity());
            inputBuf.put(data, 0, len);
            codec.queueInputBuffer(inputIdx, 0, len, 0, 0);

            // Drain all available output frames to Surface.
            // releaseOutputBuffer with explicit nanoTime() avoids the
            // TCC803x libstagefright ubsan abort triggered by the
            // (idx, true) form (troubleshooting #6) and renders ASAP
            // (no vsync wait), which also trims a few ms of latency.
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int outputIdx;
            while ((outputIdx = codec.dequeueOutputBuffer(info, 0)) >= 0) {
                codec.releaseOutputBuffer(outputIdx, System.nanoTime());
            }
        } catch (IllegalStateException e) {
            Log.w(TAG, "codec error", e);
            release();
        } catch (Exception e) {
            Log.w(TAG, "feedData error", e);
        }
    }

    public synchronized void release() {
        configured = false;
        if (codec != null) {
            try {
                codec.stop();
                codec.release();
            } catch (Exception ignored) {}
            codec = null;
        }
        Log.i(TAG, "decoder released");
    }
}
