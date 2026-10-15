# MQTT Interface & Telemetry Contract: Edge Daemon <-> FastAPI Backend

**Version:** 1.0.0  
**Transport:** MQTT 3.1.1 / 5.0 over TCP (Default Port: 1883, TLS: 8883)  
**Topic Namespace Prefix:** Configurable via `mqtt.topic_prefix` (default: `solarsweeper/robot01`)

---

## 1. Malleability & Tolerant Reading
The Edge Daemon uses **RFC 6901 JSON pointer fallback resolution**. If field names evolve in the backend (e.g. `sector_id` $\leftrightarrow$ `sectorId` $\leftrightarrow$ `sector` or nested inside `{"data": {...}}`), the daemon automatically resolves them without breaking.

---

## 2. Inbound Commands (FastAPI Cloud -> Edge Daemon)

### 2.1 Start Cleaning Pass
- **Topic:** `{topic_prefix}/cmd/start_clean`
- **QoS:** 1 (At least once)
- **JSON Payload Example:**
```json
{
  "timestamp": "2026-08-21T18:30:00Z",
  "start_sector": 0,
  "mode": "auto"
}
```
- **Tolerant Fallbacks:** Accepts `start_sector`, `sector_id`, `sector`, or empty `{}` (defaults to sector 0).

### 2.2 Emergency Abort / Stop
- **Topic:** `{topic_prefix}/cmd/abort`
- **QoS:** 2 (Exactly once)
- **JSON Payload Example:**
```json
{
  "reason": "Operator manual override",
  "source": "dashboard_admin"
}
```

### 2.3 Clear E-Stop / Reset
- **Topic:** `{topic_prefix}/cmd/reset`
- **QoS:** 1
- **JSON Payload Example:**
```json
{
  "clear_reason": "Physical inspection cleared"
}
```

---

## 3. Outbound Telemetry (Edge Daemon -> FastAPI Cloud)

### 3.1 System Status Heartbeat
- **Topic:** `{topic_prefix}/telemetry/status`
- **Frequency:** Every 5 seconds or upon State Machine state transition
- **JSON Payload Example:**
```json
{
  "device_id": "edge-daemon-01",
  "state": "Inspection",
  "current_sector": 2,
  "total_sectors": 6,
  "estop_latched": false,
  "uptime_sec": 1420,
  "timestamp": "2026-08-21T18:30:05Z"
}
```

### 3.2 Cleaning Pass Result
- **Topic:** `{topic_prefix}/telemetry/clean_result`
- **Trigger:** Published immediately upon completing inspection or cleaning for each sector
- **JSON Payload Example:**
```json
{
  "device_id": "edge-daemon-01",
  "sector_id": 2,
  "debris_detected": true,
  "debris_type": "bird_dropping",
  "confidence": 0.94,
  "action_taken": "wet_clean",
  "duration_ms": 1250,
  "success": true,
  "timestamp": "2026-08-21T18:30:10Z"
}
```

### 3.3 Hardware / Safety Alert
- **Topic:** `{topic_prefix}/telemetry/alert`
- **Trigger:** Immediate on limit switch hit, motor stall, or boot hardware fault
- **JSON Payload Example:**
```json
{
  "device_id": "edge-daemon-01",
  "alert_type": "EMERGENCY_STOP",
  "severity": "CRITICAL",
  "reason": "limit_switch_hit",
  "state_before_fault": "DryClean",
  "timestamp": "2026-08-21T18:30:12Z"
}
```

---

## 4. Offline Telemetry Persistence, Power-Cut Durability & Cloud Deduplication

### 4.1 Persistence & Power-Cut Durability
If the network connection drops or the broker becomes unreachable:
1. The daemon continues active sector operations safely and enters `Offline_Idle`.
2. All telemetry payloads are appended to a persistent local cache on disk (`/var/lib/edge-daemon/cache.json`).
3. **Physical Sync Guarantee:** Every write executes a POSIX `fdatasync()` to force the storage controller to commit bytes to non-volatile flash immediately, guaranteeing zero data loss even if the Raspberry Pi loses power abruptly.
4. Upon reconnection to the MQTT broker, the cache is drained and republished in chronological order.

### 4.2 Cloud-Side Idempotency & Deduplication
To protect against network replays or reboots during MQTT ACK exchanges, every telemetry record carries unique idempotency keys:
- `message_id`: Globally unique identifier formatted as `{device_id}-{timestamp_ms}-{seq_id}` (e.g., `edge-daemon-01-1771681205000-42`).
- `seq_id`: Monotonically increasing 64-bit integer counter.
- `timestamp_ms`: Epoch millisecond timestamp of original event occurrence.

The FastAPI cloud backend can safely deduplicate incoming messages by indexing on `message_id` or `(device_id, seq_id)`.
