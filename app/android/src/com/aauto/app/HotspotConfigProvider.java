package com.aauto.app;

import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.aauto.app.wireless.BluetoothWirelessManager;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/**
 * Reads WiFi AP configuration and network interface info.
 * Network info discovery runs on a background thread to avoid ANR.
 */
class HotspotConfigProvider {
    private static final String TAG = "AA.HotspotConfig";
    private static final int MAX_RETRIES = 5;
    private static final int RETRY_DELAY_MS = 500;

    interface Callback {
        void onConfigReady(BluetoothWirelessManager.HotspotConfig config);
    }

    @SuppressWarnings("deprecation")
    void readAsync(WifiManager wm, int tcpPort, Callback callback) {
        WifiConfiguration apConfig = wm.getWifiApConfiguration();
        if (apConfig == null) {
            Log.e(TAG, "getWifiApConfiguration() returned null");
            callback.onConfigReady(null);
            return;
        }

        String ssid = apConfig.SSID != null ? apConfig.SSID : "";
        String password = apConfig.preSharedKey != null ? apConfig.preSharedKey : "";

        // Discover AP network info on background thread to avoid ANR
        new Thread(() -> {
            String[] netInfo = getApNetworkInfo();
            Log.i(TAG, "hotspot: ssid=" + ssid + " ip=" + netInfo[0]
                    + " bssid=" + netInfo[1] + " port=" + tcpPort);
            BluetoothWirelessManager.HotspotConfig config =
                    new BluetoothWirelessManager.HotspotConfig(
                            ssid, password, netInfo[1], netInfo[0], tcpPort);
            new Handler(Looper.getMainLooper()).post(() ->
                    callback.onConfigReady(config));
        }, "HotspotConfig").start();
    }

    private String[] getApNetworkInfo() {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
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
            try {
                Thread.sleep(RETRY_DELAY_MS);
            } catch (InterruptedException ignored) {
                break;
            }
        }
        return new String[]{"192.168.43.1", "02:00:00:00:00:00"};
    }
}
