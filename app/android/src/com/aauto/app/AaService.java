package com.aauto.app;

import android.app.Service;
import android.content.Intent;
import android.hardware.usb.UsbDevice;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;

import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

/**
 * Background service bridging the Android app with aa-engine daemon.
 */
public class AaService extends Service implements UsbMonitor.Listener {
    private static final String TAG = "AA.Service";
    private static final int ENGINE_CONNECT_RETRY_MS = 2000;
    private static final int ENGINE_CONNECT_MAX_RETRIES = 10;

    private UsbMonitor usbMonitor;
    private IAAEngine engineProxy;
    private int currentSessionId = -1;
    private int connectRetryCount = 0;
    private Surface pendingSurface;
    private final Handler handler = new Handler(Looper.getMainLooper());

    public class LocalBinder extends Binder {
        public AaService getService() {
            return AaService.this;
        }
    }

    private final LocalBinder binder = new LocalBinder();

    private VideoDecoder videoDecoder;

    private final IAAEngineCallback.Stub engineCallback =
            new IAAEngineCallback.Stub() {
        @Override
        public void onSessionStateChanged(int sessionId, int status) {
            Log.i(TAG, "session " + sessionId + " state: " + status);
        }

        @Override
        public void onSessionError(int sessionId, int errorCode, String message) {
            Log.e(TAG, "session " + sessionId + " error " + errorCode +
                    ": " + message);
        }

        @Override
        public void onVideoData(int sessionId, byte[] data, long timestampUs,
                                boolean isConfig) {
            if (videoDecoder != null) {
                videoDecoder.feedData(data, timestampUs, isConfig);
            }
        }

        @Override
        public void onAudioData(int sessionId, int streamType, byte[] data,
                                long timestampUs) {
            // TODO: audio playback via AudioTrack
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "AaService created");
        connectToEngine();
        usbMonitor = new UsbMonitor(this, this);
        usbMonitor.start();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "AaService destroying");
        handler.removeCallbacksAndMessages(null);
        usbMonitor.stop();
        if (engineProxy != null) {
            try { engineProxy.stopAll(); }
            catch (RemoteException e) { Log.w(TAG, "stopAll failed", e); }
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    // ===== UsbMonitor.Listener =====

    @Override
    public void onDeviceReady(int fd, int epIn, int epOut) {
        if (engineProxy == null) {
            Log.e(TAG, "engine not connected, cannot start session");
            return;
        }

        try {
            // Wrap raw fd in ParcelFileDescriptor for binder transfer
            ParcelFileDescriptor pfd = ParcelFileDescriptor.adoptFd(fd);
            currentSessionId = engineProxy.startSession(pfd, epIn, epOut);
            Log.i(TAG, "session started: id=" + currentSessionId);

            // If Surface was set before session started, send it now
            if (pendingSurface != null && currentSessionId > 0) {
                setSurface(pendingSurface);
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start session", e);
        }
    }

    @Override
    public void onDeviceDisconnected() {
        if (engineProxy != null && currentSessionId > 0) {
            try { engineProxy.stopSession(currentSessionId); }
            catch (RemoteException e) { Log.w(TAG, "stopSession failed", e); }
        }
        currentSessionId = -1;
    }

    // ===== Public API for Activity =====

    public void onNewUsbDevice(UsbDevice device) {
        if (usbMonitor != null) {
            usbMonitor.onNewUsbDevice(device);
        }
    }

    public void sendTouchEvent(int x, int y, int action) {
        if (engineProxy == null || currentSessionId <= 0) return;
        try {
            engineProxy.sendTouchEvent(currentSessionId, x, y, action);
        } catch (RemoteException e) {
            Log.w(TAG, "sendTouchEvent failed", e);
        }
    }

    public void setSurface(Surface surface) {
        pendingSurface = surface;

        if (surface != null) {
            if (videoDecoder == null) {
                videoDecoder = new VideoDecoder();
            }
            videoDecoder.setSurface(surface);
            Log.i(TAG, "setSurface: decoder ready");
        } else {
            if (videoDecoder != null) {
                videoDecoder.release();
                videoDecoder = null;
            }
            Log.i(TAG, "setSurface: decoder released");
        }
    }

    // ===== Engine connection with retry =====

    private void connectToEngine() {
        IBinder binder = android.os.ServiceManager.getService("aa-engine");
        if (binder == null) {
            connectRetryCount++;
            if (connectRetryCount <= ENGINE_CONNECT_MAX_RETRIES) {
                Log.w(TAG, "aa-engine not found, retry " + connectRetryCount +
                        "/" + ENGINE_CONNECT_MAX_RETRIES);
                handler.postDelayed(this::connectToEngine, ENGINE_CONNECT_RETRY_MS);
            } else {
                Log.e(TAG, "aa-engine not found after max retries");
            }
            return;
        }

        engineProxy = IAAEngine.Stub.asInterface(binder);
        try {
            engineProxy.registerCallback(engineCallback);
            connectRetryCount = 0;
            Log.i(TAG, "connected to aa-engine");
        } catch (RemoteException e) {
            Log.e(TAG, "registerCallback failed", e);
            engineProxy = null;
        }
    }
}
