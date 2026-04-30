package com.aauto.app;

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiManager;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;

import com.aauto.app.wireless.BtProfileGate;
import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

/**
 * Background service bridging the Android app with aa-engine daemon.
 *
 * Responsibilities (kept minimal — see Part F.18 in architecture_review.md):
 *   - Service lifecycle (onCreate/onDestroy/onBind)
 *   - Hold engine proxy and route engine->app callbacks
 *   - Own active-session policy (activate/setSurface/touch routing)
 *   - Provide read API to UI (Activity)
 *
 * Transport-specific session lifecycle is owned by the transport
 * coordinators ({@link UsbSessionCoordinator}, {@link WirelessSessionCoordinator}).
 * Per-session bookkeeping lives in {@link SessionManager}, including the
 * transport->session_id mapping previously held as side state here.
 *
 * AaService implements exactly one callback interface
 * ({@link SessionLifecycleListener}). Engine connection events
 * are received as Consumer/Runnable lambdas passed into
 * {@link EngineConnectionManager}.
 */
public class AaService extends Service implements SessionLifecycleListener {
    private static final String TAG = "AA.Service";
    private static final int TCP_PORT = 5277;

    private volatile IAAEngine engineProxy;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SessionManager sessionManager = new SessionManager();

    private BtProfileGate btProfileGate;
    private final WirelessStateTracker wirelessState = new WirelessStateTracker();
    private final HotspotConfigProvider hotspotConfigProvider = new HotspotConfigProvider();

    private UsbSessionCoordinator usbCoordinator;
    private WirelessSessionCoordinator wirelessCoordinator;
    private EngineConnectionManager engineConnectionManager;
    private UiNavigationController uiNavigationController;
    private BluetoothPairingCoordinator btPairingCoordinator;
    private final PlaybackController playbackController = new PlaybackController();

    /** True between setSurface(real) and setSurface(null). Used by
     *  activateSession to decide whether to kick VIDEO_FOCUS_PROJECTED
     *  immediately (surface already up — transport switch path) or
     *  wait for AaDisplayActivity.surfaceCreated to trigger it. */
    private boolean surfaceLive;

    /**
     * Latest media playback state for a specific session.
     * Updated from the engine callback on the main handler thread; UI
     * reads it from the same thread, so no synchronization needed.
     */
    public static final class PlaybackInfo {
        public int  sessionId       = -1;
        public int  state           = 0;     // 1=STOPPED, 2=PLAYING, 3=PAUSED
        public String mediaSource   = "";
        public int  playbackSeconds = 0;
        public boolean shuffle      = false;
        public boolean repeat       = false;
        public boolean repeatOne    = false;
        public String song          = "";
        public String artist        = "";
        public String album         = "";
        public byte[] albumArt      = null;  // PNG/JPEG bytes; may be null
        public String playlist      = "";
        public int  durationSeconds = 0;

        void update(int sid, int s, String src, int pos,
                    boolean shuf, boolean rep, boolean repOne) {
            sessionId = sid;
            state = s;
            mediaSource = src != null ? src : "";
            playbackSeconds = pos;
            shuffle = shuf;
            repeat = rep;
            repeatOne = repOne;
        }

        void updateMetadata(int sid, String s, String ar, String al,
                            byte[] art, String pl, int dur) {
            sessionId = sid;
            song = s != null ? s : "";
            artist = ar != null ? ar : "";
            album = al != null ? al : "";
            albumArt = art;
            playlist = pl != null ? pl : "";
            durationSeconds = dur;
        }
    }

    /** Per-session playback cache (key = session_id). */
    private final java.util.Map<Integer, PlaybackInfo> playbackCache =
            new java.util.HashMap<>();

    private PlaybackInfo getOrCreatePlayback(int sessionId) {
        PlaybackInfo info = playbackCache.get(sessionId);
        if (info == null) {
            info = new PlaybackInfo();
            playbackCache.put(sessionId, info);
        }
        return info;
    }

