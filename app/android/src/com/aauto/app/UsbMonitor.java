package com.aauto.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/**
 * Monitors USB device attach/detach and handles Android Open Accessory (AOA)
 * protocol to switch a connected phone into accessory mode.
 *
 * HU is USB host. Phone is USB device. Flow:
 *   1. Phone plugs in -> USB_DEVICE_ATTACHED
 *   2. Send AOA identification strings via control transfer
 *   3. Phone re-enumerates with Google AOA VID/PID
 *   4. Open AOA device -> claim bulk interface -> get fd + endpoints
 *   5. Pass fd/ep_in/ep_out to listener
 */
public class UsbMonitor {
    private static final String TAG = "AA.UsbMonitor";
    // AOA protocol
    private static final int AOA_GET_PROTOCOL = 51;
    private static final int AOA_SEND_STRING  = 52;
    private static final int AOA_START        = 53;

    private static final int AOA_STRING_MANUFACTURER = 0;
    private static final int AOA_STRING_MODEL        = 1;
    private static final int AOA_STRING_DESCRIPTION  = 2;
    private static final int AOA_STRING_VERSION      = 3;
    private static final int AOA_STRING_URI          = 4;
    private static final int AOA_STRING_SERIAL       = 5;

    private static final String MANUFACTURER = "Android";
    private static final String MODEL        = "Android Auto";
    private static final String DESCRIPTION  = "Android Auto";
    private static final String VERSION      = "2.0.0";
    private static final String URI          = "https://developer.android.com/auto";
    private static final String SERIAL       = "HU0001";

    // Google AOA VID/PID
    private static final int GOOGLE_VID       = 0x18D1;
    private static final int AOA_PID_MIN      = 0x2D00;
    private static final int AOA_PID_MAX      = 0x2D05;

    public interface Listener {
        void onDeviceReady(int fd, int epIn, int epOut);
        void onDeviceDisconnected();
    }

