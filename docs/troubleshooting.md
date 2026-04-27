# Troubleshooting — Development Issues and Solutions

> Problems encountered during aa-v2 development, with root causes and fixes.
> Reference for future debugging.

---

## 1. Phone does not send ChannelOpenRequest

**Symptom**: After ServiceDiscoveryResponse, phone sends nothing. No ChannelOpen.

**Root cause**: MicrophoneService not advertised. Samsung phones require
`media_source_service` (microphone) in ServiceDiscoveryResponse to proceed.

**Fix**: Add MicrophoneService stub (ch 7) with PCM 16kHz mono config.
Even though capture is not implemented, the service must be advertised.

**Verified**: Samsung SM-N981N.

---

## 2. Video at 3fps instead of 30fps

**Symptom**: Phone sends frames at 300ms intervals. Touch is responsive but
video updates slowly.

**Root cause**: ACK credit-based flow control. Phone starts with `max_unacked`
credits. After exhausting initial credits, phone sends one frame per ACK.
With ACK every 10 frames, effective rate was 3fps.

**Fix**: ACK every frame immediately after processing. Do not batch ACKs.
`max_unacked` in Config is the window size; `ack=1` per frame returns credits.

**Key insight**: The counting logic (ACK every N frames) should not be
implemented on the HU side. Just ACK every frame.

---

## 3. Video start delayed by several seconds

**Symptom**: ChannelOpen and MediaSetup complete, but first video frame
takes 3-5 seconds to arrive.

**Root cause**: Missing SensorStartResponse. Phone sends SensorStartRequest
and waits for SensorStartResponse(SUCCESS) before proceeding to video.

**Fix**: Send SensorStartResponse(SUCCESS) immediately when SensorStartRequest
arrives, before sending sensor data.

---

## 4. Large video frames corrupted (garbage message type)

**Symptom**: First multi-fragment frame (16KB+) decodes fine. Second one
produces garbage message type (0xb0f3, etc.). All subsequent frames corrupt.

**Root cause**: Two issues:
1. **Multi-first total_size field**: First fragment of a multi-part message has
   a 4-byte `total_size` field NOT included in `payload_length`. Framer was
   consuming wrong number of bytes, causing buffer desync.
2. **TLS record boundaries**: Reassembling ciphertext before decrypting fails
   because TLS records don't align with AAP message boundaries. SSL_read on
   reassembled blob consumes data from adjacent messages.

**Fix**:
1. Account for extra 4 bytes: `total_frame_size = HEADER + 4 + payload_length`
   for First fragments only.
2. Decrypt per-fragment (matching aasdk model). Each fragment's ciphertext
   is fed to SSL_read individually. Plaintext is accumulated per-channel.

---

## 5. AudioFocus RELEASE → phone stalls

**Symptom**: Phone sends AudioFocusRequest(type=4, RELEASE) after
ServiceDiscovery. If HU responds with GAIN, phone stops sending.

**Root cause**: RELEASE means "I hold no audio focus". Responding with GAIN
contradicts the phone's state. Must respond LOSS to acknowledge release.

**Fix**: Map AudioFocusRequest types correctly:
- GAIN → STATE_GAIN
- GAIN_TRANSIENT → STATE_GAIN_TRANSIENT
- GAIN_TRANSIENT_MAY_DUCK → STATE_GAIN_TRANSIENT_GUIDANCE_ONLY
- RELEASE → STATE_LOSS

---

## 6. TCC803x MediaCodec crash (ubsan: mul-overflow)

**Symptom**: Native crash in `MediaCodec::onReleaseOutputBuffer` with
abort message `ubsan: mul-overflow` in libstagefright.

**Root cause**: TCC803x's libstagefright has undefined behavior in
timestamp multiplication when `releaseOutputBuffer(idx, true)` is used.

**Fix**: Use explicit render timestamp instead:
```java
codec.releaseOutputBuffer(idx, System.nanoTime());
```

---

## 7. App crash on Surface passing via AIDL

**Symptom**: `NullPointerException` at `IAAEngine$Stub$Proxy.setSurface()`
when extracting IGraphicBufferProducer IBinder from Surface via Parcel.

