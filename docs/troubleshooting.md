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