    public interface DeviceStateListener {
        void onDeviceStateChanged();
    }

    private DeviceStateListener deviceStateListener;

    public void setDeviceStateListener(DeviceStateListener listener) {
        this.deviceStateListener = listener;
    }

    private void notifyDeviceStateChanged() {
        if (deviceStateListener != null) {
            handler.post(() -> deviceStateListener.onDeviceStateChanged());
        }
    }

    public class LocalBinder extends Binder {
        public AaService getService() {
            return AaService.this;
        }
    }

    private final LocalBinder binder = new LocalBinder();

    public static final String ACTION_SESSION_ENDED = "com.aauto.app.SESSION_ENDED";
    public static final String ACTION_VIDEO_FOCUS_CHANGED = "com.aauto.app.VIDEO_FOCUS_CHANGED";
    public static final String EXTRA_PROJECTED = "projected";

    // ===== Engine callbacks (Binder thread) =====

    private final IAAEngineCallback.Stub engineCallback =
            new IAAEngineCallback.Stub() {
        @Override
        public void onSessionStateChanged(int sessionId, int status) {
            Log.i(TAG, "session " + sessionId + " state: " + status);
        }

        @Override
        public void onSessionError(int sessionId, int errorCode, String message) {
            Log.e(TAG, "session " + sessionId + " error " + errorCode
                    + ": " + message);
            handler.post(() -> removeSession(sessionId));
        }

        @Override
        public void onSessionConfig(int sessionId, int videoWidth, int videoHeight) {
            Log.i(TAG, "session config: " + videoWidth + "x" + videoHeight);
            playbackController.onSessionConfig(videoWidth, videoHeight);
        }

        @Override
        public void onVideoData(int sessionId, int channel, byte[] data,
                                long timestampUs, boolean isConfig) {
            // Day 1: only the main video channel (1) is wired through
            // to the decoder. Cluster (channel 15) frames are dropped
            // here until Day 3 adds the cluster decoder + TextureView.
            if (channel == 1) {
                playbackController.onVideoData(data, timestampUs, isConfig);
            }
        }

        @Override
        public void onAudioData(int sessionId, int streamType, byte[] data,
                                long timestampUs) {
            playbackController.onAudioData(streamType, data);
        }

        @Override
        public void onPhoneIdentified(int sessionId, String deviceName) {
            Log.i(TAG, "phone identified: session=" + sessionId
                    + " name=" + deviceName);
            handler.post(() -> {
                // Close existing session from the same phone (transport switch)
                for (SessionManager.SessionEntry e : sessionManager.getAll()) {
                    if (e.sessionId != sessionId
                            && deviceName.equals(e.deviceName)) {
                        Log.i(TAG, "same phone on session " + e.sessionId
                                + ", closing old session");
                        stopAndRemoveSession(e.sessionId);
                        break;
                    }
                }
                sessionManager.onPhoneIdentified(sessionId, deviceName);
                // Scenario 1: if no other phone is currently playing
                // audio/video (no ACTIVE / BG), auto-promote this one.
                // Scenario 2: otherwise leave it CONNECTED (no-op).
                if (!sessionManager.hasAudioOrVideo()) {
                    Log.i(TAG, "auto-activating session " + sessionId
                            + " (no other audio/video session)");
                    activateSession(sessionId);
                }
                notifyDeviceStateChanged();
            });
        }

        @Override
        public void onVideoFocusChanged(int sessionId, boolean projected) {
            Log.i(TAG, "video focus changed: session=" + sessionId
                    + " projected=" + projected);
            handler.post(() -> {
                sessionManager.onVideoFocusChanged(sessionId, projected);
                if (!projected && uiNavigationController != null) {
                    // Phone-initiated NATIVE means "hide AA video, keep
                    // session BACKGROUND for audio-only". Bring our own
                    // device list to the front first; otherwise the
                    // system would fall through to CarLauncher when
                    // AaDisplayActivity finishes itself on the broadcast.
                    uiNavigationController.showDeviceList();
                }
                Intent intent = new Intent(ACTION_VIDEO_FOCUS_CHANGED);
                intent.putExtra(EXTRA_PROJECTED, projected);
                sendBroadcast(intent);
                notifyDeviceStateChanged();
            });
        }

        @Override
        public void onPlaybackStatus(int sessionId, int state, String mediaSource,
                                     int playbackSeconds, boolean shuffle,
                                     boolean repeat, boolean repeatOne) {
            handler.post(() -> {
                getOrCreatePlayback(sessionId).update(sessionId, state, mediaSource,
                        playbackSeconds, shuffle, repeat, repeatOne);
                notifyDeviceStateChanged();
            });
        }

        @Override
        public void onPlaybackMetadata(int sessionId, String song, String artist,
                                       String album, byte[] albumArt,
                                       String playlist, int durationSeconds) {
            handler.post(() -> {
                getOrCreatePlayback(sessionId).updateMetadata(sessionId, song, artist,
                        album, albumArt, playlist, durationSeconds);
                notifyDeviceStateChanged();
            });
        }

        @Override
        public void onPairingRequest(int sessionId, String phoneAddress, int method) {
            handler.post(() -> {
                if (btPairingCoordinator != null) {
                    btPairingCoordinator.onPairingRequest(sessionId, phoneAddress, method);
                }
            });
        }

        @Override
        public void onAuthData(int sessionId, String authData, int method) {
            handler.post(() -> {
                if (btPairingCoordinator != null) {
                    btPairingCoordinator.onAuthData(sessionId, authData, method);
                }
            });
        }
    };