**Root cause**: Android 10's Surface Parcel format does not allow extracting
the underlying IBinder via `writeToParcel/readStrongBinder`. The `gui/view/Surface.h`
AIDL parcelable is an intentionally lightweight stub with incomplete types.

**Resolution**: Abandoned cross-process Surface passing. Adopted F.12
architecture: daemon sends compressed H.264 via AIDL callback, app decodes
with Java MediaCodec locally.

---

## 8. USB write blocking asio strand

**Symptom**: All protocol processing stalls periodically.

**Root cause**: `async_write` performed synchronous `ioctl(USBDEVFS_BULK)`
directly on the asio strand. USB bulk write with 1000ms timeout blocks
the entire event loop.

**Fix**: Move USB write to dedicated thread (matching read thread pattern).
Strand only handles completion callbacks.

---

## 9. aa-engine daemon does not start

**Symptom**: `ServiceManager.getService("aa-engine")` returns null.
AaService retries 10 times and gives up.

**Root cause**: SELinux domain transition. The init.rc `seclabel` was
missing or set to an unprivileged domain. The daemon process gets killed
by SELinux before it can register with ServiceManager.

**Fix**: Set `seclabel u:r:su:s0` in `impl/aa-engine.rc` for eng builds.
Production builds need a proper SELinux policy with a dedicated domain.

**Diagnosis**: `dmesg | grep avc` shows SELinux denials for the daemon PID.

---

## 10. VideoDecoder crash on disconnect (race condition)

**Symptom**: `IllegalStateException` in `MediaCodec.queueInputBuffer()`
after USB disconnect. Codec is accessed after `release()`.

**Root cause**: Two threads race: the output drain thread calls
`dequeueOutputBuffer()` while the main thread calls `release()`.
The `configured` flag was checked after `release()` had already started.

**Fix**: Set `configured = false` BEFORE calling `codec.stop()` and
`codec.release()`. The output drain thread checks `configured` before
every codec operation and exits immediately if false.

---

## 11. Activity back stack — singleTask creates separate tasks

**Symptom**: AaDisplayActivity `finish()` returns to system launcher
instead of DeviceListActivity.

**Root cause**: Both activities had `launchMode="singleTask"`. Each
singleTask activity becomes the root of its own task. When
AaDisplayActivity finishes, its task is destroyed and the system shows
the launcher, not DeviceListActivity's task.

**Fix**: Remove `singleTask` from AaDisplayActivity (use default
`standard` mode). Use `TaskStackBuilder` in AaService to build the
correct back stack: DeviceListActivity → AaDisplayActivity. On
`finish()`, the natural back stack reveals DeviceListActivity.

**Key insight**: Only the app's root activity (DeviceListActivity)
should be `singleTask`. Child activities should use standard mode so
they stack within the same task.

---

## 12. Broadcast not delivered (non-protected broadcast)

**Symptom**: DeviceListActivity does not update when device state
changes. Log shows:
```
E ActivityManager: Sending non-protected broadcast
    com.aauto.app.DEVICE_STATE_CHANGED from system
```

**Root cause**: On Android Automotive, apps with `persistent="true"`
run in a system-like process context. The system enforces that
broadcasts from system processes must be "protected" (declared in
the platform). Custom action strings are flagged as non-protected
and may not be delivered.

**Fix**: Replace `sendBroadcast()` with a direct callback interface
(`DeviceStateListener`). DeviceListActivity registers as listener
when it binds to AaService. This is both more reliable and more
efficient than broadcast.

**Note**: `ACTION_SESSION_ENDED` broadcast (used by AaDisplayActivity)
still works because AaDisplayActivity registers its receiver within
the same process, avoiding the system broadcast check.

---

## 13. android.car.usb.handler intercepts USB devices

**Symptom**: When a phone is connected, `android.car.usb.handler`
launches `UsbHostManagementActivity` on top of our app. It either
shows a device chooser dialog or crashes with NPE trying to open a
device that already re-enumerated.

**Root cause**: Android Automotive's `UsbProfileGroupSettingsManager`
routes ALL USB attach events to `android.car.usb.handler` as the
system's fixed USB handler. This happens regardless of our app's
intent-filter.

