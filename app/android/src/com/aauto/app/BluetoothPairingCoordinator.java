package com.aauto.app;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Handler;
import android.util.Log;

import com.aauto.engine.IAAEngine;

import java.util.HashMap;
import java.util.Map;

/**
 * Bridges AAP channel 13 (BluetoothService) to Android's Bluedroid
 * stack — see plan 0009 Day 2.
 *
 * Flow:
 *  1) Native receives PAIRING_REQUEST from phone
 *  2) Native -> AIDL onPairingRequest(sid, phoneAddress, method)
 *  3) AaService forwards to {@link #onPairingRequest}
 *  4) Coordinator turns BT on if needed, looks up the device, and
 *     either reports already-paired immediately or starts createBond
 *     and waits for ACTION_BOND_STATE_CHANGED
 *  5) Result -> aaEngine.completePairing(sid, status, alreadyPaired)
 *  6) Native sends PAIRING_RESPONSE on the AAP channel
 *
 * AUTH_DATA flow is the same shape: onAuthData -> setPin /
 * setPasskey on the BluetoothDevice (when feasible) -> aaEngine.completeAuth.
 *
 * MessageStatus codes (mirror shared/MessageStatus.proto):
 *   0   = STATUS_SUCCESS
 *  -7   = STATUS_INTERNAL_ERROR
 *  -11  = STATUS_BLUETOOTH_UNAVAILABLE
 *  -13  = STATUS_BLUETOOTH_INVALID_PAIRING_METHOD
 */
public class BluetoothPairingCoordinator {
    private static final String TAG = "AA.BtPair";

    public static final int STATUS_SUCCESS                 = 0;
    public static final int STATUS_INTERNAL_ERROR          = -7;
    public static final int STATUS_BLUETOOTH_UNAVAILABLE   = -11;
    public static final int STATUS_BLUETOOTH_INVALID_METHOD = -13;

    private final Context context;
    private final Handler handler;
    private final IAAEngineProvider engineProvider;
    private final BluetoothAdapter btAdapter;

    /** sid for which pairing is currently in flight, indexed by phone MAC. */
    private final Map<String, Integer> pendingPairing = new HashMap<>();

    /** Requests received while BT was OFF — drained when STATE_ON arrives. */
    private static final class DeferredRequest {
        final int sessionId;
        final String phoneAddress;
        final int method;
        DeferredRequest(int s, String a, int m) {
            sessionId = s; phoneAddress = a; method = m;
        }
    }
    private final java.util.ArrayDeque<DeferredRequest> deferred =
            new java.util.ArrayDeque<>();

    /** Allow the coordinator to call back into the engine without holding
     *  a direct AaService reference. */
    public interface IAAEngineProvider {
        IAAEngine getEngine();
    }

    public BluetoothPairingCoordinator(Context context, Handler handler,
                                       IAAEngineProvider engineProvider) {
        this.context = context;
        this.handler = handler;
        this.engineProvider = engineProvider;
        this.btAdapter = BluetoothAdapter.getDefaultAdapter();
    }

    public void register() {
        IntentFilter bondFilter = new IntentFilter();
        bondFilter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        context.registerReceiver(bondReceiver, bondFilter);

        IntentFilter adapterFilter = new IntentFilter(
                BluetoothAdapter.ACTION_STATE_CHANGED);
        context.registerReceiver(adapterReceiver, adapterFilter);
    }

    public void unregister() {
        try {
            context.unregisterReceiver(bondReceiver);
        } catch (IllegalArgumentException ignored) {
            // not registered yet
        }
        try {
            context.unregisterReceiver(adapterReceiver);
        } catch (IllegalArgumentException ignored) {
        }
    }

    /** Called from AaService.onPairingRequest. */
    public void onPairingRequest(int sessionId, String phoneAddress, int method) {
        Log.i(TAG, "pairing request: session=" + sessionId
                + " phone=" + phoneAddress + " method=" + method);

        if (btAdapter == null) {
            Log.w(TAG, "no BluetoothAdapter on this device");
            complete(sessionId, STATUS_BLUETOOTH_UNAVAILABLE, false);
            return;
        }

        // BluetoothAdapter.enable() is asynchronous — createBond()
        // called immediately after will return false because the
        // adapter isn't STATE_ON yet. Defer the request until the
        // ACTION_STATE_CHANGED broadcast says STATE_ON.
        if (!btAdapter.isEnabled()) {
            Log.i(TAG, "BT OFF — enable() requested, deferring pairing");
            deferred.addLast(new DeferredRequest(sessionId, phoneAddress, method));
            btAdapter.enable();
            return;
        }

        startBonding(sessionId, phoneAddress);
    }

