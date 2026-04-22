package com.aauto.app;

import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.graphics.Color;
import android.graphics.Typeface;
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

    // Consistent design tokens
    private static final int COLOR_BG       = 0xFF1A1A1A;
    private static final int COLOR_PANEL    = 0xFF252525;
    private static final int COLOR_ON       = 0xFF4CAF50;
    private static final int COLOR_OFF      = 0xFF616161;
    private static final int COLOR_TEXT     = 0xFFE0E0E0;
    private static final int COLOR_TEXT_SUB = 0xFF9E9E9E;
    private static final int TEXT_SIZE_BTN  = 16;
    private static final int TEXT_SIZE_ITEM = 16;
    private static final int TEXT_SIZE_SUB  = 13;
    private static final int PAD            = 24;
    private static final int BTN_PAD_V     = 20;
    private static final int BTN_PAD_H     = 32;
    private static final int ITEM_PAD_V    = 20;
    private static final int ITEM_PAD_H    = 24;

    private DeviceAdapter adapter;
    private AaService aaService;
    private Button btButton;
    private Button wifiApButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Root: horizontal split (left=list 3:1 right=buttons)
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.HORIZONTAL);
        root.setBackgroundColor(COLOR_BG);
        root.setPadding(PAD, PAD, PAD, PAD);

        // Left panel: device list
        ListView listView = new ListView(this);
        listView.setBackgroundColor(COLOR_PANEL);
        listView.setDividerHeight(1);
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
        buttonPanel.setGravity(Gravity.TOP);
        buttonPanel.setBackgroundColor(COLOR_PANEL);
        buttonPanel.setPadding(PAD, PAD, PAD, PAD);
        LinearLayout.LayoutParams panelLp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
        panelLp.leftMargin = PAD;

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
        wifiLp.topMargin = PAD;
        buttonPanel.addView(wifiApButton, wifiLp);

        root.addView(buttonPanel, panelLp);
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
        btn.setTextSize(TEXT_SIZE_BTN);
        btn.setTypeface(Typeface.DEFAULT_BOLD);
        btn.setPadding(BTN_PAD_H, BTN_PAD_V, BTN_PAD_H, BTN_PAD_V);
        btn.setAllCaps(false);
        styleButton(btn, false);
        return btn;
    }

    private void styleButton(Button btn, boolean on) {
        btn.setBackgroundColor(on ? COLOR_ON : COLOR_OFF);
        btn.setTextColor(Color.WHITE);
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
            LinearLayout row;
            if (convertView instanceof LinearLayout) {
                row = (LinearLayout) convertView;
            } else {
                row = new LinearLayout(parent.getContext());
                row.setOrientation(LinearLayout.VERTICAL);
                row.setPadding(ITEM_PAD_H, ITEM_PAD_V, ITEM_PAD_H, ITEM_PAD_V);
            }
            row.removeAllViews();

            DeviceEntry entry = entries.get(position);
            String label = entry.type == DeviceEntry.USB ? "USB" : "Wireless";

            TextView nameView = new TextView(parent.getContext());
            nameView.setText(entry.name);
            nameView.setTextSize(TEXT_SIZE_ITEM);
            nameView.setTextColor(COLOR_TEXT);
            nameView.setTypeface(Typeface.DEFAULT_BOLD);
            row.addView(nameView);

            String sub = label;
            if (!entry.status.isEmpty()) sub += "  -  " + entry.status;
            TextView subView = new TextView(parent.getContext());
            subView.setText(sub);
            subView.setTextSize(TEXT_SIZE_SUB);
            subView.setTextColor(COLOR_TEXT_SUB);
            row.addView(subView);

            return row;
        }
    }
}
