# Solar Sweeper: Arduino UART Protocol

This document defines the serial communication protocol between the Raspberry Pi (Edge Daemon) and the Arduino microcontroller (Hardware Actuators).

## 1. Physical & Link Layer
* **Transport:** UART Serial over USB (`/dev/ttyACM0` by default).
* **Configuration:** 115200 baud, 8 data bits, no parity, 1 stop bit (8-N-1). Flow control is disabled.
* **Framing:** All messages are plain ASCII text terminated by a newline character (`\n`). Carriage returns (`\r`) are ignored if present.

## 2. Protocol Design Philosophy
The protocol is highly asymmetric:
* **Pi to Arduino (Commands):** Sparse, fire-and-forget commands. The Pi tells the Arduino *what* high-level action to do (e.g., "clean this sector"), not *how* to step the motors. 
* **Arduino to Pi (Events):** Asynchronous push events. The Arduino independently reports when a long-running task is done, or if an interrupt fires. 

This prevents the Pi from ever blocking on serial I/O, allowing the Edge Daemon to maintain its lock-free concurrency model.

## 3. Commands (Pi -> Arduino)

These commands are sent by the Pi's `SerialHardwareDriver` when the FSM enters specific states.

| Command | Format | Example | Description |
| :--- | :--- | :--- | :--- |
| **Dry Clean** | `D<sector_id>\n` | `D2\n` | Instructs the Arduino to move to Sector 2 and perform a dry wipe (brushes only, no water). |
| **Wet Clean** | `W<sector_id>\n` | `W4\n` | Instructs the Arduino to move to Sector 4 and perform a wet wash (brushes + water pump active). |
| **Halt** | `H\n` | `H\n` | **Emergency Halt**. The Arduino must immediately cut PWM/relays to all actuators. Idempotent. |

*Note: `<sector_id>` is an integer representing the physical zone of the solar array.*

## 4. Events (Arduino -> Pi)

These events are asynchronously written by the Arduino. The Pi's background listener thread reads these and translates them into `TinyFSM` events.

| Event | Format | Example | Description |
| :--- | :--- | :--- | :--- |
| **Ready** | `R\n` | `R\n` | Sent exactly once by the Arduino at the end of its `setup()` routine (after self-tests pass). The Pi waits for this before leaving `Boot_Init`. |
| **Clean Complete** | `C<sector_id>\n` | `C2\n` | Sent when the Arduino has successfully finished the requested `D` or `W` command for that sector. |
| **Emergency Stop** | `E<reason>\n` | `Elimit_switch\n` | Sent immediately if a hardware safety interrupt triggers (e.g., endstop limit hit, motor stall, current spike). The `reason` string is optional but highly recommended for telemetry. |

### The E-Stop Fast Path
When the Pi receives an `E` event, it executes a two-step safety process:
1. It immediately sets the `g_estop_flag` `std::atomic<bool>`. This takes sub-microseconds and instantly prevents the Pi from sending further commands.
2. It pushes the `EvEmergencyStop` event to the FSM queue so the software state machine can gracefully log the fault and transition to the `Error` state.

## 5. Error Handling
* **Malformed Commands (Arduino Side):** If the Arduino receives an unrecognized character, it should discard the buffer and halt motion for safety.
* **Malformed Events (Pi Side):** If the Pi receives an unknown byte (e.g., noise on the line), it logs a warning and discards the line. It does **not** crash the daemon.

## 6. Extending the Protocol

Because the system is split across two distinct environments (the C++ Edge Daemon on the Pi and the C firmware on the Arduino), adding new behaviors requires updates to both codebases.

### A. Adding a New Command (Pi -> Arduino)
*Example: Adding a command to tell the Arduino to park its actuators.*

**On the Pi (Edge Daemon):**
1. **The Interface**: Add `virtual void parkActuators() = 0;` to `include/solaredge/Interfaces/IHardwareDriver.h`.
2. **The Mocks**: Implement the empty/logging method in `include/solaredge/Mocks/MockHardwareDriver.h`.
3. **The Serial Driver**: Implement the method in `src/Hardware/SerialHardwareDriver.cpp` to send the new byte over UART:
   ```cpp
   void SerialHardwareDriver::parkActuators() {
       sendCommand("P\n");
   }
   ```
4. **The FSM**: Call `Context::hw->parkActuators()` inside the appropriate state transition (e.g., entering the `Idle` state) in `include/solaredge/Core/States.h`.

**On the Arduino:**
1. Update your main `loop()`'s serial read function to parse the incoming `'P'` character.
2. Trigger the appropriate stepper/servo functions to move the cleaning head to the home position.

### B. Adding a New Event (Arduino -> Pi)
*Example: The Arduino needs to proactively report that the battery is low.*

**On the Arduino:**
1. When the battery voltage drops, write the new event to the serial port: `Serial.println("B10");` (where 10 is the percentage).

**On the Pi (Edge Daemon):**
1. **Define the Event**: Create a new struct in `include/solaredge/Core/Events.h`:
   ```cpp
   struct EvBatteryLow : tinyfsm::Event { 
       int level; 
   };
   ```
2. **Register the Event**: Add `events::EvBatteryLow` to the `EventVariant` `std::variant` definition in `include/solaredge/Core/EventQueue.h`.
3. **Parse the Byte**: In `src/Hardware/SerialHardwareDriver.cpp`, update `processLine()` to catch `'B'`:
   ```cpp
   case 'B':
       queue_.push(events::EvBatteryLow{{}, std::stoi(payload)});
       break;
   ```
4. **Handle the Event**: In `include/solaredge/Core/States.h`, add `void react(const events::EvBatteryLow& e) override { ... }` to any FSM state that needs to care about the battery level (e.g., to prevent starting a new cleaning cycle if the battery is below 20%).