**Workaround**: Two options:
1. `pm disable android.car.usb.handler` — disables the system handler
   entirely. Simple but affects all USB device handling.
2. Launch DeviceListActivity from `onDeviceAvailable()` with
   `FLAG_ACTIVITY_REORDER_TO_FRONT` to reclaim foreground after the
   system handler finishes.

**Current approach**: Option 2. The system handler briefly flashes
but our DeviceListActivity comes to foreground with the device list.

**Long-term**: Register our app as the exclusive AOA handler in the
platform's USB routing config, or build a proper `android.car.usb.handler`
replacement that delegates AOA devices to our app.

---

## 14. UsbMonitor duplicate onDeviceAttached calls

**Symptom**: AOA switch is attempted twice for the same device.
Log shows two identical "non-AOA device, attempting AOA switch" entries.

**Root cause**: `UsbMonitor.start()` scans already-attached devices
via `UsbManager.getDeviceList()` AND registers a BroadcastReceiver
for `USB_DEVICE_ATTACHED`. If the device was attached before the
receiver was registered, both paths fire for the same device.

**Fix**: Add `aoaSwitchInProgress` flag. Set it to `true` when
starting an AOA switch, `false` when an AOA device is detected.
Ignore non-AOA attach events while a switch is already in progress.

---

## 15. BT/WiFi API calls rejected — "System has not boot yet"

**Symptom**: `BluetoothAdapter.enable()` throws `IllegalStateException:
System has not boot yet` even though `sys.boot_completed` is `1`.
`WifiManager.startSoftAp()` throws `SecurityException: NETWORK_STACK`.

**Root cause**: Two issues:
1. App ran as regular uid (10xxx) instead of system uid (1000).
   `BluetoothManagerService.checkPackage()` has stricter checks for
   non-system apps. Error message is misleading — not actually a boot
   timing issue.
2. `startSoftAp()` requires `NETWORK_STACK` signature permission, which
   must be explicitly declared in manifest AND whitelisted in
   `privapp-permissions-*.xml`.

**Fix**:
1. Add `android:sharedUserId="android.uid.system"` to manifest. App
   runs as uid 1000 with full system privileges (requires `certificate:
   "platform"` in Android.bp).
2. Add `NETWORK_STACK` permission to manifest + privapp whitelist.
3. Use `startSoftAp(null)` / `stopSoftAp()` instead of the removed
   `setWifiApEnabled()`.

**Note**: Reference app also uses `sharedUserId="android.uid.system"`.
This was the key difference that caused all BT/WiFi permission failures.

---

## 16. Wireless AA — version exchange timeout (not version mismatch)

**Symptom**: Wireless AA TCP connection established, AAP session starts,
VERSION_REQUEST is sent successfully (10 bytes written), but times out
after 5 seconds with "protocol version mismatch" (misleading error name
from state timeout handler).

**Root cause**: Two issues:
1. **`asio::async_read` vs `async_read_some`**: `asio::async_read(socket, buffer)`
   is a composed operation that waits until the ENTIRE buffer (16384 bytes) is
   filled. Phone sends VERSION_RESPONSE (~12 bytes) but the read never completes
   because it waits for 16384 bytes total. Fix: use `socket.async_read_some(buffer)`
   which returns as soon as any data arrives.
2. **TCP accept blocking io_context**: Initial implementation called `accept()`
   inside TransportFactory which runs on the io_context thread. This blocked the
   entire event loop during the 3-4 second WiFi connection delay. Fix: moved
   accept to Binder thread in AidlEngineController, pass accepted fd via descriptor.

**Fix**:
- AndroidTcpTransport: `async_read` → `socket_.async_read_some`
- AidlEngineController: TCP accept on Binder thread, descriptor `"tcp:fd=N"`
- TransportFactory: assign pre-accepted fd, set `non_blocking(true)`

**Why USB wasn't affected**: USB transport uses dedicated threads with blocking
ioctl reads that return whatever data is available — equivalent to `read_some`.

---

## 17. Native daemon logs not visible in logcat

**Symptom**: aa-engine daemon logs (Session, Transport, Service) not
appearing in `logcat | grep AA`.

