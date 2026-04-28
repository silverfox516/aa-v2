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

    /**
     * Returns the single ACTIVE session (full AA screen) if any.
     * Invariant: at most one ACTIVE session at any time.
     */
    synchronized SessionEntry getActiveSession() {
        for (SessionEntry e : sessions.values()) {
            if (e.state == SessionState.ACTIVE) return e;
        }
        return null;
    }

    /**
     * Returns the single BACKGROUND session (audio-only) if any.
     * Invariant: at most one BACKGROUND session at any time.
     */
    synchronized SessionEntry getBackgroundSession() {
        for (SessionEntry e : sessions.values()) {
            if (e.state == SessionState.BACKGROUND) return e;
        }
        return null;
    }

    /**
     * True iff some session currently has audio/video flowing
     * (ACTIVE or BACKGROUND). Used by AaService to decide whether
     * a newly-connected phone should be auto-activated.
     */
    synchronized boolean hasAudioOrVideo() {
        for (SessionEntry e : sessions.values()) {
            if (e.state == SessionState.ACTIVE
                    || e.state == SessionState.BACKGROUND) {
                return true;
            }
        }
        return false;
    }

    /** Mark session as BACKGROUND (audio-only, no full-screen). */
    synchronized void background(int sessionId) {
        SessionEntry entry = sessions.get(sessionId);
        if (entry == null) return;
        entry.state = SessionState.BACKGROUND;
        Log.i(TAG, "session background: id=" + sessionId);
    }

    /**
     * Return all sessions whose {@code transportLabel} matches.
     *
     * Used by transport-specific coordinators to find their sessions
     * for cleanup (e.g., USB removed -> stop all "USB" sessions). This
     * keeps the transport->session mapping inside SessionManager rather
     * than as side state on AaService.
     */
    synchronized List<SessionEntry> getSessionsByTransport(String transportLabel) {
        List<SessionEntry> result = new ArrayList<>();
        for (SessionEntry e : sessions.values()) {
            if (transportLabel.equals(e.transportLabel)) {
                result.add(e);
            }
        }
        return result;
    }

    synchronized boolean hasAnySession() {
        return !sessions.isEmpty();
    }
}
