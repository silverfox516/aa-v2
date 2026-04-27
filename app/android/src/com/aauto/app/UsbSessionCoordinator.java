package com.aauto.app;

import android.content.Context;
import android.hardware.usb.UsbDevice;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.util.Log;

import com.aauto.engine.IAAEngine;

import java.util.function.Supplier;

/**
 * Owns the USB transport's full session lifecycle: USB monitoring,
 * AOA accessory negotiation, engine session start, and stop on detach.
 *
 * Mirror of {@link WirelessSessionCoordinator}. Together they keep the
 * host service ({@link AaService}) out of transport-specific logic —
 * the host only sees generic {@link SessionLifecycleListener} events.
 */
class UsbSessionCoordinator implements UsbMonitor.Listener {
    private static final String TAG = "AA.UsbCoord";
    static final String TRANSPORT_LABEL = "USB";

    private final UsbMonitor usbMonitor;
    private final Supplier<IAAEngine> engineProvider;
    private final SessionManager sessionManager;
    private final SessionLifecycleListener listener;

    UsbSessionCoordinator(Context context,
                          Supplier<IAAEngine> engineProvider,
                          SessionManager sessionManager,
                          SessionLifecycleListener listener) {
        this.usbMonitor = new UsbMonitor(context, this);
        this.engineProvider = engineProvider;
        this.sessionManager = sessionManager;
        this.listener = listener;
    }

    void start() {
        usbMonitor.start();
    }

    void shutdown() {
        usbMonitor.stop();
    }

    // ===== UsbMonitor.Listener =====

    @Override
    public void onDeviceAvailable(UsbDevice device) {
        Log.i(TAG, "USB device available: " + device.getProductName());
        // Auto-connect: open USB and let onDeviceReady fire.
        if (!usbMonitor.connectPendingDevice()) {
            Log.e(TAG, "failed to open USB device");
            listener.onTransportStateChanged();
        }
    }

    @Override
    public void onDeviceReady(int fd, int epIn, int epOut) {
        IAAEngine engine = engineProvider.get();
        if (engine == null) {
            Log.e(TAG, "engine not connected, cannot start USB session");
            return;
        }
        try {
            ParcelFileDescriptor pfd = ParcelFileDescriptor.adoptFd(fd);
            int sid = engine.startSession(pfd, epIn, epOut);
            if (sid > 0) {
                sessionManager.createSession(sid, TRANSPORT_LABEL);
                Log.i(TAG, "USB session started: id=" + sid);
            }
        } catch (RemoteException e) {
            Log.e(TAG, "failed to start USB session", e);
        }
        listener.onTransportStateChanged();
    }

    @Override
    public void onDeviceRemoved() {
        Log.i(TAG, "USB device removed");
        // Find sessions belonging to this transport and request cleanup.
        // SessionManager owns the transport->session mapping.
        for (SessionManager.SessionEntry e :
                sessionManager.getSessionsByTransport(TRANSPORT_LABEL)) {
            listener.onSessionShouldStop(e.sessionId);
        }
        listener.onTransportStateChanged();
    }
}
