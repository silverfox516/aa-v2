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

    public void setSurface(Surface surface) {
        this.surface = surface;
        Log.i(TAG, "surface set");
    }

    public void setVideoSize(int width, int height) {
        this.videoWidth = width;
        this.videoHeight = height;
    }

    /**
     * Open the decoder with the given dimensions.
     * CSD (SPS/PPS) is not set here — it arrives in the stream via feedData.
     */
    public boolean open() {
        if (configured) return true;
        if (surface == null) {
            Log.e(TAG, "no surface set");
            return false;
        }
        try {
            codec = MediaCodec.createDecoderByType(MIME_H264);
            MediaFormat format = MediaFormat.createVideoFormat(
                    MIME_H264, videoWidth, videoHeight);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
            codec.configure(format, surface, null, 0);
            codec.start();
            configured = true;
            Log.i(TAG, "decoder opened: " + videoWidth + "x" + videoHeight);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "failed to open decoder", e);
            codec = null;
            return false;
        }
    }

    /**
     * Feed compressed H.264 data (including SPS/PPS and frame data).
     * Queues input and immediately drains any available output to Surface.
     */
    public void feedData(byte[] data) {
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

            // Drain all available output frames to Surface
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int outputIdx;
            while ((outputIdx = codec.dequeueOutputBuffer(info, 0)) >= 0) {
                codec.releaseOutputBuffer(outputIdx, true);
            }
        } catch (IllegalStateException e) {
            Log.w(TAG, "codec error", e);
            release();
        } catch (Exception e) {
            Log.w(TAG, "feedData error", e);
        }
    }

    public void release() {
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