    // ===== WiFi AP / BT state receivers =====

    private final BroadcastReceiver apStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int state = intent.getIntExtra(WifiManager.EXTRA_WIFI_AP_STATE,
                    WifiManager.WIFI_AP_STATE_FAILED);
            if (state == WifiManager.WIFI_AP_STATE_ENABLED) {
                Log.i(TAG, "WiFi AP enabled");
                startWirelessListeningIfReady();
            } else if (state == WifiManager.WIFI_AP_STATE_DISABLED) {
                Log.i(TAG, "WiFi AP disabled");
                stopWirelessListening();
            }
            notifyDeviceStateChanged();
        }
    };

    private final BroadcastReceiver btStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE,
                    BluetoothAdapter.ERROR);
            if (state == BluetoothAdapter.STATE_ON) {
                Log.i(TAG, "Bluetooth on");
                pushBluetoothMacToEngine();
                startWirelessListeningIfReady();
            } else if (state == BluetoothAdapter.STATE_TURNING_OFF) {
                Log.i(TAG, "Bluetooth turning off");
                stopWirelessListening();
            }
            notifyDeviceStateChanged();
        }
    };

    /**
     * Read the head unit's actual BT MAC from BluetoothAdapter and
     * push it to the engine. Without this the AAP SDR advertises a
     * placeholder MAC that doesn't match the device the phone bonded
     * with — phone retries PAIRING_REQUEST every ~7s.
     */
    private void pushBluetoothMacToEngine() {
        BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) return;
        String mac = adapter.getAddress();
        if (mac == null || mac.isEmpty()) {
            Log.w(TAG, "BluetoothAdapter.getAddress() returned empty");
            return;
        }
        IAAEngine engine = engineProxy;
        if (engine == null) {
            Log.w(TAG, "engineProxy null when pushing BT MAC");
            return;
        }
        try {
            engine.setBluetoothMac(mac);
            Log.i(TAG, "pushed HU BT MAC to engine: " + mac);
        } catch (Exception e) {
            Log.w(TAG, "setBluetoothMac threw", e);
        }
    }

    // ===== Service lifecycle =====

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "AaService created");
        uiNavigationController = new UiNavigationController(this);
        engineConnectionManager = new EngineConnectionManager(
                handler, engineCallback,
                engine -> {
                    engineProxy = engine;
                    // Push BT MAC immediately if the adapter is already on
                    // (no STATE_ON broadcast on a warm boot).
                    pushBluetoothMacToEngine();
                },
                () -> engineProxy = null,
                2000, 10);
        engineConnectionManager.connect();

        usbCoordinator = new UsbSessionCoordinator(
                this, () -> engineProxy, sessionManager, this);
        usbCoordinator.start();

        btProfileGate = new BtProfileGate(this);
        wirelessCoordinator = new WirelessSessionCoordinator(
                this, wirelessState, hotspotConfigProvider, btProfileGate,
                () -> engineProxy, sessionManager, this, TCP_PORT);

        registerReceiver(apStateReceiver,
                new IntentFilter("android.net.wifi.WIFI_AP_STATE_CHANGED"));
        registerReceiver(btStateReceiver,
                new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED));

        btPairingCoordinator = new BluetoothPairingCoordinator(
                this, handler, () -> engineProxy);
        btPairingCoordinator.register();

        startWirelessListeningIfReady();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "AaService destroying");
        handler.removeCallbacksAndMessages(null);
        playbackController.shutdown();
        if (usbCoordinator != null) {
            usbCoordinator.shutdown();
            usbCoordinator = null;
        }
        if (wirelessCoordinator != null) {
            wirelessCoordinator.shutdown();
            wirelessCoordinator = null;
        }
        if (engineConnectionManager != null) {
            engineConnectionManager.shutdown();
            engineConnectionManager = null;
        }
        uiNavigationController = null;
        btProfileGate = null;
        if (btPairingCoordinator != null) {
            btPairingCoordinator.unregister();
            btPairingCoordinator = null;
        }
        try { unregisterReceiver(apStateReceiver); } catch (Exception ignored) {}
        try { unregisterReceiver(btStateReceiver); } catch (Exception ignored) {}
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return binder; }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    // ===== SessionLifecycleListener (from coordinators) =====

    @Override
    public void onTransportStateChanged() {
        notifyDeviceStateChanged();
    }

    @Override
    public void onSessionShouldStop(int sessionId) {
        stopAndRemoveSession(sessionId);
    }

    // ===== Session cleanup helpers =====

    private void stopAndRemoveSession(int sessionId) {
        if (engineProxy != null) {
            try { engineProxy.stopSession(sessionId); }
            catch (RemoteException e) { Log.w(TAG, "stopSession failed", e); }
        }
        removeSession(sessionId);
    }

    private void removeSession(int sessionId) {
        SessionManager.SessionEntry entry = sessionManager.removeSession(sessionId);
        if (entry != null && entry.state == SessionManager.SessionState.ACTIVE) {
            playbackController.clearSessionPlayback();
            sendBroadcast(new Intent(ACTION_SESSION_ENDED));
        }
        playbackCache.remove(sessionId);
        notifyDeviceStateChanged();
    }

    // ===== Wireless management =====

    private void startWirelessListeningIfReady() {
        if (wirelessCoordinator != null) {
            wirelessCoordinator.startListeningIfReady();
        }
    }

    private void stopWirelessListening() {
        if (wirelessCoordinator != null) {
            wirelessCoordinator.stopListening();
        }
    }

    // ===== Public API for Activity =====

    public SessionManager getSessionManager() {
        return sessionManager;
    }

    /** Per-session lookup. */
    public PlaybackInfo getPlaybackInfo(int sessionId) {
        return playbackCache.get(sessionId);
    }

    /**
     * Convenience for the device-list UI: return playback info for the
     * currently BACKGROUND session (the one whose audio is playing
     * while the user is on the device list). null if no BG session.
     */
    public PlaybackInfo getBackgroundPlaybackInfo() {
        SessionManager.SessionEntry bg = sessionManager.getBackgroundSession();
        if (bg == null) return null;
        return playbackCache.get(bg.sessionId);
    }

    public boolean isBluetoothEnabled() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        return bt != null && bt.isEnabled();
    }

    public boolean isWifiApEnabled() {
        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        return wm != null && wm.getWifiApState() == WifiManager.WIFI_AP_STATE_ENABLED;
    }

    /**
     * Promote a session to ACTIVE — full AA screen + all sinks.
     * Used by short-press on the device list, and by the auto-activate
     * path on phone connect when no other session is playing.
     *
     * Demotes any other ACTIVE/BACKGROUND session to CONNECTED
     * (audio focus loss + detach), enforcing the single-active /
     * single-background invariant.
     */
    public void activateSession(int sessionId) {
        if (engineProxy == null) return;
        SessionManager.SessionEntry entry = sessionManager.getSession(sessionId);
        if (entry == null || entry.state == SessionManager.SessionState.CONNECTING) {
            Log.w(TAG, "session " + sessionId + " not ready to activate");
            return;
        }
        try {
            demoteOtherSessions(sessionId);
            // Prepare session — attach sinks. VideoFocus(PROJECTED) is
            // sent next, either:
            //  (a) directly here when AaDisplayActivity is already
            //      visible with a live Surface — happens on transport
            //      switches (USB <-> Wireless, same phone) where the
            //      activity stays put across the session swap; or
            //  (b) from AaDisplayActivity.surfaceCreated -> onSurfaceReady
            //      when we still have to launch the activity (cold start).
            // Either way the trigger funnels through onSurfaceReady() so
            // there's a single place enforcing the F.14 invariant.
            engineProxy.attachAllSinks(sessionId);
            resumePhoneMedia(sessionId);
            sessionManager.activate(sessionId);
            if (uiNavigationController != null) {
                uiNavigationController.showDisplayFlow();
            }
            // Path (a): if a Surface is already live, kick the focus
            // signal now. Harmless when the activity isn't up yet —
            // onSurfaceReady is a no-op without an ACTIVE session.
            onSurfaceReady();
        } catch (RemoteException e) {
            Log.e(TAG, "activateSession failed", e);
        }
    }

    /**
     * Move a session to BACKGROUND (audio-only, no AA screen).
     * Used by long-press on the device list. Sinks are attached but
     * VideoFocus(PROJECTED) is intentionally NOT sent — the phone keeps
     * its native screen and only audio flows.
     *
     * Single-background invariant: any prior BG (or ACTIVE) session is
     * demoted to CONNECTED via the same audio-focus-loss path.
     */
    public void activateBackground(int sessionId) {
        if (engineProxy == null) return;
        SessionManager.SessionEntry entry = sessionManager.getSession(sessionId);
        if (entry == null || entry.state == SessionManager.SessionState.CONNECTING) {
            Log.w(TAG, "session " + sessionId + " not ready to background");
            return;
        }
        if (entry.state == SessionManager.SessionState.BACKGROUND) {
            return; // already BG
        }
        try {
            demoteOtherSessions(sessionId);
            engineProxy.attachAllSinks(sessionId);
            resumePhoneMedia(sessionId);
            // Don't call setVideoFocus here — without PROJECTED the phone
            // won't push video frames. We intentionally only get audio.
            sessionManager.background(sessionId);
        } catch (RemoteException e) {
            Log.e(TAG, "activateBackground failed", e);
        }
    }

    /**
     * Best-effort make the phone's media app start (or keep) playing.
     *
     * AudioFocus(GAIN) tells the phone HU has audio focus again — but
     * empirically a phone in PAUSED state doesn't resume from
     * unsolicited GAIN alone. KEYCODE_MEDIA_PLAY is sent right after
     * as a positive instruction; if the phone is already PLAYING the
     * media key is idempotent (PLAY != PLAY_PAUSE).
     */
    private void resumePhoneMedia(int sessionId) throws RemoteException {
        engineProxy.gainAudioFocus(sessionId);
        engineProxy.sendMediaKey(sessionId, KEYCODE_MEDIA_PLAY);
    }

    private static final int KEYCODE_MEDIA_PLAY = 126;

    /**
     * Demote every session except {@code keepSessionId} that is in
     * ACTIVE or BACKGROUND state down to CONNECTED. Sends
     * AudioFocus(LOSS) so the phone's media app stops actually playing.
     */
    private void demoteOtherSessions(int keepSessionId) throws RemoteException {
        for (SessionManager.SessionEntry e : sessionManager.getAll()) {
            if (e.sessionId == keepSessionId) continue;
            if (e.state != SessionManager.SessionState.ACTIVE
                    && e.state != SessionManager.SessionState.BACKGROUND) continue;
            Log.i(TAG, "demoting session " + e.sessionId
                    + " (was " + e.state + ")");
            engineProxy.setVideoFocus(e.sessionId, false);
            engineProxy.releaseAudioFocus(e.sessionId);
            engineProxy.detachAllSinks(e.sessionId);
            sessionManager.deactivate(e.sessionId);
        }
    }

    /**
     * Send a media-control key (KeyCode.proto KEYCODE_MEDIA_*) to the
     * phone whose audio is currently playing. Targets the BACKGROUND
     * session by default; falls back to ACTIVE for completeness.
     */
    public void sendMediaKey(int keycode) {
        if (engineProxy == null) return;
        SessionManager.SessionEntry target = sessionManager.getBackgroundSession();
        if (target == null) target = sessionManager.getActiveSession();
        if (target == null) {
            Log.w(TAG, "sendMediaKey: no active/background session");
            return;
        }
        try {
            engineProxy.sendMediaKey(target.sessionId, keycode);
        } catch (RemoteException e) {
            Log.w(TAG, "sendMediaKey failed", e);
        }
    }

    /**
     * Surface is ready — send VideoFocus(PROJECTED) to the ACTIVE session.
     */
    public void onSurfaceReady() {
        if (engineProxy == null) return;
        if (!surfaceLive) return;
        for (SessionManager.SessionEntry e : sessionManager.getAll()) {
            if (e.state == SessionManager.SessionState.ACTIVE) {
                Log.i(TAG, "surface ready, sending PROJECTED for session "
                        + e.sessionId);
                try {
                    engineProxy.setVideoFocus(e.sessionId, true);
                } catch (RemoteException ex) {
                    Log.w(TAG, "setVideoFocus failed", ex);
                }
                return;
            }
        }
    }

    /**
     * Surface destroyed — send VideoFocus(NATIVE) to the ACTIVE session.
     */
    public void onSurfaceDestroyed() {
        if (engineProxy == null) return;
        for (SessionManager.SessionEntry e : sessionManager.getAll()) {
            if (e.state == SessionManager.SessionState.ACTIVE) {
                Log.i(TAG, "surface destroyed, sending NATIVE for session "
                        + e.sessionId);
                try {
                    engineProxy.setVideoFocus(e.sessionId, false);
                    sessionManager.onVideoFocusChanged(e.sessionId, false);
                } catch (RemoteException ex) {
                    Log.w(TAG, "setVideoFocus failed", ex);
                }
            }
        }
        notifyDeviceStateChanged();
    }

    public int getVideoWidth() { return playbackController.getVideoWidth(); }
    public int getVideoHeight() { return playbackController.getVideoHeight(); }

    public void sendTouchEvent(int x, int y, int action) {
        // Send to the ACTIVE session
        for (SessionManager.SessionEntry e : sessionManager.getAll()) {
            if (e.state == SessionManager.SessionState.ACTIVE && engineProxy != null) {
                try {
                    engineProxy.sendTouchEvent(e.sessionId, x, y, action);
                } catch (RemoteException ex) {
                    Log.w(TAG, "sendTouchEvent failed", ex);
                }
                return;
            }
        }
    }

    public void setSurface(Surface surface) {
        playbackController.setSurface(surface);
        surfaceLive = (surface != null);
        Log.i(TAG, "setSurface: " + (surface != null ? "decoder ready"
                : "decoder released"));
    }
}
