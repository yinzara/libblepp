# Phase 2 Complete: Attribute Database

## Summary

Phase 2 of the BLE GATT Server implementation is now complete. The attribute database provides a structured way to define and manage GATT services, characteristics, and descriptors with automatic handle allocation and CCCD generation.

---

## Files Created

### Headers (`blepp/`)

1. **`bleattributedb.h`** - Attribute database class
   - `BLEAttributeDatabase` - Core database management
   - `Attribute` structure - Individual attribute storage
   - `AttributeType` enum - Service, characteristic, descriptor types
   - `ATTAccessOp` enum - Read/write operations
   - Permission and property flags

2. **`gatt_services.h`** - NimBLE-compatible service definitions
   - `GATTServiceDef` - Service definition structure
   - `GATTCharacteristicDef` - Characteristic definition
   - `GATTDescriptorDef` - Descriptor definition
   - `GATTAccessCallback` - Unified callback type
   - Helper functions for common service patterns

### Implementation (`src/`)

3. **`bleattributedb.cc`** - Attribute database implementation (~550 lines)
   - Handle allocation (0x0001-0xFFFF)
   - Service registration from definitions
   - Automatic CCCD generation
   - UUID-based attribute lookups
   - Characteristic value storage
   - Read/write callback management

---

## Key Features

### ✅ Handle Management

Automatic sequential handle allocation:
```cpp
BLEAttributeDatabase db;
uint16_t svc_handle = db.add_primary_service(UUID(0x1800));  // Handle 1
uint16_t char_handle = db.add_characteristic(...);            // Handles 2, 3
// Characteristic uses 2 handles: declaration + value
```

### ✅ CCCD Auto-Generation

Client Characteristic Configuration Descriptors automatically added:
```cpp
// If characteristic has NOTIFY or INDICATE...
db.add_characteristic(svc_handle, uuid,
                     GATT_CHR_PROP_NOTIFY,  // <-- CCCD auto-added
                     ATT_PERM_READ);
// Result: Char declaration (handle N), value (N+1), CCCD (N+2)
```

### ✅ NimBLE-Compatible API

Familiar service definition pattern:
```cpp
GATTServiceDef service(GATTServiceType::PRIMARY, UUID(0x180F));

service.add_read_characteristic(UUID(0x2A19),  // Battery level
    [](uint16_t conn, ATTAccessOp op, uint16_t offset,
       std::vector<uint8_t>& data) -> int {
        if (op == ATTAccessOp::READ_CHR) {
            data = {85};  // 85% battery
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
);

db.register_services({service});
```

### ✅ Flexible Value Storage

Three ways to handle characteristic values:

**1. Static values:**
```cpp
db.set_characteristic_value(char_handle, {0x42, 0x00});
```

**2. Dynamic read callback:**
```cpp
db.set_read_callback(char_handle,
    [](uint16_t conn, uint16_t offset, std::vector<uint8_t>& out) {
        out = get_current_sensor_data();
        return 0;
    }
);
```

**3. Combined in service definition:**
```cpp
service.add_characteristic(uuid, flags,
    [](uint16_t conn, ATTAccessOp op, uint16_t offset,
       std::vector<uint8_t>& data) -> int {
        if (op == ATTAccessOp::READ_CHR) {
            data = read_value();
        } else if (op == ATTAccessOp::WRITE_CHR) {
            write_value(data);
        }
        return 0;
    }
);
```

### ✅ Efficient Lookups

Multiple query methods:
```cpp
// By handle
auto attr = db.get_attribute(handle);

// By type (e.g., find all primary services)
auto services = db.find_by_type(0x0001, 0xFFFF, UUID(0x2800));

// By range
auto attrs = db.get_range(0x0010, 0x0020);

// By type and value
auto matching = db.find_by_type_value(0x0001, 0xFFFF,
                                      UUID(0x2800),
                                      {0x0F, 0x18});  // Battery Service
```

---

## Usage Examples

### Example 1: Simple Read-Only Service

```cpp
#include <blepp/bleattributedb.h>
#include <blepp/gatt_services.h>

BLEAttributeDatabase db;

// Device Information Service
GATTServiceDef dis(GATTServiceType::PRIMARY, UUID(0x180A));

dis.add_read_characteristic(UUID(0x2A29),  // Manufacturer Name
    [](uint16_t conn, ATTAccessOp op, uint16_t offset,
       std::vector<uint8_t>& data) -> int {
        if (op == ATTAccessOp::READ_CHR) {
            std::string manufacturer = "Acme Corp";
            data.assign(manufacturer.begin(), manufacturer.end());
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
);

db.register_services({dis});
```

