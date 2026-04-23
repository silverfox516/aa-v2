package com.aauto.app;

import android.app.TaskStackBuilder;
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
        TaskStackBuilder.create(context)
                .addNextIntent(new Intent(context, DeviceListActivity.class))
                .addNextIntent(new Intent(context, AaDisplayActivity.class))
                .startActivities();
    }
}
