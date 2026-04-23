package com.aauto.app;

class WirelessStateTracker {
    private String deviceId;
    private String deviceName;
    private boolean ready;

    void onConnecting(String nextDeviceId, String nextDeviceName) {
        deviceId = nextDeviceId;
        deviceName = nextDeviceName;
        ready = false;
    }

    void onReady(String nextDeviceId, String nextDeviceName) {
        deviceId = nextDeviceId;
        deviceName = nextDeviceName;
        ready = true;
    }

    void clear() {
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
