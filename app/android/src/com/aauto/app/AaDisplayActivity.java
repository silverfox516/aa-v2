package com.aauto.app;

import android.app.Activity;
import android.content.ComponentName;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;
import android.view.MotionEvent;
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
    private boolean surfaceReady;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new SurfaceView(this);
        setContentView(surfaceView);
        surfaceView.getHolder().addCallback(this);

        Intent serviceIntent = new Intent(this, AaService.class);
        bindService(serviceIntent, serviceConnection, BIND_AUTO_CREATE);

        IntentFilter filter = new IntentFilter();
        filter.addAction(AaService.ACTION_SESSION_ENDED);
        filter.addAction(AaService.ACTION_VIDEO_FOCUS_CHANGED);
        registerReceiver(sessionEndReceiver, filter);

        Log.i(TAG, "AaDisplayActivity created");
    }

    @Override
    protected void onDestroy() {
        try { unregisterReceiver(sessionEndReceiver); } catch (Exception ignored) {}
        unbindService(serviceConnection);
        super.onDestroy();
    }

    private final BroadcastReceiver sessionEndReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (AaService.ACTION_SESSION_ENDED.equals(action)) {
                Log.i(TAG, "session ended, finishing");
                finish();
            } else if (AaService.ACTION_VIDEO_FOCUS_CHANGED.equals(action)) {
                boolean projected = intent.getBooleanExtra(
                        AaService.EXTRA_PROJECTED, true);
                if (!projected) {
                    Log.i(TAG, "video focus lost, finishing");
                    finish();
                }
            }
        }
    };

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
            aaService.onSurfaceDestroyed();
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
            aaService.onSurfaceReady();
        }
    }

    // ===== Touch input =====

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (aaService == null) return super.onTouchEvent(event);

        int action;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:   action = 0; break;
            case MotionEvent.ACTION_UP:     action = 1; break;
            case MotionEvent.ACTION_MOVE:   action = 2; break;
            case MotionEvent.ACTION_CANCEL: action = 1; break;
            default: return super.onTouchEvent(event);
        }

        int videoW = aaService.getVideoWidth();
        int videoH = aaService.getVideoHeight();
        int x = (int) (event.getX() / surfaceView.getWidth() * videoW);
        int y = (int) (event.getY() / surfaceView.getHeight() * videoH);

        aaService.sendTouchEvent(x, y, action);
        return true;
    }

}
