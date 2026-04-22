package com.aauto.app;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.hardware.usb.UsbDevice;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/**
 * Home screen showing available devices for Android Auto.
 * Devices appear when detected (USB AOA confirmed / wireless BT connected).
 * Tap a device to start the AA session.
 */
public class DeviceListActivity extends Activity
        implements AaService.DeviceStateListener {
    private static final String TAG = "AA.DeviceList";

    private DeviceAdapter adapter;
    private AaService aaService;
    private TextView emptyText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 32, 32, 32);

        emptyText = new TextView(this);
        emptyText.setTextSize(16);
        emptyText.setVisibility(View.GONE);
        layout.addView(emptyText);

        ListView listView = new ListView(this);
        adapter = new DeviceAdapter();
        listView.setAdapter(adapter);
        listView.setOnItemClickListener((parent, view, position, id) -> {
            if (aaService != null) {
                Log.i(TAG, "device selected, connecting");
                aaService.connectDevice();
            }
        });
        layout.addView(listView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        setContentView(layout);

        Intent serviceIntent = new Intent(this, AaService.class);
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE);

        Log.i(TAG, "DeviceListActivity created");
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshDeviceList();
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
        refreshDeviceList();
    }

    private void refreshDeviceList() {
        List<UsbDevice> devices = new ArrayList<>();
        if (aaService != null) {
            UsbDevice device = aaService.getAvailableDevice();
            if (device != null && !aaService.hasActiveSession()) {
                devices.add(device);
            }
        }
        adapter.setDevices(devices);
        emptyText.setVisibility(devices.isEmpty() ? View.VISIBLE : View.GONE);
    }

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            AaService.LocalBinder localBinder = (AaService.LocalBinder) binder;
            aaService = localBinder.getService();
            aaService.setDeviceStateListener(DeviceListActivity.this);
            Log.i(TAG, "AaService bound");
            refreshDeviceList();
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            aaService = null;
        }
    };

    private static class DeviceAdapter extends BaseAdapter {
        private final List<UsbDevice> devices = new ArrayList<>();

        void setDevices(List<UsbDevice> list) {
            devices.clear();
            devices.addAll(list);
            notifyDataSetChanged();
        }

        @Override public int getCount() { return devices.size(); }
        @Override public Object getItem(int pos) { return devices.get(pos); }
        @Override public long getItemId(int pos) { return pos; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            TextView tv = (convertView instanceof TextView)
                    ? (TextView) convertView
                    : new TextView(parent.getContext());
            UsbDevice device = devices.get(position);
            String name = device.getProductName() != null
                    ? device.getProductName() : "Unknown Device";
            tv.setText(name + "  [USB]");
            tv.setTextSize(18);
            tv.setPadding(16, 32, 16, 32);
            return tv;
        }
    }
}
