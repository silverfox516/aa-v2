package com.aauto.app;

import android.os.Handler;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

import java.util.function.Consumer;

class EngineConnectionManager {
    private static final String TAG = "AA.EngineConn";

    private final Handler handler;
    private final IAAEngineCallback engineCallback;
    private final Consumer<IAAEngine> onConnected;
    private final Runnable onDisconnected;
    private final int retryDelayMs;
    private final int maxRetries;
    private final Runnable connectRunnable = this::connect;

    private int connectRetryCount = 0;
    private IAAEngine engineProxy;

    EngineConnectionManager(Handler handler,
                            IAAEngineCallback engineCallback,
                            Consumer<IAAEngine> onConnected,
                            Runnable onDisconnected,
                            int retryDelayMs,
                            int maxRetries) {
        this.handler = handler;
        this.engineCallback = engineCallback;
        this.onConnected = onConnected;
        this.onDisconnected = onDisconnected;
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
            onConnected.accept(engineProxy);
        } catch (RemoteException e) {
            Log.e(TAG, "registerCallback failed", e);
            engineProxy = null;
            onDisconnected.run();
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
        onDisconnected.run();
    }
}
