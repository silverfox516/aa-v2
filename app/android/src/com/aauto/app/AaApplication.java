package com.aauto.app;

import android.app.Application;
import android.content.Intent;
import android.util.Log;

/**
 * Application entry point. Started automatically by the system
 * because android:persistent="true" in manifest.
 * Starts AaService to begin USB monitoring.
 */
public class AaApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        Log.i("AA.Application", "starting AaService");
        startService(new Intent(this, AaService.class));
    }
}
