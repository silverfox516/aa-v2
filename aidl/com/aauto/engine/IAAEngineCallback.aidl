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
     * @param channel originating video sink channel (1 = main display,
     *                15 = cluster display, etc.). The app routes the
     *                data to the corresponding decoder/Surface.
     * @param isConfig true for codec config (SPS/PPS), false for frame data.
     */
    oneway void onVideoData(int sessionId, int channel, in byte[] data,
                            long timestampUs, boolean isConfig);

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

    /**
     * Media playback status from phone (channel 10).
     * Phone broadcasts this every ~1s while playing — pos increments,
     * state transitions PAUSED/PLAYING/STOPPED, etc.
     * @param state 0=unknown, 1=STOPPED, 2=PLAYING, 3=PAUSED
     */
    oneway void onPlaybackStatus(int sessionId, int state, String mediaSource,
                                 int playbackSeconds, boolean shuffle,
                                 boolean repeat, boolean repeatOne);

    /**
     * Media playback metadata from phone (channel 10).
     * Sent on track change / playback start. albumArt is PNG/JPEG bytes
     * (typically 3KB-90KB depending on the album). playlist is the
     * current playlist/queue name when the source app provides it
     * (often empty).
     */
    oneway void onPlaybackMetadata(int sessionId, String song, String artist,
                                   String album, in byte[] albumArt,
                                   String playlist, int durationSeconds);
}
