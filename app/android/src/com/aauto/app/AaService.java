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
 */
public class AaService extends Service
        implements EngineConnectionManager.Callback, SessionLifecycleListener {
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
    private final PlaybackController playbackController = new PlaybackController();

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
        public void onVideoData(int sessionId, byte[] data, long timestampUs,
                                boolean isConfig) {
            playbackController.onVideoData(data, timestampUs, isConfig);
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
                notifyDeviceStateChanged();
            });
        }

        @Override
        public void onVideoFocusChanged(int sessionId, boolean projected) {
            Log.i(TAG, "video focus changed: session=" + sessionId
                    + " projected=" + projected);
            handler.post(() -> {
                sessionManager.onVideoFocusChanged(sessionId, projected);
                Intent intent = new Intent(ACTION_VIDEO_FOCUS_CHANGED);
                intent.putExtra(EXTRA_PROJECTED, projected);
                sendBroadcast(intent);
                notifyDeviceStateChanged();
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
                startWirelessListeningIfReady();
            } else if (state == BluetoothAdapter.STATE_TURNING_OFF) {
                Log.i(TAG, "Bluetooth turning off");
                stopWirelessListening();
            }
            notifyDeviceStateChanged();
        }
    };

    // ===== Service lifecycle =====

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "AaService created");
        uiNavigationController = new UiNavigationController(this);
        engineConnectionManager = new EngineConnectionManager(
                handler, engineCallback, this, 2000, 10);
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

    // ===== EngineConnectionManager.Callback =====

    @Override
    public void onEngineConnected(IAAEngine engine) {
        engineProxy = engine;
    }

    @Override
    public void onEngineDisconnected() {
        engineProxy = null;
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

    public boolean isBluetoothEnabled() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        return bt != null && bt.isEnabled();
    }

    public boolean isWifiApEnabled() {
        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        return wm != null && wm.getWifiApState() == WifiManager.WIFI_AP_STATE_ENABLED;
    }

    /**
     * Activate a session — deactivate current ACTIVE (if any), then activate new.
     * Sends VideoFocus(NATIVE) to old, VideoFocus(PROJECTED) to new.
     */
    public void activateSession(int sessionId) {
        if (engineProxy == null) return;
        SessionManager.SessionEntry entry = sessionManager.getSession(sessionId);
        if (entry == null || entry.state == SessionManager.SessionState.CONNECTING) {
            Log.w(TAG, "session " + sessionId + " not ready to activate");
            return;
        }
        try {
            // Deactivate current ACTIVE/BACKGROUND session (if different)
            for (SessionManager.SessionEntry e : sessionManager.getAll()) {
                if ((e.state == SessionManager.SessionState.ACTIVE
                        || e.state == SessionManager.SessionState.BACKGROUND)
                        && e.sessionId != sessionId) {
                    Log.i(TAG, "deactivating session " + e.sessionId);
                    engineProxy.setVideoFocus(e.sessionId, false);
                    engineProxy.detachAllSinks(e.sessionId);
                    sessionManager.deactivate(e.sessionId);
                }
            }
            // Prepare session — attach sinks, open display.
            // VideoFocus(PROJECTED) will be sent when Surface is ready.
            engineProxy.attachAllSinks(sessionId);
            sessionManager.activate(sessionId);
            if (uiNavigationController != null) {
                uiNavigationController.showDisplayFlow();
            }
        } catch (RemoteException e) {
            Log.e(TAG, "activateSession failed", e);
        }
    }

    /**
     * Surface is ready — send VideoFocus(PROJECTED) to the ACTIVE session.
     */
    public void onSurfaceReady() {
        if (engineProxy == null) return;
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
        Log.i(TAG, "setSurface: " + (surface != null ? "decoder ready"
                : "decoder released"));
    }
}
