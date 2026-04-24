package com.aauto.engine;

import com.aauto.engine.IAAEngineCallback;
import android.os.ParcelFileDescriptor;

/**
 * Binder interface: App -> aa-engine daemon.
 */
interface IAAEngine {
    /** Start USB session with file descriptor and endpoints. */
    int startSession(in ParcelFileDescriptor usbFd, int epIn, int epOut);

    /** Start TCP session (wireless AA). Phone connects to host:port. */
    int startTcpSession(int port);

    /** Send touch event to phone. action: 0=DOWN, 1=UP, 2=MOVE */
    oneway void sendTouchEvent(int sessionId, int x, int y, int action);

    /** Set video focus. true=PROJECTED (phone sends video), false=NATIVE (phone stops). */
    void setVideoFocus(int sessionId, boolean projected);

    /** Attach/detach all sinks (video + audio) for session switching. */
    void attachAllSinks(int sessionId);
    void detachAllSinks(int sessionId);

    void stopSession(int sessionId);
    void stopAll();
    void registerCallback(IAAEngineCallback callback);
}
