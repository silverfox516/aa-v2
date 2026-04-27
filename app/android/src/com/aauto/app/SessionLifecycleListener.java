package com.aauto.app;

/**
 * Shared callback contract used by transport-specific session coordinators
 * (USB, Wireless) to notify the host service of session-related events
 * without coupling each coordinator to its own bespoke listener interface.
 *
 * The coordinators own their own session lifecycle internally (calling
 * engine startSession/startTcpSession). They notify through this listener
 * only for two cross-cutting concerns that the host owns:
 *   - UI/state refresh whenever a transport state changes
 *   - request that a given session be stopped and cleaned up (the host
 *     centralizes stopAndRemoveSession because cleanup spans broadcast
 *     emission, playback teardown, and SessionManager removal).
 */
interface SessionLifecycleListener {
    /**
     * A transport-related state changed (USB attached/removed, BT/WiFi
     * AP toggled, RFCOMM peer connecting, session created, etc.). The
     * host should refresh any state it exposes to the UI.
     */
    void onTransportStateChanged();

    /**
     * Coordinator requests the given session be stopped and fully
     * cleaned up. The host should call engine.stopSession(sid) and
     * remove the entry from SessionManager.
     */
    void onSessionShouldStop(int sessionId);
}
