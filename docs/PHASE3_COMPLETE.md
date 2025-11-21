# Phase 3 Complete: GATT Server Core

## Summary

Phase 3 of the BLE GATT Server implementation is now complete. The GATT server provides a complete implementation of the ATT protocol server, handles all standard GATT operations, and integrates with the transport layer and attribute database from previous phases.

---

## Files Created

### Headers (`blepp/`)

1. **`blegattserver.h`** - GATT server class (260 lines)
   - `BLEGATTServer` - Main server class
   - `ConnectionState` - Per-connection state tracking
   - User callbacks for connection events
   - Public API for notifications/indications

### Implementation (`src/`)

2. **`blegattserver.cc`** - GATT server implementation (1100+ lines)
   - ATT PDU dispatcher
   - 13 ATT request handlers
   - Response builders
   - CCCD management
   - Notification/indication support
   - Callback invocation

### Build System

3. **`Makefile.in`** - Updated to include `blegattserver.o`

---

## Key Features

### ✅ ATT Request Handlers

Complete implementation of all standard ATT operations:

| Opcode | Request | Handler | Status |
|--------|---------|---------|--------|
| 0x02 | MTU Exchange | `handle_mtu_exchange_req()` | ✅ Complete |
| 0x04 | Find Information | `handle_find_info_req()` | ✅ Complete |
| 0x06 | Find By Type Value | `handle_find_by_type_value_req()` | ✅ Complete |
| 0x08 | Read By Type | `handle_read_by_type_req()` | ✅ Complete |
| 0x0A | Read | `handle_read_req()` | ✅ Complete |
| 0x0C | Read Blob | `handle_read_blob_req()` | ✅ Complete |
| 0x10 | Read By Group Type | `handle_read_by_group_type_req()` | ✅ Complete |
| 0x12 | Write Request | `handle_write_req()` | ✅ Complete |
| 0x52 | Write Command | `handle_write_cmd()` | ✅ Complete |
| 0x16 | Prepare Write | `handle_prepare_write_req()` | 🔄 Placeholder |
| 0x18 | Execute Write | `handle_execute_write_req()` | 🔄 Placeholder |
| 0xD2 | Signed Write | `handle_signed_write_cmd()` | 🔄 Placeholder |
| 0x1E | Handle Confirm | (inline) | ✅ Complete |

### ✅ Connection Management

```cpp
struct ConnectionState {
    uint16_t conn_handle;
    uint16_t mtu;                              // Negotiated MTU
    std::map<uint16_t, uint16_t> cccd_values;  // Per-characteristic CCCD
    bool connected;
};
```

**Features:**
- Per-connection MTU tracking
- Per-connection CCCD state
- Thread-safe connection map
- Automatic cleanup on disconnect

### ✅ MTU Exchange

```cpp
// Client requests MTU of 247 bytes
handle_mtu_exchange_req(conn, pdu, len);

// Server responds with its MTU (517)
// Negotiated MTU = min(247, 517) = 247

// MTU stored in connection state
connections_[conn].mtu = 247;
```

**Supported Range:** 23-517 bytes

### ✅ Service Discovery

**Primary Service Discovery:**
```cpp
// Read By Group Type Request (0x10)
// Type: Primary Service (0x2800)
handle_read_by_group_type_req(conn, pdu, len);

// Response format:
// [opcode][length][start_handle][end_handle][uuid]...
```

**Characteristic Discovery:**
```cpp
// Read By Type Request (0x08)
// Type: Characteristic (0x2803)
handle_read_by_type_req(conn, pdu, len);

// Response format:
// [opcode][length][handle][properties|value_handle|uuid]...
```

**Descriptor Discovery:**
```cpp
// Find Information Request (0x04)
handle_find_info_req(conn, pdu, len);

// Response format (16-bit UUIDs):
// [opcode][format=0x01][handle][uuid]...
```

### ✅ Read Operations

