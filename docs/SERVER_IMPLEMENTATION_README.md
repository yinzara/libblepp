# BLE Server Implementation for libblepp

## Quick Start

This enhancement adds BLE GATT Server (peripheral/slave) functionality to libblepp, which currently only supports client mode.

### Key Features

- ✅ **Hardware-agnostic design** via transport abstraction layer
- ✅ **Dual transport support**: Standard BlueZ HCI/L2CAP + ATBM ioctl
- ✅ **NimBLE-compatible API** for easy migration from ATBM
- ✅ **Optional compilation** via preprocessor flags (zero overhead when disabled)
- ✅ **Modern C++** with lambdas and smart pointers

---

## Documentation

📖 **[Full Implementation Guide](BLE_SERVER_IMPLEMENTATION_GUIDE.md)** - Comprehensive 1000+ line guide with:
- ATBM/NimBLE analysis
- Architecture design
- Code examples
- Implementation roadmap
- Testing strategy

---

## Build Configuration

### Preprocessor Flags

```cpp
// blepp/blepp_config.h

#define BLEPP_SERVER_SUPPORT    // Enable GATT server
#define BLEPP_ATBM_SUPPORT      // Enable ATBM transport (requires SERVER)
```

### Compilation

```bash
# Client-only (default - no flags needed)
g++ my_client.cpp -lblepp

# Server with BlueZ
g++ -DBLEPP_SERVER_SUPPORT my_server.cpp -lblepp -lbluetooth

# Server with ATBM
g++ -DBLEPP_SERVER_SUPPORT -DBLEPP_ATBM_SUPPORT my_atbm_server.cpp -lblepp
```

---

## Example: Simple Read/Write Service

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>

using namespace BLEPP;

int main() {
    auto transport = std::make_unique<BlueZTransport>();
    BLEGATTServer server(std::move(transport));

    uint16_t char_handle;
    std::vector<uint8_t> value = {0x42};

    GATTServiceDef service = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID("12345678-1234-5678-1234-56789abcdef0"),
        .characteristics = {
            {
                .uuid = UUID("12345678-1234-5678-1234-56789abcdef1"),
                .properties = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .permissions = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                .access_cb = [&](uint16_t conn, uint8_t op, uint16_t offset,
                                std::vector<uint8_t>& data) {
                    if (op == BLE_GATT_ACCESS_OP_READ_CHR) {
                        data = value;
                        return 0;
                    } else if (op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                        value = data;
                        return 0;
                    }
                    return BLE_ATT_ERR_UNLIKELY;
                },
                .val_handle_ptr = &char_handle
            }
        }
    };

    server.register_services({service});
    server.start_advertising("MyDevice", {service.uuid});

    server.set_on_connected([](uint16_t conn) {
        std::cout << "Client connected" << std::endl;
    });

    server.run();
    return 0;
}
```

---

## Architecture

```
┌──────────────────────────────────────────────┐
│   Application                                 │
├───────────────────┬──────────────────────────┤
│ BLEGATTStateMachine│  BLEGATTServer (NEW)    │
│ (Client - existing)│  - Attribute database    │
│                    │  - Request handlers      │
│                    │  - Notify/Indicate       │
├────────────────────┴──────────────────────────┤
│        BLEDevice (ATT Protocol)               │
├───────────────────────────────────────────────┤
│            BLETransport (Abstraction)         │
├──────────────────────┬────────────────────────┤
│  BlueZTransport      │  ATBMTransport         │
│  (L2CAP/HCI)         │  (/dev/atbm_ioctl)     │
└──────────────────────┴────────────────────────┘
```

---

## Implementation Status

- ✅ **Phase 0:** Analysis and design (COMPLETE)
- ⬜ **Phase 1:** Transport abstraction layer
- ⬜ **Phase 2:** Attribute database
- ⬜ **Phase 3:** GATT server core
- ⬜ **Phase 4:** BlueZ transport
- ⬜ **Phase 5:** ATBM transport
- ⬜ **Phase 6:** Examples and tests

See [Implementation Roadmap](BLE_SERVER_IMPLEMENTATION_GUIDE.md#6-implementation-roadmap) for details.

---

## Key Components

| Component | Purpose | Status |
|-----------|---------|--------|
| `BLETransport` | Hardware abstraction interface | To implement |
| `BlueZTransport` | Standard Linux BLE (HCI/L2CAP) | To implement |
| `ATBMTransport` | ATBM ioctl interface | To implement |
| `BLEAttributeDatabase` | GATT attribute storage | To implement |
| `BLEGATTServer` | Server state machine | To implement |
| `blepp_config.h` | Build configuration | ✅ Created |

---

## Migration from NimBLE

| NimBLE | libblepp |
|--------|----------|
| `ble_gatts_register_svcs()` | `server.register_services()` |
| `ble_gap_adv_start()` | `server.start_advertising()` |
| `ble_gatts_chr_updated()` | `server.notify()` |
| `ble_gatt_access_ctxt` | Lambda with `conn_handle, op, data` |
| `os_mbuf` | `std::vector<uint8_t>` |

---

## Testing

### Unit Tests
- Attribute database handle allocation
- Service registration
- PDU encoding/decoding

### Integration Tests
- Connect with **nRF Connect** (Android)
- Connect with **LightBlue** (iOS)
- Interop with libblepp client

---

## References

- **Full Guide:** [BLE_SERVER_IMPLEMENTATION_GUIDE.md](BLE_SERVER_IMPLEMENTATION_GUIDE.md)
- **ATBM Source:** `/Users/yinzara/github/atbm-wifi/ble_host`
- **NimBLE Docs:** https://github.com/apache/mynewt-nimble
- **Bluetooth Spec:** https://www.bluetooth.com/specifications/specs/

---

## Contributing

See the [Implementation Roadmap](BLE_SERVER_IMPLEMENTATION_GUIDE.md#6-implementation-roadmap) for where to start.

Recommended order:
1. Implement `BLETransport` interface
2. Implement `BlueZTransport`
3. Implement `BLEAttributeDatabase`
4. Implement `BLEGATTServer`
5. Add examples
6. Add tests

---

## License

Same as libblepp (GPLv2)
