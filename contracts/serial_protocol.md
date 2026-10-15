# Serial Interface Contract: Edge Daemon <-> Arduino Microcontroller

**Version:** 1.0.0  
**Target:** Arduino Uno / Mega / Nano / STM32 via USB-Serial or UART (`/dev/ttyACM0` or `/dev/ttyUSB0`)  
**Baud Rate:** 115200 bps, 8 Data Bits, 1 Stop Bit, No Parity (8-N-1)

---

## 1. Protocol Architecture & Malleability
The C++ Edge Daemon parses all byte values and interrupt triggers from `config/daemon.config.json`. If opcode byte values or interrupt assignments change, update the configuration file without recompiling the daemon.

---

## 2. Command Set (Edge Daemon -> Arduino)

All commands are dispatched by the Edge Daemon HAL over the serial connection.

| Logical Command | Default Byte | Description | Arduino Expected Action | Response / ACK |
|---|:---:|---|---|:---:|
| `DRY_CLEAN` | `0x01` | Begin dry roller-brush sweep for current sector | Start roller brush motor & linear gantry sweep | `0xA1` |
| `WET_CLEAN` | `0x02` | Begin wet clean (water solenoids + brush) | Open solenoid valve, start water pump & brush | `0xA2` |
| `EMERGENCY_HALT` | `0xFF` | Hardware kill signal (E-Stop / Abort) | Cut power to all motors and close solenoids immediately | `0xAF` |
| `PING` | `0x00` | Self-test / Heartbeat check | Verify actuator and sensor readiness | `0xA0` |

---

## 3. Interrupt & Response Set (Arduino -> Edge Daemon)

Interrupts are asynchronous status or fault bytes sent by the Arduino to the Edge Daemon.

| Logical Signal | Default Byte | Signal Type | Description | Daemon Reaction |
|---|:---:|:---:|---|---|
| `LIMIT_SWITCH_HIT` | `0xE1` | **Emergency Interrupt** | Mechanical limit switch triggered | Flips atomic `g_estop_flag` $\to$ `true`, halts all tasks, enters `Error` state |
| `MOTOR_STALL` | `0xE2` | **Emergency Interrupt** | Overcurrent / mechanical jam detected | Flips atomic `g_estop_flag` $\to$ `true`, halts all tasks, enters `Error` state |
| `OVERHEAT_WARNING` | `0xE3` | Warning | Driver temperature threshold exceeded | Logs warning & dispatches telemetry alert to cloud |
| `ACTION_COMPLETE` | `0xD0` | Success Notification | Sector cleaning pass completed successfully | Dispatches `EvCleanComplete`, advances to next sector |
| `ACK_PING` | `0xA0` | Response | Pong response confirming hardware ready | Marks hardware self-test ready |
| `ACK_DRY` | `0xA1` | Response | Confirms receipt of dry clean command | Logs command ACK |
| `ACK_WET` | `0xA2` | Response | Confirms receipt of wet clean command | Logs command ACK |
| `ACK_HALT` | `0xAF` | Response | Confirms emergency halt executed | Enters safe park |

---

## 4. Latency & Timing Constraints
- **Emergency Halt (E-Stop):** Arduino must process `0xFF` within $< 1\text{ ms}$.
- **Interrupt Transmission:** Limit switch triggers must be transmitted within $< 500\ \mu\text{s}$ of hardware pin state change.
- **Heartbeat Timeout:** If no ACK or ping response is received within `timeout_ms` (default: 500ms), daemon enters retry sequence (max 3 retries) before transitioning to `Hardware_Fault`.
