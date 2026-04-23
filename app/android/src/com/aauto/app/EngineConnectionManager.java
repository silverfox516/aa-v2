package com.aauto.app;

import android.os.Handler;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

class EngineConnectionManager {
    private static final String TAG = "AA.EngineConn";

    interface Callback {
        void onEngineConnected(IAAEngine engine);
        void onEngineDisconnected();
    }

    private final Handler handler;
    private final IAAEngineCallback engineCallback;
    private final Callback callback;
    private final int retryDelayMs;
    private final int maxRetries;
    private final Runnable connectRunnable = this::connect;

    private int connectRetryCount = 0;
    private IAAEngine engineProxy;

    EngineConnectionManager(Handler handler,
                            IAAEngineCallback engineCallback,
                            Callback callback,
                            int retryDelayMs,
                            int maxRetries) {
        this.handler = handler;
        this.engineCallback = engineCallback;
        this.callback = callback;
        this.retryDelayMs = retryDelayMs;
        this.maxRetries = maxRetries;
    }

    void connect() {
        IBinder binder = android.os.ServiceManager.getService("aa-engine");
        if (binder == null) {
            connectRetryCount++;
            if (connectRetryCount <= maxRetries) {
                Log.w(TAG, "aa-engine not found, retry " + connectRetryCount +
                        "/" + maxRetries);
                handler.postDelayed(connectRunnable, retryDelayMs);
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
            callback.onEngineConnected(engineProxy);
        } catch (RemoteException e) {
            Log.e(TAG, "registerCallback failed", e);
            engineProxy = null;
            callback.onEngineDisconnected();
        }
    }

    void shutdown() {
        handler.removeCallbacks(connectRunnable);
        if (engineProxy != null) {
            try {
                engineProxy.stopAll();
            } catch (RemoteException e) {
                Log.w(TAG, "stopAll failed", e);
            }
        }
        engineProxy = null;
        callback.onEngineDisconnected();
    }
}