### Example 2: Read/Write with Persistence

```cpp
// LED Control Service
GATTServiceDef led_service(GATTServiceType::PRIMARY,
                           UUID("12345678-1234-5678-1234-56789abcdef0"));

uint8_t led_state = 0;  // Off

led_service.add_read_write_characteristic(
    UUID("12345678-1234-5678-1234-56789abcdef1"),
    [&led_state](uint16_t conn, ATTAccessOp op, uint16_t offset,
                std::vector<uint8_t>& data) -> int {
        if (op == ATTAccessOp::READ_CHR) {
            data = {led_state};
            return 0;
        } else if (op == ATTAccessOp::WRITE_CHR) {
            if (data.size() != 1 || data[0] > 1) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            led_state = data[0];
            // Actually control LED hardware here
            set_led(led_state);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
);

db.register_services({led_service});
```

### Example 3: Notify with CCCD

```cpp
// Temperature Sensor with Notifications
GATTServiceDef temp_service(GATTServiceType::PRIMARY, UUID(0x1809));

uint16_t temp_value_handle;

temp_service.add_notify_characteristic(UUID(0x2A1C),  // Temperature
    [](uint16_t conn, ATTAccessOp op, uint16_t offset,
       std::vector<uint8_t>& data) -> int {
        if (op == ATTAccessOp::READ_CHR) {
            float temp = get_temperature();
            // Encode as IEEE-11073 float
            data.resize(5);
            data[0] = 0x00;  // Flags
            memcpy(&data[1], &temp, 4);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
).val_handle_ptr = &temp_value_handle;

db.register_services({temp_service});

// CCCD automatically added at temp_value_handle + 1
```

### Example 4: Using Helper Functions

```cpp
// Quick read-only service
auto device_name_svc = create_read_only_service(
    UUID(0x1800),  // GAP Service
    UUID(0x2A00),  // Device Name characteristic
    {'M', 'y', 'D', 'e', 'v', 'i', 'c', 'e'}
);

db.register_services({device_name_svc});

// Read/write service with lambdas
std::string current_ssid;

auto wifi_config_svc = create_read_write_service(
    UUID("aaaa0000-1234-5678-1234-56789abcdef0"),
    UUID("aaaa0001-1234-5678-1234-56789abcdef0"),
    // Read function
    [&current_ssid]() {
        return std::vector<uint8_t>(current_ssid.begin(), current_ssid.end());
    },
    // Write function
    [&current_ssid](const std::vector<uint8_t>& data) {
        current_ssid = std::string(data.begin(), data.end());
        connect_to_wifi(current_ssid);
    }
);

db.register_services({wifi_config_svc});
```

---

## Database Structure

### Handle Allocation

```
Handle  Type                    UUID        Value
------  ----------------------  ----------  -------------------------
0x0001  Primary Service Decl    0x2800      0x180F (Battery Service)
0x0002  Characteristic Decl     0x2803      Props|0x0003|0x2A19
0x0003  Characteristic Value    0x2A19      [battery level data]
0x0004  Primary Service Decl    0x2800      0x1809 (Health Thermo)
0x0005  Characteristic Decl     0x2803      Props|0x0006|0x2A1C
0x0006  Characteristic Value    0x2A1C      [temperature data]
0x0007  CCCD (auto-added)       0x2902      0x0000 (disabled)
...
```

### Service End Group Handles

Each service tracks its range:
```cpp
Service at 0x0001: end_group_handle = 0x0003
Service at 0x0004: end_group_handle = 0x0007
```

This enables efficient service discovery.

---

## Testing

### Manual Testing