**Simple Read:**
```cpp
// Read Request (0x0A)
handle_read_req(conn, pdu, len);

// Invokes callback or returns static value
invoke_read_callback(attr, conn, offset, out_data);

// Send response
send_read_rsp(conn, value);
```

**Long Read (Blob):**
```cpp
// Read Blob Request (0x0C) with offset
handle_read_blob_req(conn, pdu, len);

// Returns portion of value starting at offset
// Client repeats with increasing offsets until all data read
```

### ✅ Write Operations

**Write Request (with response):**
```cpp
// Write Request (0x12)
handle_write_req(conn, pdu, len);

// Invokes callback or updates static value
invoke_write_callback(attr, conn, data);

// Send Write Response (0x13)
send_write_rsp(conn);
```

**Write Command (no response):**
```cpp
// Write Command (0x52)
handle_write_cmd(conn, pdu, len);

// Writes value, but sends no response
// Faster but no error indication
```

### ✅ CCCD Management

```cpp
// Client writes CCCD to enable notifications
handle_write_req(conn, cccd_handle, {0x01, 0x00});

// Server detects CCCD write
if (attr->uuid == UUID(0x2902)) {
    handle_cccd_write(conn, cccd_handle, 0x0001);
}

// Stores per-connection CCCD state
connections_[conn].cccd_values[char_handle] = 0x0001;
```

**CCCD Values:**
- `0x0000` - Notifications and indications disabled
- `0x0001` - Notifications enabled
- `0x0002` - Indications enabled
- `0x0003` - Both enabled

### ✅ Notifications

```cpp
// Send notification to client
int result = server.notify(conn_handle, char_value_handle, data);

// Checks if notifications enabled
uint16_t cccd = connections_[conn].cccd_values[char_value_handle];
if (!(cccd & 0x0001)) {
    return -1;  // Not enabled
}

// Builds and sends ATT Handle Value Notification (0x1B)
// Format: [opcode][handle][value...]
```

**No acknowledgment required** - fire and forget

### ✅ Indications

```cpp
// Send indication to client
int result = server.indicate(conn_handle, char_value_handle, data);

// Checks if indications enabled
uint16_t cccd = connections_[conn].cccd_values[char_value_handle];
if (!(cccd & 0x0002)) {
    return -1;  // Not enabled
}

// Builds and sends ATT Handle Value Indication (0x1D)
// Format: [opcode][handle][value...]
```

**Requires acknowledgment** - client must send Handle Value Confirmation (0x1E)

### ✅ Error Handling

```cpp
void send_error_response(uint16_t conn, uint8_t req_opcode,
                        uint16_t attr_handle, uint8_t error_code);

// Format: [ATT_OP_ERROR][req_opcode][handle][error_code]
```

**Common Error Codes:**
- `0x01` - Invalid Handle
- `0x02` - Read Not Permitted
- `0x03` - Write Not Permitted
- `0x04` - Invalid PDU
- `0x05` - Insufficient Authentication
- `0x06` - Request Not Supported
- `0x07` - Invalid Offset
- `0x0A` - Attribute Not Found
- `0x0E` - Unlikely Error

---

## API Reference

### BLEGATTServer Class

```cpp
class BLEGATTServer {
public:
    /// Constructor
    explicit BLEGATTServer(std::unique_ptr<BLETransport> transport);

    /// Get attribute database
    BLEAttributeDatabase& db();

    /// Register services
    int register_services(const std::vector<GATTServiceDef>& services);

    /// Start/stop advertising
    int start_advertising(const AdvertisingParams& params);
    int stop_advertising();
    bool is_advertising() const;

    /// Run server event loop (blocking)
    int run();
    void stop();

    /// Send notification
    int notify(uint16_t conn_handle, uint16_t char_val_handle,
               const std::vector<uint8_t>& data);

    /// Send indication
    int indicate(uint16_t conn_handle, uint16_t char_val_handle,
                const std::vector<uint8_t>& data);

    /// Disconnect client
    int disconnect(uint16_t conn_handle);

    /// Get connection state
    ConnectionState* get_connection_state(uint16_t conn_handle);

    /// Callbacks
    std::function<void(uint16_t, const std::string&)> on_connected;
    std::function<void(uint16_t)> on_disconnected;
    std::function<void(uint16_t, uint16_t)> on_mtu_exchanged;
};
```

