# 0004 — Wireless Android Auto

> Created: 2026-04-22
> Status: VERIFIED (video streaming confirmed 2026-04-22)
> Reference: aa/app/android/src/main/java/com/aauto/app/wireless/
>
> **Naming note**: This work was originally drafted as "Phase 6" but conflicts with
> 0001's Phase 6 (Hardening). Treated as a parallel Wireless AA track, not a
> renumbering of the original phases. The original Phase 6 in 0001 remains
> Hardening. Internal subsections still use the historical 6a–6e labels.

---

## Overview

Add wireless Android Auto support. Reuses existing AAP protocol (transport-agnostic)
over TCP instead of USB. BT RFCOMM handles discovery and WiFi credential exchange.

## Key Facts (from reference)

- **No BLE**: classic BT RFCOMM only (SDP service record with AAW UUID)
- **WiFi AP**: system hotspot, not self-created. Read config via WifiManager
- **TCP port**: 5277
- **AAW UUID**: `4DE17A00-52CB-11E6-BDF4-0800200C9A66`
- **RFCOMM protocol**: [2B length][2B msgId][protobuf payload]
- **BT profile conflict**: A2DP_SINK must be disconnected during wireless session

## Connection Sequence

```
1. User enables WiFi AP (system hotspot) + BT via UI toggles
2. Both ready → RFCOMM server starts listening on AAW UUID
3. Phone BT pairs with HU → connects to RFCOMM service
4. HU sends VERSION_REQUEST + START_REQUEST(ip, port)
5. Phone sends INFO_REQUEST → HU responds INFO_RESPONSE(ssid, psk, bssid)
6. Phone sends CONNECTION_STATUS → START_RESPONSE
7. Phone connects to WiFi AP → TCP 5277
8. AAP handshake (VERSION → SSL → AUTH) — identical to USB
9. Streaming begins
```

## Components

### 6a: TCP Transport (DONE)
- AndroidTcpTransport.hpp/cpp
- AIDL startTcpSession()
- TransportFactory TCP routing

### 6b: BluetoothWirelessManager (DONE)
- RFCOMM server on AAW UUID
- AAW handshake (version, start, info exchange)
- Keep-alive loop after handshake
- Listener interface for lifecycle events

### 6c: AaService wireless integration (DONE)
- Manage BluetoothWirelessManager lifecycle
- Listen for WiFi AP state changes
- Read hotspot config (SSID, PSK, BSSID, IP)
- Start TCP session via engineProxy.startTcpSession()
- BT + WiFi AP state tracking
- During implementation, the wireless logic was extracted out of `AaService`
  into dedicated classes: `WirelessSessionCoordinator`, `WirelessStateTracker`,
  `HotspotConfigProvider` (see refactoring_assessment.md and F.16)

### 6d: DeviceListActivity UI (DONE)
- BT ON/OFF toggle button
- WiFi AP ON/OFF toggle button
- Show wireless devices alongside USB devices
- State display: connecting / connected

### 6e: BtProfileGate (DONE)
- Disable A2DP_SINK during wireless session
- Prevent AVRCP PAUSE conflict
- Restore on session end

## Files

### New
- `app/android/src/com/aauto/app/wireless/BluetoothWirelessManager.java`
- `app/android/src/com/aauto/app/wireless/BtProfileGate.java`
- `app/android/src/com/aauto/app/WirelessSessionCoordinator.java`
- `app/android/src/com/aauto/app/WirelessStateTracker.java`
- `app/android/src/com/aauto/app/HotspotConfigProvider.java`

### Modified
- `app/android/src/com/aauto/app/AaService.java` — wireless lifecycle (now delegates to coordinator/tracker)
- `app/android/src/com/aauto/app/DeviceListActivity.java` — BT/WiFi toggles
- `app/android/AndroidManifest.xml` — BT/WiFi permissions
