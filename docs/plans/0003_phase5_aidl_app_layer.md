# 0003 — Phase 5 Minimal: AIDL Daemon + App Layer for Device Testing

> Created: 2026-04-17
> Status: DONE (verified on real device 2026-04-21)
> Scope: Minimum viable app layer to test handshake with real phone
>
> **Implementation status (2026-04-27)**: All steps completed. Several originally
> deferred items have since been implemented as part of later work:
> - SessionManager → `app/android/src/com/aauto/app/SessionManager.java` (multi-session tracking, F.13)
> - DeviceListActivity → wireless/USB device list UI
> - BtMonitor / WifiMonitor → covered by `BluetoothWirelessManager` and `WirelessStateTracker` in 0004
> - Multi-session → enabled via `SessionManager` (F.13)

---

## Goal

Get a real phone connected via USB -> accessory mode -> SSL handshake ->
service discovery, visible in logcat. This unblocks Phase 1 exit criteria
and provides the foundation for Phase 2/3/4 work.

## NOT in scope (deferred — see status note above for items completed since)

- SessionManager, DeviceRegistry (multi-device tracking)
- BtMonitor, WifiMonitor (wireless AA)
- MainActivity (launcher UI)
- Polished UI / error display
- Multi-session

---

## Architecture

```
┌─────────────────────────────────┐       ┌──────────────────────────┐
│ App Process (Java)              │       │ aa-engine (native daemon) │
│                                 │       │                          │
│  AaDisplayActivity              │       │  Engine                  │
│    └─ Surface ──────────────────┼─AIDL──┼─→ AMediaCodecVideoSink  │
│                                 │       │                          │
│  AaService                      │       │  AidlEngineController    │
│    ├─ UsbMonitor                │       │    (Bn server)           │
│    │   └─ detect USB            │       │                          │
│    │   └─ accessory handshake   │       │  Session(s)              │
│    │   └─ get fd ───────────────┼─AIDL──┼─→ start_session(fd)     │
│    └─ IAAEngine.Stub.Proxy      │       │                          │
│         (Bp client)             │       │                          │
└─────────────────────────────────┘       └──────────────────────────┘
                                    Binder
```

---

## Implementation Steps

### Step 1: AIDL Interface Definition

Files:
- `aidl/com/aauto/engine/IAAEngine.aidl`
- `aidl/com/aauto/engine/IAAEngineCallback.aidl`

```
interface IAAEngine {
    int startSession(in ParcelFileDescriptor usbFd);
    void stopSession(int sessionId);
    void stopAll();
    void registerCallback(IAAEngineCallback cb);
    void setSurface(in Surface surface);
}

interface IAAEngineCallback {
    void onSessionStateChanged(int sessionId, int status);
    void onSessionError(int sessionId, int errorCode, String message);
}
```

### Step 2: Native Daemon (C++ AIDL server)

Files:
- `impl/android/aidl/AidlEngineController.hpp`
- `impl/android/aidl/AidlEngineController.cpp`
- Update `impl/android/main/main.cpp` — register as binder service

The daemon:
1. Starts, registers "aa-engine" binder service
2. Waits for client (AaService) to connect
3. Receives fd via startSession(ParcelFileDescriptor)
4. Extracts native fd, passes to Engine::start_session()
5. Receives Surface via setSurface(), passes to video sink

### Step 3: init.rc + SELinux

Files:
- `aa-engine.rc` — service definition
- `sepolicy/aa_engine.te` — type enforcement
- `sepolicy/file_contexts` — binary label

### Step 4: App Layer (Java)

Files:
- `app/android/AndroidManifest.xml`
- `app/android/service/AaService.java`
- `app/android/discovery/UsbMonitor.java`
- `app/android/ui/AaDisplayActivity.java`
- `app/android/Android.bp`

UsbMonitor flow:
1. Register BroadcastReceiver for USB_ACCESSORY_ATTACHED
2. When device attached: check VID/PID for Android Auto
3. Request USB permission
4. Open accessory -> get ParcelFileDescriptor
5. Pass fd to AaService -> AIDL -> engine

AaDisplayActivity:
1. Creates SurfaceView
2. Passes Surface to engine via AIDL setSurface()
3. Engine routes video frames to that Surface

### Step 5: Build Integration

- `aidl/Android.bp` — AIDL compilation
- `app/android/Android.bp` — APK build
- Update `impl/Android.bp` — link binder libs
- Root `Android.bp` — add subdirs

---

## Build Order

1. AIDL interface (no deps)
2. Native daemon update (depends on AIDL)
3. init.rc + SELinux (depends on binary name)
4. Java app (depends on AIDL)
5. Integration test on device

---

## Exit Criteria

- `adb shell service list | grep aa-engine` shows registered service
- Plug phone via USB -> logcat shows:
  - USB accessory detected
  - fd passed to engine
  - SSL handshake started
  - VERSION_REQUEST sent
  - (ideally) VERSION_RESPONSE received
