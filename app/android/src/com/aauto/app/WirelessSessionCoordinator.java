package com.aauto.app;

import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.RemoteException;
import android.util.Log;

import com.aauto.app.wireless.BluetoothWirelessManager;
import com.aauto.app.wireless.BtProfileGate;
import com.aauto.engine.IAAEngine;

import java.util.function.Supplier;

/**
 * Owns the wireless transport's full session lifecycle: BT RFCOMM
 * handshake, hotspot config exchange, engine TCP session start, BT
 * profile gating, and stop on RFCOMM disconnect.
 *
 * Mirror of {@link UsbSessionCoordinator}. The host service is notified
 * only via the generic {@link SessionLifecycleListener} contract.
 */
class WirelessSessionCoordinator implements BluetoothWirelessManager.Listener {
    private static final String TAG = "AA.WirelessCoord";
    static final String TRANSPORT_LABEL = "Wireless";

    private final Context context;
    private final WirelessStateTracker stateTracker;
    private final HotspotConfigProvider hotspotConfigProvider;
    private final BtProfileGate btProfileGate;
    private final Supplier<IAAEngine> engineProvider;
    private final SessionManager sessionManager;
    private final SessionLifecycleListener listener;
    private final int tcpPort;

    private BluetoothWirelessManager wirelessManager;

    WirelessSessionCoordinator(Context context,
                               WirelessStateTracker stateTracker,
                               HotspotConfigProvider hotspotConfigProvider,
                               BtProfileGate btProfileGate,
                               Supplier<IAAEngine> engineProvider,
                               SessionManager sessionManager,
                               SessionLifecycleListener listener,
                               int tcpPort) {
        this.context = context;
        this.stateTracker = stateTracker;
        this.hotspotConfigProvider = hotspotConfigProvider;
        this.btProfileGate = btProfileGate;
        this.engineProvider = engineProvider;
        this.sessionManager = sessionManager;
        this.listener = listener;
        this.tcpPort = tcpPort;
    }

    void startListeningIfReady() {
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

    // ===== BluetoothWirelessManager.Listener =====

    @Override
    public void onDeviceConnecting(String deviceId, String deviceName) {
        Log.i(TAG, "wireless connecting: " + deviceId + " (" + deviceName + ")");
        stateTracker.onConnecting(deviceId, deviceName);
        listener.onTransportStateChanged();
    }

    @Override
    public void onDeviceReady(String deviceId, String deviceName) {
        Log.i(TAG, "wireless ready: " + deviceId + " (" + deviceName + ")");
        stateTracker.onReady(deviceId, deviceName);
        if (btProfileGate != null) {
            btProfileGate.block(deviceId);
        }
        startEngineSession();
        listener.onTransportStateChanged();
    }

    @Override
    public void onConnectionFailed(String deviceId, String reason) {
        Log.e(TAG, "wireless connection failed: " + deviceId + " - " + reason);
        stateTracker.clear();
        listener.onTransportStateChanged();
    }

    @Override
    public void onDeviceDisconnected(String deviceId, String reason) {
        Log.i(TAG, "wireless disconnected: " + deviceId + " - " + reason);
        if (btProfileGate != null) {
            btProfileGate.restore(deviceId);
        }
        stateTracker.clear();
        // Stop any wireless sessions belonging to this transport. The
        // host's SessionManager owns the transport->session mapping.
        for (SessionManager.SessionEntry e :
                sessionManager.getSessionsByTransport(TRANSPORT_LABEL)) {
            listener.onSessionShouldStop(e.sessionId);
        }
        listener.onTransportStateChanged();
    }

    // ===== Internal =====

    private void startEngineSession() {
        IAAEngine engine = engineProvider.get();
        if (engine == null) {
            Log.e(TAG, "engine not connected, cannot start wireless session");
            return;
        }
        try {
            int sid = engine.startTcpSession(tcpPort);
            if (sid > 0) {
                sessionManager.createSession(sid, TRANSPORT_LABEL);
                Log.i(TAG, "wireless session started: id=" + sid);
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start wireless session", e);
        }
    }
}