**Root cause**: Default `log_impl()` writes to `stderr` via `fprintf`.
On Android, `stderr` may not be redirected to logcat depending on how
the daemon is started (init.rc vs manual).

**Fix**: Register Android-native log function in `main()`:
```cpp
set_log_function(android_log_function);
// Uses __android_log_vprint() with proper tag and level
```

---

## 18. Wireless AA — ~500ms input latency vs reference

**Symptom**: Touch response on wireless AA is noticeably slower than the
reference implementation (~500ms). Frame rate is fine (no drops), but the
visual response to touch is delayed.

**Root cause**: Architecture difference. Reference uses JNI with native
AMediaCodec (single process, zero-copy). Our F.12 architecture routes
every video frame through AIDL:
```
TCP → Session → VideoService → AIDL media queue → Binder → Java VideoDecoder → MediaCodec
```
Each frame is copied into the media queue, then serialized through Binder.
Additional factors:
- `TCP_NODELAY` was missing initially (Nagle buffering small ACK/touch packets)
- MediaCodec `KEY_LOW_LATENCY` not set
- `INPUT_TIMEOUT_US = 5ms` may cause input buffer stalls

**Fixes applied** (cumulative improvement):
1. `TCP_NODELAY` on TCP socket (reduces ACK/touch event delay)
2. Dedicated read/write threads for TCP transport (matching USB pattern,
   decouples I/O from ASIO event loop)
3. Direct oneway Binder calls (removed media queue thread hop)
4. MediaCodec `low-latency` hint + reduced input/output timeouts (1ms)

**Remaining**: ~200-300ms latency vs reference. Root cause is F.12
architecture (AIDL process boundary). Reference uses JNI with native
AMediaCodec (single process, zero-copy). Full fix requires native
video decoding or SharedMemory for frame transfer.

**Update 2026-04-27**: Phase 4 stub channels were a separate cause of
visible lag spikes during scroll (cadence dropped to 5-10fps); see #22
for the full analysis. After unregistering them and adding
`BufferedTransport` (F.19) so underlying transport reads are no longer
serialized behind upper-layer processing, sustained scroll cadence
recovered to 30fps and end-to-end input-to-display lag dropped from
~400ms to ~300ms — within the F.12 baseline. Further reduction now
truly requires the SharedMemory / JNI changes called out above.

**5GHz AP**: TCC803x WiFi chip supports 5GHz (Band 2), but SoftApManager
requires a valid country code. `iw reg set`, `settings put`, `setprop
persist.vendor.wifi.country` all failed at runtime. Requires platform
build configuration (`ro.boot.wificountrycode=KR` or board overlay).

---

## 19. Video not displayed after focus transition (IDR frame drop)

**Symptom**: Video works on first connection. After going back to device
list and re-activating, video sometimes does not appear. TCC803x HW
decoder logs `key frame search (I-Slice)` 201 times then dies with
`OMX_ErrorHardware`.

**Root causes** (multiple issues found):
1. **Garbage timestamps**: Phone timestamps after focus transition were
   invalid (e.g., `2798039084948324352`). TCC803x HW decoder crashed on
   these values.
2. **IDR frame drop**: `configureCodec` called `release()` which blocks
   on `outputThread.join()`. During this block, the IDR frame arrived on
   a Binder thread and was dropped (`configured=false`). Only P-frames
   arrived after the decoder was ready — no keyframe = no output.
3. **Output thread complexity**: Separate output drain thread required
   synchronization (codecLock, configured flag) and created race
   conditions during codec reconfiguration.

**Fix**: Adopted reference implementation's single-threaded model:
- No separate output thread. `feedData()` queues input AND drains output
  in one call (matching reference `pushData` pattern).
- CSD (SPS/PPS) not set in MediaFormat — arrives in stream, decoded
  like any other NAL unit.
- Timestamp always 0 (no phone timestamp, no monotonic counter).
- `releaseOutputBuffer(idx, true)` (matching reference).
- `open()` configures codec once; subsequent `feedData` calls just
  queue data. No reconfiguration on same session.