    private void startBonding(int sessionId, String phoneAddress) {
        BluetoothDevice device;
        try {
            device = btAdapter.getRemoteDevice(phoneAddress);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "invalid BT address: " + phoneAddress, e);
            complete(sessionId, STATUS_INTERNAL_ERROR, false);
            return;
        }

        int bond = device.getBondState();
        if (bond == BluetoothDevice.BOND_BONDED) {
            Log.i(TAG, "device already bonded — short-circuit");
            complete(sessionId, STATUS_SUCCESS, /*alreadyPaired=*/true);
            return;
        }

        // Track this pairing so the receiver knows which sid to respond on.
        pendingPairing.put(phoneAddress.toUpperCase(), sessionId);

        if (bond == BluetoothDevice.BOND_BONDING) {
            Log.i(TAG, "bond already in progress — wait for state change");
            return;
        }

        boolean started = device.createBond();
        if (started) {
            Log.i(TAG, "createBond started — waiting for BOND_BONDED broadcast"
                    + " (system pairing UI may appear on the phone)");
        } else {
            Log.w(TAG, "createBond returned false (BT off? device not in range?)");
            pendingPairing.remove(phoneAddress.toUpperCase());
            complete(sessionId, STATUS_INTERNAL_ERROR, false);
        }
    }

    private final BroadcastReceiver adapterReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE,
                    BluetoothAdapter.ERROR);
            if (state != BluetoothAdapter.STATE_ON) return;
            if (deferred.isEmpty()) return;
            Log.i(TAG, "BT now ON — draining " + deferred.size()
                    + " deferred pairing request(s)");
            while (!deferred.isEmpty()) {
                DeferredRequest req = deferred.pollFirst();
                startBonding(req.sessionId, req.phoneAddress);
            }
        }
    };

    /** Called from AaService.onAuthData. PIN/Passkey forwarding. */
    public void onAuthData(int sessionId, String authData, int method) {
        Log.i(TAG, "auth data: session=" + sessionId
                + " data=\"" + authData + "\" method=" + method);

        // Day 2 baseline: just ack the data. Real PIN/passkey injection
        // requires either a system app pairing dialog or
        // BluetoothDevice.setPin (only available to system uid). Most
        // Android Auto installs rely on the OS pairing UI handling this
        // automatically once createBond runs above. Logging the data is
        // useful for verifying the AAP channel content.
        complete_auth(sessionId, STATUS_SUCCESS);
    }

    private final BroadcastReceiver bondReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            BluetoothDevice device = intent.getParcelableExtra(
                    BluetoothDevice.EXTRA_DEVICE);
            int state = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE,
                    BluetoothDevice.ERROR);
            int prev  = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE,
                    BluetoothDevice.ERROR);
            if (device == null) return;

            String addr = device.getAddress().toUpperCase();
            Integer sid = pendingPairing.get(addr);
            if (sid == null) return;  // not ours

            Log.i(TAG, "bond state " + prev + " -> " + state
                    + " for " + addr + " (sid=" + sid + ")");

            switch (state) {
                case BluetoothDevice.BOND_BONDED:
                    pendingPairing.remove(addr);
                    complete(sid, STATUS_SUCCESS, /*alreadyPaired=*/false);
                    break;
                case BluetoothDevice.BOND_NONE:
                    if (prev == BluetoothDevice.BOND_BONDING) {
                        pendingPairing.remove(addr);
                        complete(sid, STATUS_INTERNAL_ERROR, false);
                    }
                    break;
                default:
                    // BOND_BONDING — keep waiting
                    break;
            }
        }
    };

    private void complete(int sessionId, int status, boolean alreadyPaired) {
        IAAEngine engine = engineProvider.getEngine();
        if (engine == null) {
            Log.w(TAG, "engine proxy null; cannot complete pairing");
            return;
        }
        try {
            engine.completePairing(sessionId, status, alreadyPaired);
        } catch (Exception e) {
            Log.w(TAG, "completePairing threw", e);
        }
    }

    private void complete_auth(int sessionId, int status) {
        IAAEngine engine = engineProvider.getEngine();
        if (engine == null) {
            Log.w(TAG, "engine proxy null; cannot complete auth");
            return;
        }
        try {
            engine.completeAuth(sessionId, status);
        } catch (Exception e) {
            Log.w(TAG, "completeAuth threw", e);
        }
    }
}
