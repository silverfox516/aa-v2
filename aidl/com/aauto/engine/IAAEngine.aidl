package com.aauto.engine;

import com.aauto.engine.IAAEngineCallback;
import android.os.ParcelFileDescriptor;

/**
 * Binder interface: App -> aa-engine daemon.
 */
interface IAAEngine {
    /**
     * Start a new AAP session with the given USB device fd and endpoints.
     * @param usbFd file descriptor from UsbDeviceConnection.getFileDescriptor()
     * @param epIn  bulk IN endpoint address (e.g., 0x81)
     * @param epOut bulk OUT endpoint address (e.g., 0x01)
     * @return session ID (>0), or -1 on failure.
     */
    int startSession(in ParcelFileDescriptor usbFd, int epIn, int epOut);

    void stopSession(int sessionId);
    void stopAll();
    void registerCallback(IAAEngineCallback callback);
}
