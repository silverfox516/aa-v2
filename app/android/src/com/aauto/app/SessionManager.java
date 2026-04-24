package com.aauto.app;

import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Manages AA sessions by session_id. Transport-agnostic — sessions are
 * identified by engine-assigned session_id, not by transport type.
 *
 * Thread safety: all methods synchronized. Written from Binder thread
 * (engine callbacks) and main thread (UI), read from main thread.
 */
class SessionManager {
    private static final String TAG = "AA.SessionManager";

    enum SessionState { CONNECTING, CONNECTED, BACKGROUND, ACTIVE }

    static class SessionEntry {
        final int sessionId;
        final String transportLabel;  // display only: "USB" or "Wireless"
        String deviceName;            // from ServiceDiscovery
        SessionState state;

        SessionEntry(int sessionId, String transportLabel) {
            this.sessionId = sessionId;
            this.transportLabel = transportLabel;
            this.state = SessionState.CONNECTING;
        }
    }

    private final Map<Integer, SessionEntry> sessions = new LinkedHashMap<>();

    synchronized SessionEntry createSession(int sessionId,
                                            String transportLabel) {
        SessionEntry entry = new SessionEntry(sessionId, transportLabel);
        sessions.put(sessionId, entry);
        Log.i(TAG, "session created: id=" + sessionId
                + " transport=" + transportLabel);
        return entry;
    }

    synchronized void onPhoneIdentified(int sessionId, String deviceName) {
        SessionEntry entry = sessions.get(sessionId);
        if (entry == null) return;
        entry.deviceName = deviceName;
        entry.state = SessionState.CONNECTED;
        Log.i(TAG, "session connected: id=" + sessionId
                + " device=" + deviceName);
    }

    synchronized void activate(int sessionId) {
        SessionEntry entry = sessions.get(sessionId);
        if (entry == null) return;
        entry.state = SessionState.ACTIVE;
        Log.i(TAG, "session active: id=" + sessionId);
    }

    /** Full deactivation — all sinks detached, back to CONNECTED. */
    synchronized void deactivate(int sessionId) {
        SessionEntry entry = sessions.get(sessionId);
        if (entry == null) return;
        entry.state = SessionState.CONNECTED;
        Log.i(TAG, "session deactivated: id=" + sessionId);
    }

    synchronized void onVideoFocusChanged(int sessionId, boolean projected) {
        SessionEntry entry = sessions.get(sessionId);
        if (entry == null) return;
        entry.state = projected ? SessionState.ACTIVE : SessionState.BACKGROUND;
        Log.i(TAG, "session focus: id=" + sessionId
                + " state=" + entry.state);
    }

    synchronized SessionEntry removeSession(int sessionId) {
        SessionEntry entry = sessions.remove(sessionId);
        if (entry != null) {
            Log.i(TAG, "session removed: id=" + sessionId);
        }
        return entry;
    }

    synchronized SessionEntry getSession(int sessionId) {
        return sessions.get(sessionId);
    }

    synchronized List<SessionEntry> getAll() {
        return new ArrayList<>(sessions.values());
    }

    synchronized boolean hasAnySession() {
        return !sessions.isEmpty();
    }
}