**Key insight**: The dual-threaded approach added complexity (thread sync,
join blocking, codec caching) without real benefit. HW decoder is fast
enough to decode within one `feedData` call cycle.

---

## 20. VideoFocus must follow Surface lifecycle

**Symptom**: Phone sends codec config before Surface is ready. Codec
config cached but first IDR frame sometimes dropped.

**Root cause**: `VideoFocus(PROJECTED)` was sent in `activateSession()`
before `AaDisplayActivity` created the Surface. Phone started sending
before decoder was ready.

**Fix**: VideoFocus driven by Surface lifecycle:
- `surfaceCreated` → `onSurfaceReady()` → `VideoFocus(PROJECTED)`
- `surfaceDestroyed` → `onSurfaceDestroyed()` → `VideoFocus(NATIVE)`
- `activateSession()` no longer sends VideoFocus directly

---

## 21. Same phone USB+wireless: both connections drop

**Symptom**: Phone wirelessly connected. USB cable plugged in → AOA switch
→ phone drops wireless TCP → USB session starts → USB write times out
(phone transitioning) → both sessions dead.

Reverse: phone wired. Wireless connects → phone drops USB → slow to
detect disconnect (blocked on USB write timeout).

**Root cause**: Same phone can only maintain one AA connection. New
transport → phone drops old transport. Two issues:
1. Wireless→USB: USB write timeout (500ms) too short for phone transition
2. USB→wireless: USB disconnect detection slow (blocked on timeout)

**Fix**: Two complementary changes:
1. USB bulk timeout increased to 2000ms — gives phone time to complete
   wireless→USB transition. VERSION_REQUEST write waits up to 2s.
2. Same-phone detection in `onPhoneIdentified`: when ServiceDiscovery
   identifies a phone that already has an active session on a different
   transport, the old session is closed immediately (no timeout wait).

```
Wireless→USB: AOA switch → wireless drops → USB 2s timeout → phone ready → success
USB→wireless: wireless handshake → onPhoneIdentified → same phone → USB closed immediately
```

---

## 22. Stub channel advertise → phone throttles entire video cadence

**Symptom**: Scrolling on USB or wireless drops video from 30fps to
~5-10fps for 1-5 seconds whenever the user actively scrolls. Once
scrolling stops the cadence recovers. `write_queue` shows no backpressure
and transport writes are fast.

**Root cause**: Phase 4 added several "stub" services (PhoneStatus,
MediaPlayback, GenericNotification, MediaBrowser, Bluetooth,
VendorExtension) that advertise capability in ServiceDiscoveryResponse
but only have `fill_config` + an unhandled-log path — no actual response
to phone messages. The phone opens those channels and continues sending
periodic messages on them (e.g., `media.playback` MEDIA_START every 1s).
Our silence appears to make the phone throttle overall video cadence —
not because of CPU contention on our side (the stub traffic is ~100 B/s
vs ~210 KB/s of video) but because the phone's AAP scheduler / flow
control behaves differently when advertised channels are unresponsive.

**Diagnosis (2026-04-27)**:
- Added `video diag` and Java `video diag (Java RX)` cadence logs:
  native and Java arrival times were nearly identical (e.g., 172ms vs
  169ms per frame), proving the AIDL hop is not the cause.
- Added `write_queue depth` and `slow transport write` logs: never fired
  (queue stayed at 0~1, no writes >20ms). Rules out our transport
  backpressure.
- Disabling stub services 12/13/14 alone did not help (their traffic was
  small).
- Disabling 8~14 (especially `media.playback` ch10, which received
  76KB MEDIA_CONFIG plus periodic 26B MEDIA_START) restored steady
  30fps under heavy scroll.

**Fix**: Don't register stub services on `IServiceFactory::create_services`.
Only register channels with real handlers (currently 0~7: control, video,
audio×3, input, sensor, microphone). The stub service classes themselves
remain in-tree because their `fill_config` documents the proto layout —
re-register with proper response handlers when each channel becomes a
learning target.

**Generalized rule**: "advertise without responding" is not a free stub.
Phone-side scheduling treats unresponsive advertised channels as a brake
on the whole session. Either implement the response or don't advertise.
