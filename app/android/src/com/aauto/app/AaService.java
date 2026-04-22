package com.aauto.app;

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;
import android.view.Surface;

import com.aauto.app.wireless.BluetoothWirelessManager;
import com.aauto.app.wireless.BtProfileGate;
import com.aauto.engine.IAAEngine;
import com.aauto.engine.IAAEngineCallback;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/**
 * Background service bridging the Android app with aa-engine daemon.
 * Manages both USB and wireless AA connections.
 */
public class AaService extends Service
        implements UsbMonitor.Listener, BluetoothWirelessManager.Listener {
    private static final String TAG = "AA.Service";
    private static final int ENGINE_CONNECT_RETRY_MS = 2000;
    private static final int ENGINE_CONNECT_MAX_RETRIES = 10;
    private static final int TCP_PORT = 5277;

    private UsbMonitor usbMonitor;
    private IAAEngine engineProxy;
    private int currentSessionId = -1;
    private int connectRetryCount = 0;
    private Surface pendingSurface;
    private int videoWidth = 800;
    private int videoHeight = 480;
    private final Handler handler = new Handler(Looper.getMainLooper());

    // USB state
    private UsbDevice availableDevice;

    // Wireless state
    private BluetoothWirelessManager wirelessManager;
    private BtProfileGate btProfileGate;
    private String wirelessDeviceId;
    private String wirelessDeviceName;
    private boolean wirelessReady;

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

    private VideoDecoder videoDecoder;
    private final AudioPlayer audioPlayer = new AudioPlayer();

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
            AaService.this.videoWidth = videoWidth;
            AaService.this.videoHeight = videoHeight;
        }

        @Override
        public void onVideoData(int sessionId, byte[] data, long timestampUs,
                                boolean isConfig) {
            if (videoDecoder != null) {
                videoDecoder.feedData(data, timestampUs, isConfig);
            }
        }

        @Override
        public void onAudioData(int sessionId, int streamType, byte[] data,
                                long timestampUs) {
            audioPlayer.feedData(streamType, data);
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
        connectToEngine();
        usbMonitor = new UsbMonitor(this, this);
        usbMonitor.start();

        btProfileGate = new BtProfileGate(this);

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
        audioPlayer.release();
        usbMonitor.stop();
        stopWirelessListening();
        if (btProfileGate != null) {
            btProfileGate.close();
            btProfileGate = null;
        }
        try { unregisterReceiver(apStateReceiver); } catch (Exception ignored) {}
        try { unregisterReceiver(btStateReceiver); } catch (Exception ignored) {}
        if (engineProxy != null) {
            try { engineProxy.stopAll(); }
            catch (RemoteException e) { Log.w(TAG, "stopAll failed", e); }
        }
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

        Intent intent = new Intent(this, DeviceListActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(intent);
    }

    @Override
    public void onDeviceReady(int fd, int epIn, int epOut) {
        if (engineProxy == null) {
            Log.e(TAG, "engine not connected, cannot start session");
            return;
        }
        try {
            ParcelFileDescriptor pfd = ParcelFileDescriptor.adoptFd(fd);
            currentSessionId = engineProxy.startSession(pfd, epIn, epOut);
            Log.i(TAG, "USB session started: id=" + currentSessionId);
            if (pendingSurface != null && currentSessionId > 0) {
                setSurface(pendingSurface);
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start USB session", e);
        }
    }

    @Override
    public void onDeviceRemoved() {
        Log.i(TAG, "USB device removed");
        availableDevice = null;
        if (currentSessionId > 0) {
            if (engineProxy != null) {
                try { engineProxy.stopSession(currentSessionId); }
                catch (RemoteException e) { Log.w(TAG, "stopSession failed", e); }
            }
            cleanupSession();
        }
        notifyDeviceStateChanged();
    }

    // ===== BluetoothWirelessManager.Listener =====

    @Override
    public void onDeviceConnecting(String deviceId, String deviceName) {
        Log.i(TAG, "wireless connecting: " + deviceId + " (" + deviceName + ")");
        wirelessDeviceId = deviceId;
        wirelessDeviceName = deviceName;
        wirelessReady = false;
        notifyDeviceStateChanged();
    }

    @Override
    public void onDeviceReady(String deviceId, String deviceName) {
        Log.i(TAG, "wireless ready: " + deviceId + " (" + deviceName + ")");
        wirelessReady = true;
        notifyDeviceStateChanged();

        // Block A2DP/AVRCP to prevent BT media pause during wireless AA
        if (btProfileGate != null) {
            btProfileGate.block(deviceId);
        }

        // Start TCP AAP session
        if (engineProxy == null) {
            Log.e(TAG, "engine not connected, cannot start wireless session");
            return;
        }
        try {
            currentSessionId = engineProxy.startTcpSession(TCP_PORT);
            Log.i(TAG, "wireless session started: id=" + currentSessionId);

            // Launch AaDisplayActivity
            android.app.TaskStackBuilder.create(this)
                    .addNextIntent(new Intent(this, DeviceListActivity.class))
                    .addNextIntent(new Intent(this, AaDisplayActivity.class))
                    .startActivities();

            if (pendingSurface != null && currentSessionId > 0) {
                setSurface(pendingSurface);
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start wireless session", e);
        }
    }

    @Override
    public void onConnectionFailed(String deviceId, String reason) {
        Log.e(TAG, "wireless connection failed: " + deviceId + " — " + reason);
        wirelessDeviceId = null;
        wirelessDeviceName = null;
        wirelessReady = false;
        notifyDeviceStateChanged();
    }

    @Override
    public void onDeviceDisconnected(String deviceId, String reason) {
        Log.i(TAG, "wireless disconnected: " + deviceId + " — " + reason);

        // Restore A2DP/AVRCP for this device
        if (btProfileGate != null) {
            btProfileGate.restore(deviceId);
        }

        wirelessDeviceId = null;
        wirelessDeviceName = null;
        wirelessReady = false;

        if (currentSessionId > 0) {
            if (engineProxy != null) {
                try { engineProxy.stopSession(currentSessionId); }
                catch (RemoteException e) { Log.w(TAG, "stopSession failed", e); }
            }
            cleanupSession();
        }
        notifyDeviceStateChanged();
    }

    // ===== Wireless management =====

    private void startWirelessListeningIfReady() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        if (bt == null || !bt.isEnabled()) return;

        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        if (wm.getWifiApState() != WifiManager.WIFI_AP_STATE_ENABLED) return;

        BluetoothWirelessManager.HotspotConfig config = readHotspotConfig(wm);
        if (config == null) return;

        stopWirelessListening();
        wirelessManager = new BluetoothWirelessManager(this, config);
        wirelessManager.startListening();
        Log.i(TAG, "wireless listening started");
    }

    private void stopWirelessListening() {
        if (wirelessManager != null) {
            wirelessManager.stop();
            wirelessManager = null;
            Log.i(TAG, "wireless listening stopped");
        }
    }

    @SuppressWarnings("deprecation")
    private BluetoothWirelessManager.HotspotConfig readHotspotConfig(WifiManager wm) {
        WifiConfiguration apConfig = wm.getWifiApConfiguration();
        if (apConfig == null) {
            Log.e(TAG, "getWifiApConfiguration() returned null");
            return null;
        }

        String ssid = apConfig.SSID != null ? apConfig.SSID : "";
        String password = apConfig.preSharedKey != null ? apConfig.preSharedKey : "";
        String[] netInfo = getApNetworkInfo();

        Log.i(TAG, "hotspot: ssid=" + ssid + " ip=" + netInfo[0]
                + " bssid=" + netInfo[1] + " port=" + TCP_PORT);
        return new BluetoothWirelessManager.HotspotConfig(
                ssid, password, netInfo[1], netInfo[0], TCP_PORT);
    }

    private String[] getApNetworkInfo() {
        for (int attempt = 0; attempt < 5; attempt++) {
            try {
                for (NetworkInterface iface :
                        Collections.list(NetworkInterface.getNetworkInterfaces())) {
                    if (iface.isLoopback() || !iface.isUp()) continue;
                    String name = iface.getName();
                    if (name.startsWith("wlan") || name.startsWith("ap")
                            || name.startsWith("swlan")) {
                        for (InetAddress addr :
                                Collections.list(iface.getInetAddresses())) {
                            if (addr instanceof Inet4Address
                                    && !addr.isLoopbackAddress()) {
                                String ip = addr.getHostAddress();
                                byte[] mac = iface.getHardwareAddress();
                                String bssid = "02:00:00:00:00:00";
                                if (mac != null) {
                                    bssid = String.format(
                                        "%02x:%02x:%02x:%02x:%02x:%02x",
                                        mac[0], mac[1], mac[2],
                                        mac[3], mac[4], mac[5]);
                                }
                                return new String[]{ip, bssid};
                            }
                        }
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "getApNetworkInfo failed: " + e.getMessage());
            }
            try { Thread.sleep(500); } catch (InterruptedException ignored) { break; }
        }
        return new String[]{"192.168.43.1", "02:00:00:00:00:00"};
    }

    // ===== Session cleanup =====

    private synchronized void cleanupSession() {
        if (currentSessionId <= 0) return;
        currentSessionId = -1;
        wirelessDeviceId = null;
        wirelessDeviceName = null;
        wirelessReady = false;
        if (videoDecoder != null) {
            videoDecoder.release();
            videoDecoder = null;
        }
        audioPlayer.release();
        sendBroadcast(new Intent(ACTION_SESSION_ENDED));
        notifyDeviceStateChanged();
        Log.i(TAG, "session cleaned up");
    }

    // ===== Public API for Activity =====

    public UsbDevice getAvailableDevice() {
        return availableDevice;
    }

    public String getWirelessDeviceName() {
        return wirelessDeviceName;
    }

    public boolean isWirelessConnecting() {
        return wirelessDeviceId != null && !wirelessReady;
    }

    public boolean isWirelessReady() {
        return wirelessReady;
    }

    public boolean hasActiveSession() {
        return currentSessionId > 0;
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
        android.app.TaskStackBuilder.create(this)
                .addNextIntent(new Intent(this, DeviceListActivity.class))
                .addNextIntent(new Intent(this, AaDisplayActivity.class))
                .startActivities();
    }

    public int getVideoWidth() { return videoWidth; }
    public int getVideoHeight() { return videoHeight; }

    public void sendTouchEvent(int x, int y, int action) {
        if (engineProxy == null || currentSessionId <= 0) return;
        try {
            engineProxy.sendTouchEvent(currentSessionId, x, y, action);
        } catch (RemoteException e) {
            Log.w(TAG, "sendTouchEvent failed", e);
        }
    }

    public void setSurface(Surface surface) {
        pendingSurface = surface;
        if (surface != null) {
            if (videoDecoder == null) {
                videoDecoder = new VideoDecoder();
            }
            videoDecoder.setSurface(surface);
            Log.i(TAG, "setSurface: decoder ready");
        } else {
            if (videoDecoder != null) {
                videoDecoder.release();
                videoDecoder = null;
            }
            Log.i(TAG, "setSurface: decoder released");
        }
    }

    // ===== Engine connection with retry =====

    private void connectToEngine() {
        IBinder binder = android.os.ServiceManager.getService("aa-engine");
        if (binder == null) {
            connectRetryCount++;
            if (connectRetryCount <= ENGINE_CONNECT_MAX_RETRIES) {
                Log.w(TAG, "aa-engine not found, retry " + connectRetryCount +
                        "/" + ENGINE_CONNECT_MAX_RETRIES);
                handler.postDelayed(this::connectToEngine, ENGINE_CONNECT_RETRY_MS);
            } else {
                Log.e(TAG, "aa-engine not found after max retries");
            }
            return;
        }
        engineProxy = IAAEngine.Stub.asInterface(binder);
        try {
            engineProxy.registerCallback(engineCallback);
            connectRetryCount = 0;
            Log.i(TAG, "connected to aa-engine");
        } catch (RemoteException e) {
            Log.e(TAG, "registerCallback failed", e);
            engineProxy = null;
        }
    }
}
