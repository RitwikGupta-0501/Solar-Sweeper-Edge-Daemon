# Solar Sweeper: MQTT API Contract

This document defines the MQTT topics and JSON payload schemas used for communication between the C++ Edge Daemon (Raspberry Pi) and the Cloud Backend / Web Dashboard.

## Global Rules
1. **Timestamps:** Every message sent by the daemon includes a `timestamp_ms` field (UNIX epoch in milliseconds). This allows the backend to handle out-of-order or cached messages smoothly.
2. **Device Routing:** All topics are prefixed with `solarsweeper/<device_id>/` so the backend can manage fleet deployments.

---

## 1. Telemetry (Robot -> Cloud)

### A. Live Status Stream
**Topic:** `solarsweeper/{device_id}/telemetry/status`
**Frequency:** Emitted on every FSM state change.
```json
{
  "timestamp_ms": 1718042451000,
  "fsm_state": "WetClean",
  "current_sector": 4,
  "network_status": "online"
}
```

### B. ML Detection Events
**Topic:** `solarsweeper/{device_id}/telemetry/detections`
**Frequency:** Emitted during the `Inspection` state when the Vision Engine processes a frame.
```json
{
  "timestamp_ms": 1718042453200,
  "sector": 4,
  "debris_type": "bird_dropping",
  "confidence_score": 94.2,
  "profiling": {
    "camera_capture_ms": 32.1,
    "inference_time_ms": 115.4
  }
}
```
*Note: The `inference_time_ms` helps the dashboard flag if thermal throttling is slowing down the ONNX model.*

### C. System Health & Diagnostics
**Topic:** `solarsweeper/{device_id}/telemetry/health`
**Frequency:** Periodic (e.g., every 30 seconds).
```json
{
  "timestamp_ms": 1718042480000,
  "cpu_temp_c": 68.5,
  "ram_usage_mb": 215,
  "uptime_s": 3600
}
```

### D. Cycle Summary
**Topic:** `solarsweeper/{device_id}/telemetry/cycle_summary`
**Frequency:** Emitted once when a cleaning cycle successfully completes (returns to `Idle`).
```json
{
  "timestamp_ms": 1718043000000,
  "total_duration_s": 142.5,
  "water_pump_uptime_s": 45.0,
  "total_sectors_cleaned": 10,
  "detections": {
    "bird_dropping": 2,
    "dust": 8,
    "leaf": 0
  },
  "average_inference_time_ms": 112.3
}
```

### E. Alerts / E-Stops
**Topic:** `solarsweeper/{device_id}/telemetry/alerts`
**Frequency:** Emitted immediately upon hardware fault or emergency stop.
```json
{
  "timestamp_ms": 1718043500000,
  "severity": "CRITICAL",
  "fault_code": "ESTOP_TRIGGERED",
  "reason": "limit_switch_hit"
}
```

---

## 2. Commands (Cloud -> Robot)

The daemon subscribes to these topics to receive commands from the web dashboard.

**Topic:** `solarsweeper/{device_id}/commands`
**Payload Schema:**
```json
{
  "command": "<command_name>",
  "args": {}
}
```

### Supported Commands:
| Command | Args | Description |
| :--- | :--- | :--- |
| `START_CYCLE` | `{}` | Initiates a full cleaning cycle from Sector 0. |
| `EMERGENCY_HALT` | `{}` | Immediately stops all actuators (software E-Stop). |
| `CLEAR_FAULT` | `{}` | Resets an E-Stop condition and re-runs the boot self-test. |
