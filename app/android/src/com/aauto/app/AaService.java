package com.aauto.app;

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.net.wifi.WifiManager;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;

import com.aauto.app.wireless.BtProfileGate;
import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

/**
 * Background service bridging the Android app with aa-engine daemon.
 * Manages both USB and wireless AA connections.
 */
public class AaService extends Service
        implements UsbMonitor.Listener,
        WirelessSessionCoordinator.Callback,
        EngineConnectionManager.Callback,
        SessionLifecycleController.Callback {
    private static final String TAG = "AA.Service";
    private static final int ENGINE_CONNECT_RETRY_MS = 2000;
    private static final int ENGINE_CONNECT_MAX_RETRIES = 10;
    private static final int TCP_PORT = 5277;

    private UsbMonitor usbMonitor;
    private IAAEngine engineProxy;
    private final Handler handler = new Handler(Looper.getMainLooper());

    // USB state
    private UsbDevice availableDevice;

    // Wireless state
    private BtProfileGate btProfileGate;
    private final WirelessStateTracker wirelessState = new WirelessStateTracker();
    private final HotspotConfigProvider hotspotConfigProvider = new HotspotConfigProvider();
    private WirelessSessionCoordinator wirelessCoordinator;
    private EngineConnectionManager engineConnectionManager;
    private SessionLifecycleController sessionLifecycleController;
    private UiNavigationController uiNavigationController;

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

    private final PlaybackController playbackController = new PlaybackController();

    public static final String ACTION_SESSION_ENDED = "com.aauto.app.SESSION_ENDED";

    private final IAAEngineCallback.Stub engineCallback =
            new IAAEngineCallback.Stub() {
        @Override
        public void onSessionStateChanged(int sessionId, int status) {
            Log.i(TAG, "session " + sessionId + " state: " + status);
        }

        @Override
        public void onSessionError(int sessionId, int errorCode, String message) {
            Log.e(TAG, "session " + sessionId + " error " + errorCode +
                    ": " + message);
            cleanupSession();
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
    };

    // ===== WiFi AP state receiver =====

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

    // ===== BT state receiver =====

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

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "AaService created");
        uiNavigationController = new UiNavigationController(this);
        sessionLifecycleController = new SessionLifecycleController(
                this,
                wirelessState,
                playbackController,
                this);
        engineConnectionManager = new EngineConnectionManager(
                handler,
                engineCallback,
                this,
                ENGINE_CONNECT_RETRY_MS,
                ENGINE_CONNECT_MAX_RETRIES);
        engineConnectionManager.connect();
        usbMonitor = new UsbMonitor(this, this);
        usbMonitor.start();

        btProfileGate = new BtProfileGate(this);
        wirelessCoordinator = new WirelessSessionCoordinator(
                this,
                wirelessState,
                hotspotConfigProvider,
                btProfileGate,
                this);

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
        usbMonitor.stop();
        if (wirelessCoordinator != null) {
            wirelessCoordinator.shutdown();
            wirelessCoordinator = null;
        }
        if (engineConnectionManager != null) {
            engineConnectionManager.shutdown();
            engineConnectionManager = null;
        }
        sessionLifecycleController = null;
        uiNavigationController = null;
        btProfileGate = null;
        try { unregisterReceiver(apStateReceiver); } catch (Exception ignored) {}
        try { unregisterReceiver(btStateReceiver); } catch (Exception ignored) {}
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    // ===== UsbMonitor.Listener =====

    @Override
    public void onDeviceAvailable(UsbDevice device) {
        Log.i(TAG, "USB device available: " + device.getProductName());
        availableDevice = device;
        notifyDeviceStateChanged();
        if (uiNavigationController != null) {
            uiNavigationController.showDeviceList();
        }
    }

    @Override
    public void onDeviceReady(int fd, int epIn, int epOut) {
        if (sessionLifecycleController != null) {
            sessionLifecycleController.startUsbSession(engineProxy, fd, epIn, epOut);
        }
    }

    @Override
    public void onDeviceRemoved() {
        Log.i(TAG, "USB device removed");
        availableDevice = null;
        if (sessionLifecycleController != null
                && sessionLifecycleController.hasActiveSession()) {
            sessionLifecycleController.stopActiveSession(engineProxy);
            sessionLifecycleController.cleanupSession();
        }
        notifyDeviceStateChanged();
    }

    // ===== WirelessSessionCoordinator.Callback =====

    @Override
    public void onWirelessStateChanged() {
        notifyDeviceStateChanged();
    }

    @Override
    public void onWirelessReadyToStartSession(String deviceId, String deviceName) {
        Log.i(TAG, "wireless ready: " + deviceId + " (" + deviceName + ")");
        if (sessionLifecycleController != null) {
            sessionLifecycleController.startWirelessSession(engineProxy, TCP_PORT);
            if (sessionLifecycleController.hasActiveSession()
                    && uiNavigationController != null) {
                uiNavigationController.showDisplayFlow();
            }
        }
    }

    @Override
    public void onWirelessDisconnected(String deviceId, String reason) {
        Log.i(TAG, "wireless disconnected: " + deviceId + " — " + reason);

        if (sessionLifecycleController != null
                && sessionLifecycleController.hasActiveSession()) {
            sessionLifecycleController.stopActiveSession(engineProxy);
            sessionLifecycleController.cleanupSession();
        }
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

    // ===== SessionLifecycleController.Callback =====

    @Override
    public void onSessionStateChanged() {
        notifyDeviceStateChanged();
    }

    // ===== Wireless management =====

    private void startWirelessListeningIfReady() {
        if (wirelessCoordinator != null) {
            wirelessCoordinator.startListeningIfReady(TCP_PORT);
        }
    }

    private void stopWirelessListening() {
        if (wirelessCoordinator != null) {
            wirelessCoordinator.stopListening();
        }
    }

    // ===== Public API for Activity =====

    public UsbDevice getAvailableDevice() {
        return availableDevice;
    }

    public String getWirelessDeviceName() {
        return wirelessState.getDeviceName();
    }

    public boolean isWirelessConnecting() {
        return wirelessState.isConnecting();
    }

    public boolean isWirelessReady() {
        return wirelessState.isReady();
    }

    public boolean hasActiveSession() {
        return sessionLifecycleController != null
                && sessionLifecycleController.hasActiveSession();
    }

    public boolean isBluetoothEnabled() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        return bt != null && bt.isEnabled();
    }

    public boolean isWifiApEnabled() {
        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        return wm.getWifiApState() == WifiManager.WIFI_AP_STATE_ENABLED;
    }

    /**
     * Connect to the available USB device and start AA session.
     */
    public void connectDevice() {
        if (availableDevice == null) {
            Log.w(TAG, "no available device");
            return;
        }
        if (!usbMonitor.connectPendingDevice()) {
            Log.e(TAG, "failed to open USB device");
            return;
        }
        if (uiNavigationController != null) {
            uiNavigationController.showDisplayFlow();
        }
    }

    public int getVideoWidth() { return playbackController.getVideoWidth(); }
    public int getVideoHeight() { return playbackController.getVideoHeight(); }

    public void sendTouchEvent(int x, int y, int action) {
        if (engineProxy == null || sessionLifecycleController == null) return;
        int currentSessionId = sessionLifecycleController.getCurrentSessionId();
        if (currentSessionId <= 0) return;
        try {
            engineProxy.sendTouchEvent(currentSessionId, x, y, action);
        } catch (RemoteException e) {
            Log.w(TAG, "sendTouchEvent failed", e);
        }
    }

    public void setSurface(Surface surface) {
        playbackController.setSurface(surface);
        Log.i(TAG, "setSurface: %s", surface != null ? "decoder ready" : "decoder released");
    }

}