    private final Context context;
    private final UsbManager usbManager;
    private final Listener listener;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private UsbDeviceConnection activeConnection;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            String action = intent.getAction();

            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (device != null) {
                    onDeviceAttached(device);
                }

            } else if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                Log.i(TAG, "USB device detached");
                closeConnection();
                listener.onDeviceDisconnected();
            }
        }
    };

    public UsbMonitor(Context context, Listener listener) {
        this.context = context;
        this.usbManager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        this.listener = listener;
    }

    public void start() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        context.registerReceiver(receiver, filter);
        Log.i(TAG, "USB monitor started");

        // Check already attached devices
        for (UsbDevice device : usbManager.getDeviceList().values()) {
            onDeviceAttached(device);
        }
    }

    public void stop() {
        try {
            context.unregisterReceiver(receiver);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "receiver not registered", e);
        }
        closeConnection();
        Log.i(TAG, "USB monitor stopped");
    }

    /** Called from Activity when USB_DEVICE_ATTACHED intent arrives directly. */
    public void onNewUsbDevice(UsbDevice device) {
        if (device != null) {
            onDeviceAttached(device);
        }
    }

    // ===== Internal =====

    private void onDeviceAttached(UsbDevice device) {
        int vid = device.getVendorId();
        int pid = device.getProductId();
        Log.i(TAG, "device attached: vid=0x" + Integer.toHexString(vid) +
                " pid=0x" + Integer.toHexString(pid));

        if (isAoaDevice(vid, pid)) {
            Log.i(TAG, "AOA device detected, opening");
            requestPermissionAndOpen(device);
        } else {
            Log.i(TAG, "non-AOA device, attempting AOA switch");
            requestPermissionAndSwitch(device);
        }
    }

    private void requestPermissionAndOpen(UsbDevice device) {
        // System app with platform certificate — permission is granted
        // automatically. Skip requestPermission() to avoid popup.
        openAoaDevice(device);
    }

    private void requestPermissionAndSwitch(UsbDevice device) {
        performAoaSwitch(device);
    }

    private void performAoaSwitch(UsbDevice device) {
        if (!usbManager.hasPermission(device)) {
            usbManager.grantPermission(device);
        }
        UsbDeviceConnection conn = usbManager.openDevice(device);
        if (conn == null) {
            Log.e(TAG, "failed to open device for AOA switch");
            return;
        }

        try {
            // Check AOA protocol version
            byte[] buf = new byte[2];
            int len = conn.controlTransfer(
                    UsbConstants.USB_DIR_IN | UsbConstants.USB_TYPE_VENDOR,
                    AOA_GET_PROTOCOL, 0, 0, buf, 2, 1000);
            if (len != 2) {
                Log.w(TAG, "device does not support AOA");
                return;
            }
            int version = buf[0] | (buf[1] << 8);
            Log.i(TAG, "AOA protocol version: " + version);
            if (version < 1) return;

            // Send identification strings
            sendAoaString(conn, AOA_STRING_MANUFACTURER, MANUFACTURER);
            sendAoaString(conn, AOA_STRING_MODEL, MODEL);
            sendAoaString(conn, AOA_STRING_DESCRIPTION, DESCRIPTION);
            sendAoaString(conn, AOA_STRING_VERSION, VERSION);
            sendAoaString(conn, AOA_STRING_URI, URI);
            sendAoaString(conn, AOA_STRING_SERIAL, SERIAL);

            // Start accessory mode
            int result = conn.controlTransfer(
                    UsbConstants.USB_DIR_OUT | UsbConstants.USB_TYPE_VENDOR,
                    AOA_START, 0, 0, null, 0, 1000);
            if (result < 0) {
                Log.e(TAG, "AOA start failed: " + result);
            } else {
                Log.i(TAG, "AOA start sent, waiting for re-enumeration");
            }
        } finally {
            conn.close();
        }
    }

    private void openAoaDevice(UsbDevice device) {
        if (!usbManager.hasPermission(device)) {
            // Platform-signed app can grant permission to itself
            usbManager.grantPermission(device);
        }
        UsbDeviceConnection conn = usbManager.openDevice(device);
        if (conn == null) {
            Log.e(TAG, "failed to open AOA device");
            return;
        }

        // Find bulk interface with IN + OUT endpoints
        int epIn = -1, epOut = -1;
        UsbInterface targetIntf = null;

        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface intf = device.getInterface(i);
            int bulkIn = -1, bulkOut = -1;

            for (int j = 0; j < intf.getEndpointCount(); j++) {
                UsbEndpoint ep = intf.getEndpoint(j);
                if (ep.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;

                if (ep.getDirection() == UsbConstants.USB_DIR_IN) {
                    bulkIn = ep.getAddress();
                } else {
                    bulkOut = ep.getAddress();
                }
            }

            if (bulkIn >= 0 && bulkOut >= 0) {
                epIn = bulkIn;
                epOut = bulkOut;
                targetIntf = intf;
                break;
            }
        }

        if (targetIntf == null) {
            Log.e(TAG, "no bulk interface found on AOA device");
            conn.close();
            return;
        }

        if (!conn.claimInterface(targetIntf, true)) {
            Log.e(TAG, "failed to claim interface");
            conn.close();
            return;
        }

        int fd = conn.getFileDescriptor();
        Log.i(TAG, "AOA device opened: fd=" + fd +
                " ep_in=0x" + Integer.toHexString(epIn) +
                " ep_out=0x" + Integer.toHexString(epOut));

        activeConnection = conn;  // Keep alive to maintain fd validity
        listener.onDeviceReady(fd, epIn, epOut);
    }

    private void sendAoaString(UsbDeviceConnection conn, int index, String value) {
        byte[] bytes = (value + "\0").getBytes();
        conn.controlTransfer(
                UsbConstants.USB_DIR_OUT | UsbConstants.USB_TYPE_VENDOR,
                AOA_SEND_STRING, 0, index, bytes, bytes.length, 1000);
    }

    private boolean isAoaDevice(int vid, int pid) {
        return vid == GOOGLE_VID && pid >= AOA_PID_MIN && pid <= AOA_PID_MAX;
    }

    private void closeConnection() {
        if (activeConnection != null) {
            activeConnection.close();
            activeConnection = null;
        }
    }
}
