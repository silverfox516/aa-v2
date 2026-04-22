package com.aauto.app.wireless;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.lang.reflect.Method;
import java.util.HashSet;
import java.util.Set;

/**
 * Disables BT media profiles (A2DP_SINK, AVRCP_CONTROLLER) for a device
 * during a wireless Android Auto session.
 *
 * Problem: when a phone is wirelessly connected via AAW, audio flows over
 * AAP (WiFi/TCP). But BT pairing remains active, and AOSP's
 * BluetoothMediaBrowserService reacts to AVRCP "playing" notifications by
 * sending PASS THROUGH PAUSE — interpreting "remote is playing" + "local
 * audio focus held by another app" as a pause request. Music stops 3-4
 * seconds after starting.
 *
 * Fix: disconnect A2DP_SINK and set its priority to OFF while the wireless
 * session is alive. When A2DP_SINK is gone, BluetoothMediaBrowserService
 * stops tracking the device's AVRCP state.
 */
public class BtProfileGate {
    private static final String TAG = "AA.BtProfileGate";

    // Hidden BluetoothProfile constants (Android 10)
    private static final int PROFILE_A2DP_SINK = 11;
    private static final int PROFILE_AVRCP_CONTROLLER = 12;
    private static final int PRIORITY_OFF = 0;
    private static final int PRIORITY_ON = 100;

    private static final String ACTION_A2DP_SINK_CONNECTION_STATE_CHANGED =
            "android.bluetooth.a2dp-sink.profile.action.CONNECTION_STATE_CHANGED";

    private final Context context;
    private final BluetoothAdapter adapter;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private BluetoothProfile a2dpSink;
    private BluetoothProfile avrcpController;
    private boolean receiverRegistered;

    private final Set<String> blocked = new HashSet<>();

    public BtProfileGate(Context context) {
        this.context = context.getApplicationContext();
        adapter = BluetoothAdapter.getDefaultAdapter();
        if (adapter == null) {
            Log.w(TAG, "BluetoothAdapter unavailable");
            return;
        }
        adapter.getProfileProxy(this.context, a2dpListener, PROFILE_A2DP_SINK);
        adapter.getProfileProxy(this.context, avrcpListener, PROFILE_AVRCP_CONTROLLER);

        try {
            this.context.registerReceiver(connectionStateReceiver,
                    new IntentFilter(ACTION_A2DP_SINK_CONNECTION_STATE_CHANGED));
            receiverRegistered = true;
        } catch (Exception e) {
            Log.w(TAG, "registerReceiver failed: " + e.getMessage());
        }
    }

    /** Block media profiles for the given device. */
    public synchronized void block(String btAddress) {
        if (adapter == null || btAddress == null) return;
        if (!blocked.add(btAddress)) return;

        BluetoothDevice device = getDevice(btAddress);
        if (device == null) {
            blocked.remove(btAddress);
            return;
        }

        Log.i(TAG, "blocking media profiles for " + btAddress);
        applyBlock(device);
    }

    /** Restore media profiles for the given device. */
    public synchronized void restore(String btAddress) {
        if (adapter == null || btAddress == null) return;
        if (!blocked.remove(btAddress)) return;

        BluetoothDevice device = getDevice(btAddress);
        if (device == null) return;

        Log.i(TAG, "restoring media profiles for " + btAddress);
        setPriority(a2dpSink, device, PRIORITY_ON, "A2DP_SINK");
    }

    /** Restore all blocked devices and release proxies. */
    public synchronized void close() {
        for (String addr : new HashSet<>(blocked)) {
            BluetoothDevice device = getDevice(addr);
            if (device != null) {
                setPriority(a2dpSink, device, PRIORITY_ON, "A2DP_SINK");
            }
        }
        blocked.clear();

        if (receiverRegistered) {
            try { context.unregisterReceiver(connectionStateReceiver); }
            catch (Exception ignored) {}
            receiverRegistered = false;
        }

        if (adapter != null) {
            if (a2dpSink != null) adapter.closeProfileProxy(PROFILE_A2DP_SINK, a2dpSink);
            if (avrcpController != null) adapter.closeProfileProxy(PROFILE_AVRCP_CONTROLLER, avrcpController);
        }
        a2dpSink = null;
        avrcpController = null;
    }

