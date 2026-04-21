package com.aauto.app;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

/**
 * Fullscreen activity providing Surface for Android Auto video output.
 * Auto-launched when USB device is attached (via intent filter).
 */
public class AaDisplayActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "AA.Display";

    private SurfaceView surfaceView;
    private AaService aaService;
    private UsbDevice pendingDevice;
    private boolean surfaceReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);
        surfaceView.getHolder().addCallback(this);

        Intent serviceIntent = new Intent(this, AaService.class);
        startService(serviceIntent);
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE);

        handleUsbIntent(getIntent());

        Log.i(TAG, "AaDisplayActivity created");
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleUsbIntent(intent);
    }

    @Override
    protected void onDestroy() {
        unbindService(serviceConnection);
        super.onDestroy();
    }

    // ===== SurfaceHolder.Callback =====

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "surface created");
        surfaceReady = true;
        trySendSurface();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format,
                               int width, int height) {
        Log.i(TAG, "surface changed: " + width + "x" + height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surface destroyed");
        surfaceReady = false;
        if (aaService != null) {
            aaService.setSurface(null);
        }
    }

    // ===== Service connection =====

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override
        public void onServiceConnected(ComponentName name, IBinder binder) {
            AaService.LocalBinder localBinder = (AaService.LocalBinder) binder;
            aaService = localBinder.getService();
            Log.i(TAG, "AaService bound");
            trySendSurface();

            if (pendingDevice != null) {
                aaService.onNewUsbDevice(pendingDevice);
                pendingDevice = null;
            }
        }

        @Override
        public void onServiceDisconnected(ComponentName name) {
            aaService = null;
            Log.w(TAG, "AaService disconnected");
        }
    };

    private void trySendSurface() {
        if (aaService != null && surfaceReady) {
            aaService.setSurface(surfaceView.getHolder().getSurface());
        }
    }

    // ===== USB intent handling =====

    private void handleUsbIntent(Intent intent) {
        if (intent == null) return;

        if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(intent.getAction())) {
            UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
            if (device == null) return;

            Log.i(TAG, "USB device intent: vid=0x" +
                    Integer.toHexString(device.getVendorId()));

            if (aaService != null) {
                aaService.onNewUsbDevice(device);
            } else {
                pendingDevice = device;
                Log.i(TAG, "service not ready, device queued");
            }
        }
    }
}
