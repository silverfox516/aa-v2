package com.aauto.engine;

/**
 * Binder callback: aa-engine daemon -> App.
 *
 * Engine notifies the app of session lifecycle events.
 */
interface IAAEngineCallback {
    /** Session state changed. Status values match engine::SessionStatus enum. */
    void onSessionStateChanged(int sessionId, int status);

    /** Session error. errorCode matches AapErrc enum values. */
    void onSessionError(int sessionId, int errorCode, String message);
}
