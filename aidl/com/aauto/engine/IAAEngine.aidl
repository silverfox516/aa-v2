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

    /**
     * Send a media-control key to the phone via the input channel
     * (InputReport.key_event). Use KeyCode constants from
     * aap_protobuf KeyCode.proto: 85=PLAY_PAUSE, 86=STOP,
     * 87=NEXT, 88=PREVIOUS, etc.
     */
    oneway void sendMediaKey(int sessionId, int keycode);

    /**
     * Send AudioFocus(LOSS) to the phone — phone's media app will
     * auto-pause. Used when demoting a BACKGROUND session to
     * CONNECTED so its audio actually stops.
     */
    oneway void releaseAudioFocus(int sessionId);

    /**
     * Send AudioFocus(GAIN) to the phone — counterpart of
     * releaseAudioFocus. Used when re-promoting a previously demoted
     * session so the phone's media app resumes playback automatically
     * (otherwise it stays paused from the prior LOSS).
     */
    oneway void gainAudioFocus(int sessionId);
}