---

## Usage Examples

### Example 1: Simple Read/Write Service

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>

using namespace BLEPP;

int main() {
    // Create transport and server
    auto transport = std::make_unique<BlueZTransport>();
    BLEGATTServer server(std::move(transport));

    // Define service
    GATTServiceDef service(GATTServiceType::PRIMARY,
                          UUID("12345678-1234-5678-1234-56789abcdef0"));

    uint8_t led_state = 0;

    // Add read/write characteristic
    service.add_read_write_characteristic(
        UUID("12345678-1234-5678-1234-56789abcdef1"),
        [&](uint16_t conn, ATTAccessOp op, uint16_t offset,
            std::vector<uint8_t>& data) -> int {
            if (op == ATTAccessOp::READ_CHR) {
                data = {led_state};
                return 0;
            } else if (op == ATTAccessOp::WRITE_CHR) {
                if (data.size() != 1) {
                    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
                }
                led_state = data[0];
                std::cout << "LED: " << (led_state ? "ON" : "OFF") << std::endl;
                return 0;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
    );

    // Register service
    server.register_services({service});

    // Start advertising
    AdvertisingParams adv;
    adv.device_name = "LED Controller";
    adv.service_uuids = {service.uuid};
    server.start_advertising(adv);

    // Run server
    server.run();

    return 0;
}
```

### Example 2: Notification Service

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>
#include <thread>
#include <chrono>

using namespace BLEPP;

int main() {
    auto transport = std::make_unique<BlueZTransport>();
    BLEGATTServer server(std::move(transport));

    // Temperature service
    GATTServiceDef temp_service(GATTServiceType::PRIMARY, UUID(0x1809));

    uint16_t temp_char_handle = 0;

    // Add notify characteristic
    auto& temp_char = temp_service.add_notify_characteristic(
        UUID(0x2A1C),  // Temperature Measurement
        [](uint16_t conn, ATTAccessOp op, uint16_t offset,
           std::vector<uint8_t>& data) -> int {
            if (op == ATTAccessOp::READ_CHR) {
                // Return current temperature
                float temp = 23.5f;
                data.resize(5);
                data[0] = 0x00;  // Flags (Celsius, no timestamp)
                std::memcpy(&data[1], &temp, sizeof(float));
                return 0;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
    );
    temp_char.val_handle_ptr = &temp_char_handle;

    server.register_services({temp_service});

    // Start advertising
    AdvertisingParams adv;
    adv.device_name = "Temp Sensor";
    adv.service_uuids = {UUID(0x1809)};
    server.start_advertising(adv);

    // Track connected clients
    std::set<uint16_t> connected_clients;

    server.on_connected = [&](uint16_t conn, const std::string& addr) {
        std::cout << "Client connected: " << addr << std::endl;
        connected_clients.insert(conn);
    };

    server.on_disconnected = [&](uint16_t conn) {
        std::cout << "Client disconnected" << std::endl;
        connected_clients.erase(conn);
    };

    // Start server in thread
    std::thread server_thread([&]() {
        server.run();
    });

    // Send temperature notifications every 5 seconds
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        float temp = 20.0f + (rand() % 100) / 10.0f;
        std::vector<uint8_t> data(5);
        data[0] = 0x00;
        std::memcpy(&data[1], &temp, sizeof(float));

        for (uint16_t conn : connected_clients) {
            server.notify(conn, temp_char_handle, data);
        }
    }

    server_thread.join();
    return 0;
}
```

### Example 3: Multiple Services

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>

using namespace BLEPP;

int main() {
    auto transport = std::make_unique<BlueZTransport>();
    BLEGATTServer server(std::move(transport));

    // Battery Service
    GATTServiceDef battery(GATTServiceType::PRIMARY, UUID(0x180F));
    battery.add_read_characteristic(
        UUID(0x2A19),  // Battery Level
        [](uint16_t conn, ATTAccessOp op, uint16_t offset,
           std::vector<uint8_t>& data) -> int {
            if (op == ATTAccessOp::READ_CHR) {
                data = {85};  // 85%
                return 0;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
    );

    // Device Information Service
    GATTServiceDef device_info(GATTServiceType::PRIMARY, UUID(0x180A));
    device_info.add_read_characteristic(
        UUID(0x2A29),  // Manufacturer Name
        [](uint16_t conn, ATTAccessOp op, uint16_t offset,
           std::vector<uint8_t>& data) -> int {
            if (op == ATTAccessOp::READ_CHR) {
                std::string mfg = "Acme Corp";
                data.assign(mfg.begin(), mfg.end());
                return 0;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
    );

    // Register both services
    server.register_services({battery, device_info});

    // Start advertising with multiple service UUIDs
    AdvertisingParams adv;
    adv.device_name = "Multi-Service Device";
    adv.service_uuids = {UUID(0x180F), UUID(0x180A)};
    server.start_advertising(adv);

    server.run();
    return 0;
}
```

---

## Integration with Previous Phases

### Phase 1: Transport Layer

```cpp
// Create transport (BlueZ or ATBM)
auto transport = std::make_unique<BlueZTransport>();

// Pass to server
BLEGATTServer server(std::move(transport));

// Server uses transport for:
// - Advertising
// - Connection management
// - Data transmission
// - Event processing
```

### Phase 2: Attribute Database

```cpp
// Access database through server
BLEAttributeDatabase& db = server.db();

// Or register services (which updates database)
server.register_services(services);

// Database handles:
// - Handle allocation
// - Attribute storage
// - UUID lookups
// - CCCD auto-generation
```

### Complete Stack

```
┌─────────────────────────────────────┐
│    User Application                 │
│  (Service definitions, callbacks)   │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      BLEGATTServer (Phase 3)        │
│  - ATT request handlers             │
│  - Connection management            │
│  - Notifications/indications        │
└──────────┬──────────────┬───────────┘
           │              │
┌──────────▼──────────┐   │
│  BLEAttributeDatabase│   │
│  (Phase 2)           │   │
│  - Handle allocation │   │
│  - Attribute storage │   │
└──────────────────────┘   │
                           │
                ┌──────────▼──────────┐
                │   BLETransport      │
                │   (Phase 1)         │
                │  - BlueZ / ATBM     │
                └─────────────────────┘
```

---

## ATT Protocol State Machine

### Service Discovery Sequence

```
Client                           Server
  │                                │
  ├─ Read By Group Type (0x10) ──>│
  │  Type: Primary Service (0x2800)│
  │<───── Response ────────────────┤
  │  [Service 1: 0x0001-0x0005]   │
  │  [Service 2: 0x0006-0x000A]   │
  │                                │
  ├─ Read By Type (0x08) ────────>│
  │  Type: Characteristic (0x2803) │
  │<───── Response ────────────────┤
  │  [Char 1: properties, handle,  │
  │   UUID]                        │
  │                                │
  ├─ Find Information (0x04) ────>│
  │  Range: 0x0003-0x0005          │
  │<───── Response ────────────────┤
  │  [0x0004: CCCD (0x2902)]      │
  │  [0x0005: User Description]    │
```

### Read/Write Sequence

```
Client                           Server
  │                                │
  ├─ Read Request (0x0A) ────────>│
  │  Handle: 0x0003                │
  │                ┌───────────────┤
  │                │ invoke_read_  │
  │                │ callback()     │
  │                └───────────────┤
  │<───── Read Response (0x0B) ────┤
  │  Value: [0x42, 0x00]          │
  │                                │
  ├─ Write Request (0x12) ───────>│
  │  Handle: 0x0003                │
  │  Value: [0xFF, 0x01]          │
  │                ┌───────────────┤
  │                │ invoke_write_ │
  │                │ callback()     │
  │                └───────────────┤
  │<───── Write Response (0x13) ───┤
```

### Notification Sequence

```
Client                           Server
  │                                │
  ├─ Write Request (CCCD) ───────>│
  │  Handle: 0x0004                │
  │  Value: [0x01, 0x00] (enable) │
  │<───── Write Response ──────────┤
  │                                │
  │              (later...)        │
  │                                │
  │<─── Handle Value Notify (0x1B)┤
  │  Handle: 0x0003                │
  │  Value: [0x42]                │
  │                                │
  │  (no acknowledgment needed)    │
```

---

## Performance Considerations

### MTU Negotiation

- **Default:** 23 bytes (3 bytes overhead = 20 bytes data)
- **Maximum:** 517 bytes (3 bytes overhead = 514 bytes data)
- **Recommendation:** Always negotiate MTU for better throughput

### Notification vs Indication

| Feature | Notification | Indication |
|---------|-------------|------------|
| Acknowledgment | No | Yes |
| Overhead | Lower | Higher |
| Reliability | Best effort | Guaranteed delivery |
| Use case | Sensor data | Critical alerts |

### Connection Parameters

- **Min interval:** 7.5ms (high bandwidth)
- **Max interval:** 4s (low power)
- **Latency:** 0-499 missed events
- **Timeout:** 100ms - 32s

---

## Testing

### Unit Tests Needed

1. ATT PDU parsing
2. Error response generation
3. MTU negotiation
4. CCCD state management
5. Callback invocation

### Integration Tests Needed

1. Service discovery sequence
2. Read/write operations
3. Notification delivery
4. Multiple connections
5. Connection parameter update

### Hardware Tests Needed

1. Connect from phone (nRF Connect app)
2. Discover all services
3. Read all characteristics
4. Write characteristics
5. Enable notifications
6. Verify CCCD persistence per connection

---

## Known Limitations

- **Prepared writes:** Not fully implemented (returns error)
- **Signed writes:** No signature verification
- **Long characteristics:** Read Blob implemented, Prepare Write pending
- **Multiple simultaneous indications:** Not queued (one at a time)
- **Bonding/pairing:** Not implemented (future enhancement)

---

## Next Steps (Phase 4+)

### Phase 4: Examples and Documentation

1. Create example applications
   - Simple LED control
   - Temperature sensor with notifications
   - UART service
   - Custom services

2. Write comprehensive documentation
   - API reference
   - Migration guides
   - Best practices
   - Troubleshooting

### Phase 5: Advanced Features

1. Bonding and pairing
2. Security (encryption, authentication)
3. Connection parameter negotiation
4. Multiple advertising sets
5. Extended advertising (BLE 5.0+)

### Phase 6: Optimization

1. Zero-copy data paths
2. Connection event scheduling
3. Power management
4. Memory pool optimization

---

## Build and Run

### Compile with Server Support

```bash
./configure
make BLEPP_SERVER_SUPPORT=1
sudo make install
```

### Compile Your Application

```cpp
g++ -DBLEPP_SERVER_SUPPORT \
    my_server.cpp \
    -o my_server \
    -lble++ -lbluetooth \
    -pthread
```

### Run (requires privileges)

```bash
# Option 1: Run as root
sudo ./my_server

# Option 2: Grant capabilities
sudo setcap 'cap_net_admin,cap_net_raw+eip' ./my_server
./my_server
```

---

## References

- **Bluetooth Core Spec v5.4:** Volume 3, Part F (ATT Protocol)
- **Bluetooth Core Spec v5.4:** Volume 3, Part G (GATT Profile)
- **Phase 1 Documentation:** `PHASE1_COMPLETE.md`
- **Phase 2 Documentation:** `PHASE2_COMPLETE.md`
- **ATBM API:** `ATBM_IOCTL_API.md`
- **Build Options:** `BUILD_OPTIONS.md`

---

**Status:** Phase 3 Complete ✅

**Next Phase:** Phase 4 - Examples and Documentation

**Lines of Code:** ~1,360 lines (header + implementation)
