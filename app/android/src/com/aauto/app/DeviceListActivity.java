package com.aauto.app;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Color;
import android.hardware.usb.UsbDevice;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/**
 * Home screen showing available devices for Android Auto.
 * Left panel: device list. Right panel: BT/WiFi AP toggle buttons.
 */
public class DeviceListActivity extends Activity
        implements AaService.DeviceStateListener {
    private static final String TAG = "AA.DeviceList";

    private static final int COLOR_ON  = 0xFF4CAF50;  // green
    private static final int COLOR_OFF = 0xFF9E9E9E;  // gray
    private static final int COLOR_TEXT_ON  = Color.WHITE;
    private static final int COLOR_TEXT_OFF = Color.WHITE;

    private DeviceAdapter adapter;
    private AaService aaService;
    private Button btButton;
    private Button wifiApButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Root: horizontal split (left=list, right=buttons)
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setPadding(32, 32, 32, 32);

        // Left panel: device list
        ListView listView = new ListView(this);
        adapter = new DeviceAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) -> {
            DeviceEntry entry = adapter.getEntry(position);
            if (entry != null && aaService != null && entry.type == DeviceEntry.USB) {
                Log.i(TAG, "USB device selected, connecting");
                aaService.connectDevice();
            }
        });
        root.addView(listView, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 3));

        // Right panel: toggle buttons
        LinearLayout buttonPanel = new LinearLayout(this);
        buttonPanel.setOrientation(LinearLayout.VERTICAL);
        buttonPanel.setGravity(Gravity.CENTER);
        buttonPanel.setPadding(32, 0, 0, 0);

        btButton = createToggleButton("BT OFF");
        btButton.setOnClickListener(v -> toggleBluetooth());
        buttonPanel.addView(btButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        wifiApButton = createToggleButton("WiFi AP OFF");
        wifiApButton.setOnClickListener(v -> toggleWifiAp());
        LinearLayout.LayoutParams wifiLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        wifiLp.topMargin = 24;
        buttonPanel.addView(wifiApButton, wifiLp);

        root.addView(buttonPanel, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1));

        setContentView(root);

        Intent serviceIntent = new Intent(this, AaService.class);
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE);

        Log.i(TAG, "DeviceListActivity created");
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshAll();
    }

    @Override
    protected void onDestroy() {
        if (aaService != null) {
            aaService.setDeviceStateListener(null);
        }
        unbindService(serviceConnection);
        super.onDestroy();
    }

    // ===== AaService.DeviceStateListener =====

    @Override
    public void onDeviceStateChanged() {
        refreshAll();
    }

    // ===== UI refresh =====

    private void refreshAll() {
        refreshToggleButtons();
        refreshDeviceList();
    }

    private void refreshToggleButtons() {
        if (aaService == null) return;

        boolean btOn = aaService.isBluetoothEnabled();
        btButton.setText(btOn ? "BT ON" : "BT OFF");
        styleButton(btButton, btOn);

        boolean apOn = aaService.isWifiApEnabled();
        wifiApButton.setText(apOn ? "WiFi AP ON" : "WiFi AP OFF");
        styleButton(wifiApButton, apOn);
    }

    private void refreshDeviceList() {
        List<DeviceEntry> entries = new ArrayList<>();
        if (aaService != null && !aaService.hasActiveSession()) {
            UsbDevice usb = aaService.getAvailableDevice();
            if (usb != null) {
                String name = usb.getProductName() != null
                        ? usb.getProductName() : "Unknown Device";
                entries.add(new DeviceEntry(DeviceEntry.USB, name, ""));
            }

            String wirelessName = aaService.getWirelessDeviceName();
            if (wirelessName != null) {
                String status = aaService.isWirelessConnecting()
                        ? "Connecting..." : "Ready";
                entries.add(new DeviceEntry(DeviceEntry.WIRELESS,
                        wirelessName, status));
            }
        }
        adapter.setEntries(entries);
    }

    // ===== Toggle actions =====

    private void toggleBluetooth() {
        BluetoothAdapter bt = BluetoothAdapter.getDefaultAdapter();
        if (bt == null) {
            Log.w(TAG, "no Bluetooth adapter");
            return;
        }
        try {
            if (bt.isEnabled()) {
                Log.i(TAG, "disabling Bluetooth");
                bt.disable();
            } else {
                Log.i(TAG, "enabling Bluetooth");
                bt.enable();
            }
        } catch (Exception e) {
            Log.e(TAG, "toggleBluetooth failed", e);
        }
    }

    @SuppressWarnings("deprecation")
    private void toggleWifiAp() {
        WifiManager wm = (WifiManager) getSystemService(Context.WIFI_SERVICE);
        int state = wm.getWifiApState();
        boolean wantEnabled = (state != WifiManager.WIFI_AP_STATE_ENABLED
                && state != WifiManager.WIFI_AP_STATE_ENABLING);
        Log.i(TAG, "toggling WiFi AP -> " + (wantEnabled ? "ENABLE" : "DISABLE"));

        if (wantEnabled) {
            if (!tryReflect(wm, "startSoftAp",
                    new Class[]{android.net.wifi.WifiConfiguration.class},
                    new Object[]{null})) {
                tryReflect(wm, "setWifiApEnabled",
                        new Class[]{android.net.wifi.WifiConfiguration.class, boolean.class},
                        new Object[]{null, true});
            }
        } else {
            if (!tryReflect(wm, "stopSoftAp", new Class[0], new Object[0])) {
                tryReflect(wm, "setWifiApEnabled",
                        new Class[]{android.net.wifi.WifiConfiguration.class, boolean.class},
                        new Object[]{null, false});
            }
        }
    }

    private boolean tryReflect(Object target, String method,
                               Class<?>[] paramTypes, Object[] args) {
        try {
            java.lang.reflect.Method m = target.getClass()
                    .getMethod(method, paramTypes);
            Object result = m.invoke(target, args);
            Log.i(TAG, method + " -> " + result);
            return true;
        } catch (NoSuchMethodException e) {
            Log.w(TAG, method + " not available");
            return false;
        } catch (Exception e) {
            Throwable cause = e.getCause() != null ? e.getCause() : e;
            Log.e(TAG, method + " failed", cause);
            return false;
        }
    }

    // ===== Button styling =====

    private Button createToggleButton(String text) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setTextSize(16);
        btn.setPadding(32, 24, 32, 24);
        styleButton(btn, false);
        return btn;
    }

    private void styleButton(Button btn, boolean on) {
        btn.setBackgroundColor(on ? COLOR_ON : COLOR_OFF);
        btn.setTextColor(on ? COLOR_TEXT_ON : COLOR_TEXT_OFF);
    }

    // ===== Service connection =====

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            AaService.LocalBinder localBinder = (AaService.LocalBinder) binder;
            aaService = localBinder.getService();
            aaService.setDeviceStateListener(DeviceListActivity.this);
            Log.i(TAG, "AaService bound");
            refreshAll();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            aaService = null;
        }
    };

    // ===== Device entry =====

    private static class DeviceEntry {
        static final int USB = 0;
        static final int WIRELESS = 1;

        final int type;
        final String name;
        final String status;

        DeviceEntry(int type, String name, String status) {
            this.type = type;
            this.name = name;
            this.status = status;
        }
    }

    // ===== Adapter =====

    private static class DeviceAdapter extends BaseAdapter {
        private final List<DeviceEntry> entries = new ArrayList<>();

        void setEntries(List<DeviceEntry> list) {
            entries.clear();
            entries.addAll(list);
            notifyDataSetChanged();
        }

        DeviceEntry getEntry(int position) {
            return position < entries.size() ? entries.get(position) : null;
        }

        @Override public int getCount() { return entries.size(); }
        @Override public Object getItem(int pos) { return entries.get(pos); }
        @Override public long getItemId(int pos) { return pos; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            TextView tv = (convertView instanceof TextView)
                    ? (TextView) convertView
                    : new TextView(parent.getContext());
            DeviceEntry entry = entries.get(position);
            String label = entry.type == DeviceEntry.USB ? "[USB]" : "[Wireless]";
            String text = entry.name + "  " + label;
            if (!entry.status.isEmpty()) {
                text += "\n" + entry.status;
            }
            tv.setText(text);
            tv.setTextSize(18);
            tv.setPadding(16, 32, 16, 32);
            return tv;
        }
    }
}
