package com.aauto.app;

/**
 * Thread-safe wireless device state cache.
 * Written from BT RFCOMM thread, read from main thread (UI).
 */
class WirelessStateTracker {
    private volatile String deviceId;
    private volatile String deviceName;
    private volatile boolean ready;

    synchronized void onConnecting(String nextDeviceId, String nextDeviceName) {
        deviceId = nextDeviceId;
        deviceName = nextDeviceName;
        ready = false;
    }

    synchronized void onReady(String nextDeviceId, String nextDeviceName) {
        deviceId = nextDeviceId;
        deviceName = nextDeviceName;
        ready = true;
    }

    synchronized void clear() {
        deviceId = null;
        deviceName = null;
        ready = false;
    }

    String getDeviceId() {
        return deviceId;
    }

    String getDeviceName() {
        return deviceName;
    }

    boolean isReady() {
        return ready;
    }

    boolean isConnecting() {
        return deviceId != null && !ready;
    }
}