    // ===== Internal =====

    private void applyBlock(BluetoothDevice device) {
        setPriority(a2dpSink, device, PRIORITY_OFF, "A2DP_SINK");
        disconnect(a2dpSink, device, "A2DP_SINK");
        disconnect(avrcpController, device, "AVRCP_CONTROLLER");
    }

    private BluetoothDevice getDevice(String addr) {
        try {
            return adapter.getRemoteDevice(addr);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "invalid BT address: " + addr);
            return null;
        }
    }

    // ===== Broadcast: re-block on A2DP_SINK reconnect =====

    private final BroadcastReceiver connectionStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            int state = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
            BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            if (device == null) return;
            String addr = device.getAddress();

            synchronized (BtProfileGate.this) {
                if (!blocked.contains(addr)) return;
            }

            if (state == BluetoothProfile.STATE_CONNECTED
                    || state == BluetoothProfile.STATE_CONNECTING) {
                mainHandler.post(() -> {
                    synchronized (BtProfileGate.this) {
                        if (!blocked.contains(addr)) return;
                        Log.i(TAG, "re-blocking reconnected device: " + addr);
                        applyBlock(device);
                    }
                });
            }
        }
    };

    // ===== Profile proxy listeners =====

    private final BluetoothProfile.ServiceListener a2dpListener =
            new BluetoothProfile.ServiceListener() {
        @Override
        public void onServiceConnected(int profile, BluetoothProfile proxy) {
            Log.i(TAG, "A2DP_SINK proxy connected");
            synchronized (BtProfileGate.this) {
                a2dpSink = proxy;
                for (String addr : blocked) {
                    BluetoothDevice d = getDevice(addr);
                    if (d != null) applyBlock(d);
                }
            }
        }
        @Override
        public void onServiceDisconnected(int profile) {
            synchronized (BtProfileGate.this) { a2dpSink = null; }
        }
    };

    private final BluetoothProfile.ServiceListener avrcpListener =
            new BluetoothProfile.ServiceListener() {
        @Override
        public void onServiceConnected(int profile, BluetoothProfile proxy) {
            Log.i(TAG, "AVRCP_CONTROLLER proxy connected");
            synchronized (BtProfileGate.this) {
                avrcpController = proxy;
                for (String addr : blocked) {
                    BluetoothDevice d = getDevice(addr);
                    if (d != null) disconnect(proxy, d, "AVRCP_CONTROLLER");
                }
            }
        }
        @Override
        public void onServiceDisconnected(int profile) {
            synchronized (BtProfileGate.this) { avrcpController = null; }
        }
    };

    // ===== Reflection helpers =====

    private static void setPriority(BluetoothProfile proxy, BluetoothDevice device,
                                    int priority, String name) {
        if (proxy == null) return;
        try {
            Method m = proxy.getClass().getMethod("setPriority",
                    BluetoothDevice.class, int.class);
            Object result = m.invoke(proxy, device, priority);
            Log.i(TAG, name + ".setPriority(" + device.getAddress()
                    + "," + priority + ") -> " + result);
            return;
        } catch (NoSuchMethodException ignored) {}
        catch (Exception e) {
            Log.w(TAG, name + ".setPriority failed: " + e.getMessage());
            return;
        }
        try {
            Method m = proxy.getClass().getMethod("setConnectionPolicy",
                    BluetoothDevice.class, int.class);
            Object result = m.invoke(proxy, device, priority);
            Log.i(TAG, name + ".setConnectionPolicy(" + device.getAddress()
                    + "," + priority + ") -> " + result);
        } catch (NoSuchMethodException ignored) {}
        catch (Exception e) {
            Log.w(TAG, name + ".setConnectionPolicy failed: " + e.getMessage());
        }
    }

    private static void disconnect(BluetoothProfile proxy, BluetoothDevice device,
                                   String name) {
        if (proxy == null) return;
        try {
            Method m = proxy.getClass().getMethod("disconnect", BluetoothDevice.class);
            Object result = m.invoke(proxy, device);
            Log.i(TAG, name + ".disconnect(" + device.getAddress() + ") -> " + result);
        } catch (NoSuchMethodException ignored) {}
        catch (Exception e) {
            Log.w(TAG, name + ".disconnect failed: " + e.getMessage());
        }
    }
}
