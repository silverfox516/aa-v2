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

    /** Video/display configuration for this session. Sent once at session start. */
    void onSessionConfig(int sessionId, int videoWidth, int videoHeight);

    /**
     * Video data from phone (compressed H.264 NALUs).
     * App decodes via MediaCodec and renders to Surface.
     * @param isConfig true for codec config (SPS/PPS), false for frame data.
     */
    oneway void onVideoData(int sessionId, in byte[] data, long timestampUs,
                            boolean isConfig);

    /**
     * Audio data from phone (PCM samples).
     * @param streamType 1=media, 2=guidance, 3=system (matches AudioStreamType).
     */
    oneway void onAudioData(int sessionId, int streamType, in byte[] data,
                            long timestampUs);

    /** Phone identified after ServiceDiscovery. */
    void onPhoneIdentified(int sessionId, String deviceName);

    /** Phone requested video focus change (e.g., "exit" button pressed). */
    void onVideoFocusChanged(int sessionId, boolean projected);
}
