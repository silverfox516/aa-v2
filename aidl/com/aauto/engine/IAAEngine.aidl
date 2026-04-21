package com.aauto.engine;

import com.aauto.engine.IAAEngineCallback;
import android.os.ParcelFileDescriptor;

/**
 * Binder interface: App -> aa-engine daemon.
 */
interface IAAEngine {
    int startSession(in ParcelFileDescriptor usbFd, int epIn, int epOut);

    /** Send touch event to phone. action: 0=DOWN, 1=UP, 2=MOVE */
    oneway void sendTouchEvent(int sessionId, int x, int y, int action);

    void stopSession(int sessionId);
    void stopAll();
    void registerCallback(IAAEngineCallback callback);
}
