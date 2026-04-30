package com.aauto.app;

import android.content.Context;
import android.content.Intent;

class UiNavigationController {
    private final Context context;

    UiNavigationController(Context context) {
        this.context = context;
    }

    void showDeviceList() {
        Intent intent = new Intent(context, DeviceListActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        context.startActivity(intent);
    }

    void showDisplayFlow() {
        // singleTask + REORDER_TO_FRONT: reuse the existing
        // AaDisplayActivity if one is already showing (e.g. during a
        // wireless->USB transport switch on the same phone). Without
        // REORDER_TO_FRONT, the system would relaunch the activity and
        // its SurfaceView would be destroyed/recreated mid-stream,
        // racing with incoming video frames. Manifest declares
        // AaDisplayActivity launchMode=singleTask so a single instance
        // is reused across activateSession() calls.
        Intent display = new Intent(context, AaDisplayActivity.class);
        display.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        context.startActivity(display);
    }
}
