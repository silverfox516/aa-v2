package com.aauto.app;

import android.content.Context;
import android.content.Intent;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import com.aauto.engine.IAAEngine;

class SessionLifecycleController {
    private static final String TAG = "AA.SessionLifecycle";

    interface Callback {
        void onSessionStateChanged();
    }

    private final Context context;
    private final WirelessStateTracker wirelessStateTracker;
    private final PlaybackController playbackController;
    private final Callback callback;

    private int currentSessionId = -1;
    private boolean isWirelessSession;
    private volatile boolean videoFocusActive;

    SessionLifecycleController(Context context,
                               WirelessStateTracker wirelessStateTracker,
                               PlaybackController playbackController,
                               Callback callback) {
        this.context = context;
        this.wirelessStateTracker = wirelessStateTracker;
        this.playbackController = playbackController;
        this.callback = callback;
    }

    boolean hasActiveSession() {
        return currentSessionId > 0;
    }

    int getCurrentSessionId() {
        return currentSessionId;
    }

    boolean isWirelessSession() {
        return isWirelessSession;
    }

    boolean isVideoFocusActive() {
        return videoFocusActive;
    }

    void setVideoFocusActive(boolean active) {
        this.videoFocusActive = active;
    }

    void startUsbSession(IAAEngine engineProxy, int fd, int epIn, int epOut) {
        if (engineProxy == null) {
            Log.e(TAG, "engine not connected, cannot start USB session");
            return;
        }
        try {
            ParcelFileDescriptor pfd = ParcelFileDescriptor.adoptFd(fd);
            currentSessionId = engineProxy.startSession(pfd, epIn, epOut);
            isWirelessSession = false;
            Log.i(TAG, "USB session started: id=" + currentSessionId);
            if (currentSessionId > 0) {
                callback.onSessionStateChanged();
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start USB session", e);
        }
    }

    void startWirelessSession(IAAEngine engineProxy, int tcpPort) {
        if (engineProxy == null) {
            Log.e(TAG, "engine not connected, cannot start wireless session");
            return;
        }
        try {
            currentSessionId = engineProxy.startTcpSession(tcpPort);
            isWirelessSession = true;
            Log.i(TAG, "wireless session started: id=" + currentSessionId);
            if (currentSessionId > 0) {
                callback.onSessionStateChanged();
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start wireless session", e);
        }
    }

    void stopActiveSession(IAAEngine engineProxy) {
        if (currentSessionId <= 0 || engineProxy == null) {
            return;
        }
        try {
            engineProxy.stopSession(currentSessionId);
        } catch (RemoteException e) {
            Log.w(TAG, "stopSession failed", e);
        }
    }

    void cleanupSession() {
        if (currentSessionId <= 0) return;
        currentSessionId = -1;
        if (isWirelessSession) {
            wirelessStateTracker.clear();
        }
        isWirelessSession = false;
        playbackController.clearSessionPlayback();
        context.sendBroadcast(new Intent(AaService.ACTION_SESSION_ENDED));
        callback.onSessionStateChanged();
        Log.i(TAG, "session cleaned up");
    }
}
