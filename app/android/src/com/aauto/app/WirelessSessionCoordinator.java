package com.aauto.app;

import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.util.Log;

import com.aauto.app.wireless.BluetoothWirelessManager;
import com.aauto.app.wireless.BtProfileGate;

class WirelessSessionCoordinator implements BluetoothWirelessManager.Listener {
    private static final String TAG = "AA.WirelessCoord";

    interface Callback {
        void onWirelessStateChanged();
        void onWirelessReadyToStartSession(String deviceId, String deviceName);
        void onWirelessDisconnected(String deviceId, String reason);
    }

    private final Context context;
    private final WirelessStateTracker stateTracker;
    private final HotspotConfigProvider hotspotConfigProvider;
    private final BtProfileGate btProfileGate;
    private final Callback callback;

    private BluetoothWirelessManager wirelessManager;

    WirelessSessionCoordinator(Context context,
                               WirelessStateTracker stateTracker,
                               HotspotConfigProvider hotspotConfigProvider,
                               BtProfileGate btProfileGate,
                               Callback callback) {
        this.context = context;
        this.stateTracker = stateTracker;
        this.hotspotConfigProvider = hotspotConfigProvider;
        this.btProfileGate = btProfileGate;
        this.callback = callback;
    }

    void startListeningIfReady(int tcpPort) {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        if (bt == null || !bt.isEnabled()) return;

        WifiManager wm = (WifiManager) context.getSystemService(Context.WIFI_SERVICE);
        if (wm == null || wm.getWifiApState() != WifiManager.WIFI_AP_STATE_ENABLED) return;

        hotspotConfigProvider.readAsync(wm, tcpPort, config -> {
            if (config == null) return;
            stopListening();
            wirelessManager = new BluetoothWirelessManager(this, config);
            wirelessManager.startListening();
            Log.i(TAG, "wireless listening started");
        });
    }

    void stopListening() {
        if (wirelessManager != null) {
            wirelessManager.stop();
            wirelessManager = null;
            Log.i(TAG, "wireless listening stopped");
        }
    }

    void shutdown() {
        stopListening();
        if (btProfileGate != null) {
            btProfileGate.close();
        }
        stateTracker.clear();
    }

    @Override
    public void onDeviceConnecting(String deviceId, String deviceName) {
        Log.i(TAG, "wireless connecting: " + deviceId + " (" + deviceName + ")");
        stateTracker.onConnecting(deviceId, deviceName);
        callback.onWirelessStateChanged();
    }

    @Override
    public void onDeviceReady(String deviceId, String deviceName) {
        Log.i(TAG, "wireless ready: " + deviceId + " (" + deviceName + ")");
        stateTracker.onReady(deviceId, deviceName);
        if (btProfileGate != null) {
            btProfileGate.block(deviceId);
        }
        callback.onWirelessStateChanged();
        callback.onWirelessReadyToStartSession(deviceId, deviceName);
    }

    @Override
    public void onConnectionFailed(String deviceId, String reason) {
        Log.e(TAG, "wireless connection failed: " + deviceId + " - " + reason);
        stateTracker.clear();
        callback.onWirelessStateChanged();
    }

    @Override
    public void onDeviceDisconnected(String deviceId, String reason) {
        Log.i(TAG, "wireless disconnected: " + deviceId + " - " + reason);
        if (btProfileGate != null) {
            btProfileGate.restore(deviceId);
        }
        stateTracker.clear();
        callback.onWirelessDisconnected(deviceId, reason);
        callback.onWirelessStateChanged();
    }
}