```cpp
#include <cassert>

BLEAttributeDatabase db;

// Test handle allocation
uint16_t h1 = db.allocate_handle();
uint16_t h2 = db.allocate_handle();
assert(h1 == 1);
assert(h2 == 2);

// Test service addition
uint16_t svc = db.add_primary_service(UUID(0x1800));
assert(svc > 0);
assert(db.size() == 1);

// Test characteristic addition
uint16_t chr = db.add_characteristic(svc, UUID(0x2A00),
                                     GATT_CHR_PROP_READ,
                                     ATT_PERM_READ);
assert(chr > 0);
assert(db.size() == 3);  // Service + char decl + char value

// Test CCCD auto-add
uint16_t chr_notify = db.add_characteristic(svc, UUID(0x2A01),
                                            GATT_CHR_PROP_NOTIFY,
                                            ATT_PERM_READ);
assert(db.size() == 6);  // +3 for char, +1 for CCCD

// Test lookups
auto attr = db.get_attribute(svc);
assert(attr != nullptr);
assert(attr->type == AttributeType::PRIMARY_SERVICE);

auto services = db.find_by_type(0x0001, 0xFFFF, UUID(0x2800));
assert(services.size() == 1);
```

---

## Integration with Phase 1

The attribute database integrates with the transport layer:

```cpp
#include <blepp/bluez_transport.h>
#include <blepp/bleattributedb.h>
#include <blepp/gatt_services.h>

// Create transport
auto transport = std::make_unique<BlueZTransport>();

// Create attribute database
BLEAttributeDatabase db;

// Define services
GATTServiceDef service(...);
db.register_services({service});

// Start advertising (Phase 1)
AdvertisingParams params;
params.device_name = "MyServer";
params.service_uuids = {service.uuid};
transport->start_advertising(params);

// In Phase 3, we'll add the GATT server that ties these together
```

---

## Next Steps (Phase 3)

### GATT Server State Machine

The next phase will implement the actual server:

1. **`blegattserver.h`** - GATT server header
   - Request handlers (read, write, find info, etc.)
   - Response builders
   - Notification/indication management
   - Connection state tracking

2. **`blegattserver.cc`** - GATT server implementation
   - ATT PDU dispatch
   - MTU exchange handling
   - Error response generation
   - CCCD tracking per connection

See [BLE_SERVER_IMPLEMENTATION_GUIDE.md](BLE_SERVER_IMPLEMENTATION_GUIDE.md#phase-3-gatt-server-core-week-3-4) for details.

---

## API Reference Summary

### BLEAttributeDatabase

```cpp
class BLEAttributeDatabase {
public:
    // Service management
    int register_services(const std::vector<GATTServiceDef>& services);
    uint16_t add_primary_service(const UUID& uuid);
    uint16_t add_secondary_service(const UUID& uuid);
    uint16_t add_characteristic(uint16_t svc, const UUID& uuid,
                                uint8_t props, uint8_t perms);
    uint16_t add_descriptor(uint16_t char_handle, const UUID& uuid,
                            uint8_t perms);

    // Attribute access
    Attribute* get_attribute(uint16_t handle);
    std::vector<const Attribute*> find_by_type(...);
    std::vector<const Attribute*> get_range(...);

    // Value management
    int set_characteristic_value(uint16_t handle, const std::vector<uint8_t>& value);
    std::vector<uint8_t> get_characteristic_value(uint16_t handle) const;
    int set_read_callback(uint16_t handle, ...);
    int set_write_callback(uint16_t handle, ...);

    // Utilities
    uint16_t get_next_handle() const;
    size_t size() const;
    void clear();
};
```

### GATTServiceDef

```cpp
struct GATTServiceDef {
    GATTServiceType type;
    UUID uuid;
    std::vector<GATTCharacteristicDef> characteristics;

    // Helper methods
    GATTCharacteristicDef& add_characteristic(UUID, flags, callback);
    GATTCharacteristicDef& add_read_characteristic(UUID, callback);
    GATTCharacteristicDef& add_read_write_characteristic(UUID, callback);
    GATTCharacteristicDef& add_notify_characteristic(UUID, callback);
    GATTCharacteristicDef& add_indicate_characteristic(UUID, callback);
};
```

---

## Known Limitations

- No support for long characteristics (>MTU) yet (coming in Phase 3)
- No support for Characteristic Extended Properties descriptor
- No support for Characteristic User Description
- No bonding/pairing data persistence
- Maximum 65534 handles (standard BLE limit)

---

## Documentation

- **Implementation Guide:** [BLE_SERVER_IMPLEMENTATION_GUIDE.md](BLE_SERVER_IMPLEMENTATION_GUIDE.md)
- **Phase 1 Summary:** [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md)
- **Build Options:** [BUILD_OPTIONS.md](BUILD_OPTIONS.md)

---

**Status:** Phase 2 Complete ✅
**Next Phase:** GATT Server Core (Phase 3)
**Estimated Completion:** ~1-2 weeks
