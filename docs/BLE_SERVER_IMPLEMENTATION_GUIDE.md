# BLE GATT Server Implementation Guide for libblepp

## Executive Summary

This document provides a comprehensive guide for enhancing libblepp from a pure BLE client library to support BLE GATT server (peripheral/slave) functionality. The design is based on analysis of:
- **Apache NimBLE** BLE stack (used by ATBM)
- **libblepp** existing client architecture
- **BlueZ** HCI/L2CAP interfaces

The implementation uses a **hardware abstraction layer** to support both:
- Standard Linux BlueZ (HCI/L2CAP)
- ATBM-specific ioctl interface (`/dev/atbm_ioctl`)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [ATBM/NimBLE Analysis](#2-atbm-nimble-analysis)
3. [Core Components](#3-core-components)
4. [Transport Abstraction Layer](#4-transport-abstraction-layer)
5. [GATT Server Implementation](#5-gatt-server-implementation)
6. [Implementation Roadmap](#6-implementation-roadmap)
7. [Code Examples](#7-code-examples)

---

## 1. Architecture Overview

### Current libblepp (Client-Only)

```
┌─────────────────────────────────────┐
│   Application (e.g., temperature)   │
├─────────────────────────────────────┤
│  BLEGATTStateMachine (CLIENT)       │
│  - connect()                         │
│  - read_primary_services()           │
│  - send_read/write_request()         │
│  - Receives notifications            │
├─────────────────────────────────────┤
│  BLEDevice (ATT Protocol Layer)     │
│  - PDU encoding/decoding             │
│  - send_read_request()               │
│  - process_att_read_response()       │
├─────────────────────────────────────┤
│      L2CAP Socket (BlueZ)           │
│  AF_BLUETOOTH, BTPROTO_L2CAP        │
│  CID 4 (ATT Channel)                 │
└─────────────────────────────────────┘
```

### Proposed Architecture (Client + Server)

```
┌──────────────────────────────────────────────────────────┐
│   Application                                             │
│   - Client Mode: connect(), discover services            │
│   - Server Mode: register services, handle requests      │
├────────────────────┬─────────────────────────────────────┤
│ BLEGATTStateMachine│  BLEGATTServer (NEW)                │
│ (CLIENT - existing)│  - Attribute database                │
│                    │  - Handle read/write requests        │
│                    │  - Send notifications/indications    │
│                    │  - CCCD tracking                     │
├────────────────────┴─────────────────────────────────────┤
│        BLEDevice (ATT Protocol - Enhanced)               │
│        - Existing: Client PDU encode/decode              │
│        - NEW: Server PDU request handlers                │
├──────────────────────────────────────────────────────────┤
│            BLETransport (ABSTRACTION LAYER)              │
├───────────────────────┬──────────────────────────────────┤
│   BlueZTransport      │      ATBMTransport               │
│   - L2CAP sockets     │      - /dev/atbm_ioctl           │
│   - HCI advertising   │      - ATBM-specific commands    │
│   - Connection accept │      - ATBM event handling       │
└───────────────────────┴──────────────────────────────────┘
```

---

## 2. ATBM/NimBLE Analysis

### 2.1 Key Findings

The ATBM BLE implementation uses **Apache NimBLE v4.2**, a full-featured BLE stack:

**Location:** `/Users/yinzara/github/atbm-wifi/ble_host/nimble_v42/`

**Key Files:**
- `/nimble/host/src/ble_gatts.c` - GATT Server implementation
- `/nimble/host/include/host/ble_gatt.h` - GATT API definitions
- `/user_app/ble_adv_cfg/` - Example application

### 2.2 NimBLE GATT Server Architecture

#### Service Definition Structure

```c
// From: nimble_v42/nimble/host/include/host/ble_gatt.h

struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;              // Characteristic UUID
    ble_gatt_access_fn *access_cb;       // Read/write callback
    void *arg;                            // User argument
    struct ble_gatt_dsc_def *descriptors; // Descriptor array
    ble_gatt_chr_flags flags;            // Read, write, notify, indicate
    uint8_t min_key_size;                // Security requirement
    uint16_t *val_handle;                // Filled at registration time
};

struct ble_gatt_svc_def {
    uint8_t type;                         // PRIMARY or SECONDARY
    const ble_uuid_t *uuid;               // Service UUID
    const struct ble_gatt_svc_def **includes; // Included services
    const struct ble_gatt_chr_def *characteristics; // Characteristics array
};

struct ble_gatt_dsc_def {
    const ble_uuid_t *uuid;               // Descriptor UUID
    uint8_t att_flags;                    // Permission flags
    uint8_t min_key_size;                 // Security requirement
    ble_gatt_access_fn *access_cb;       // Read/write callback
    void *arg;                            // User argument
};
```

#### Access Callback

```c
// Callback type for read/write operations
typedef int ble_gatt_access_fn(uint16_t conn_handle,
                               uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt,
                               void *arg);

struct ble_gatt_access_ctxt {
    uint8_t op;                   // READ_CHR, WRITE_CHR, READ_DSC, WRITE_DSC
    struct os_mbuf *om;           // Data buffer (mbuf chain)
    union {
        const struct ble_gatt_chr_def *chr;
        const struct ble_gatt_dsc_def *dsc;
    };
};
```

### 2.3 Example Service Definition

From `/ble_host/nimble_v42/nimble/host/services/gap/src/ble_svc_gap.c`:

```c
static const struct ble_gatt_svc_def ble_svc_gap_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_UUID16),  // 0x1800
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Device Name characteristic
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME),
                .access_cb = ble_svc_gap_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {
                // Appearance characteristic
                .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_APPEARANCE),
                .access_cb = ble_svc_gap_access,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }  // Terminator
        }
    },
    { 0 }  // Terminator
};

static int ble_svc_gap_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    switch (uuid16) {
    case BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return os_mbuf_append(ctxt->om, device_name, strlen(device_name));
        } else {
            // Handle write
            return ble_hs_mbuf_to_flat(ctxt->om, device_name, len, NULL);
        }
    case BLE_SVC_GAP_CHR_UUID16_APPEARANCE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint16_t appearance = htole16(ble_svc_gap_appearance);
            return os_mbuf_append(ctxt->om, &appearance, sizeof(appearance));
        }
    }
    return BLE_ATT_ERR_UNLIKELY;
}
```

### 2.4 Attribute Database Management

From `/ble_host/nimble_v42/nimble/host/src/ble_gatts.c`:

```c
struct ble_gatts_svc_entry {
    const struct ble_gatt_svc_def *svc;
    uint16_t handle;            // Service declaration handle
    uint16_t end_group_handle;  // Last handle in service
};

struct ble_gatts_clt_cfg {
    uint16_t chr_val_handle;    // Characteristic value handle
    uint8_t flags;              // NOTIFY | INDICATE enabled
    uint8_t allowed;            // What operations are allowed
};

// Global state
static struct ble_gatts_svc_entry *ble_gatts_svc_entries;
static uint16_t ble_gatts_num_svc_entries;
static struct ble_gatts_clt_cfg *ble_gatts_clt_cfgs;  // CCCD cache
```

**Handle Allocation:**
- Services are allocated sequential handles starting from 0x0001
- Each service has: Service Declaration (UUID), Characteristics, Descriptors
- CCCD (Client Characteristic Configuration Descriptor) auto-added for notify/indicate

### 2.5 ATBM-Specific Transport Layer

**ATBM uses:** `/dev/atbm_ioctl` device for communication with BLE controller

**Key Functions Found:**
```c
// From user_app/ble_adv_cfg/main.c
hif_ioctl_init();      // Initialize ioctl interface
hif_ioctl_loop();      // Event loop for ioctl
```

**Abstraction Points:**
1. **Advertising:** `ble_gap_adv_set_data()`, `ble_gap_adv_start()`
2. **Connection:** Event-driven via ioctl callbacks
3. **Data TX/RX:** Mbuf chains sent/received via ioctl

---

## 3. Core Components

### 3.1 Reusable from libblepp

✅ **Can be reused directly:**
- `/blepp/att.h` - ATT protocol constants, opcodes, error codes
- `/blepp/att_pdu.h` - PDU encoding/decoding helpers
- `/blepp/uuid.h` - UUID handling (16-bit, 32-bit, 128-bit)
- `/src/att.cc` - Helper functions: `att_get_u16()`, `att_put_u16()`, etc.

✅ **Can be extended:**
- `/blepp/bledevice.h` - Add server-side PDU handlers
- `/blepp/blestatemachine.h` - Client state machine (keep as-is)

### 3.2 New Components Needed

❌ **Must be created:**
1. **BLEGATTServer** - Server state machine
2. **BLEAttributeDatabase** - Handle-based attribute storage
3. **BLETransport** - Hardware abstraction interface
4. **BlueZTransport** - Standard Linux BLE transport
5. **ATBMTransport** - ATBM ioctl transport

---

## 4. Transport Abstraction Layer

### 4.1 Interface Design

```cpp
// blepp/bletransport.h

namespace BLEPP {

struct AdvertisingParams {
    std::string device_name;
    std::vector<UUID> service_uuids;
    uint16_t appearance;
    uint16_t min_interval_ms;  // Advertising interval
    uint16_t max_interval_ms;
    uint8_t advertising_data[31];
    uint8_t advertising_data_len;
    uint8_t scan_response_data[31];
    uint8_t scan_response_data_len;
};

struct ConnectionParams {
    uint16_t conn_handle;
    std::string peer_address;
    uint8_t peer_address_type;
    uint16_t mtu;
};

class BLETransport {
public:
    virtual ~BLETransport() = default;

    // Advertising
    virtual int start_advertising(const AdvertisingParams& params) = 0;
    virtual int stop_advertising() = 0;

    // Connection management
    virtual int accept_connection() = 0;  // Blocking or async
    virtual int disconnect(uint16_t conn_handle) = 0;
    virtual int get_fd() const = 0;  // For select()/poll()

    // Data transmission (ATT PDUs)
    virtual int send_pdu(uint16_t conn_handle, const uint8_t* data, size_t len) = 0;
    virtual int recv_pdu(uint16_t conn_handle, uint8_t* buf, size_t len) = 0;

    // MTU
    virtual int set_mtu(uint16_t conn_handle, uint16_t mtu) = 0;
    virtual uint16_t get_mtu(uint16_t conn_handle) const = 0;

    // Callbacks
    std::function<void(const ConnectionParams&)> on_connected;
    std::function<void(uint16_t conn_handle)> on_disconnected;
    std::function<void(uint16_t conn_handle, const uint8_t* data, size_t len)> on_data_received;
};

} // namespace BLEPP
```

### 4.2 BlueZ Implementation

```cpp
// blepp/bluez_transport.h

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

namespace BLEPP {

class BlueZTransport : public BLETransport {
public:
    BlueZTransport(int hci_dev_id = 0);
    ~BlueZTransport() override;

    int start_advertising(const AdvertisingParams& params) override;
    int stop_advertising() override;
    int accept_connection() override;
    int disconnect(uint16_t conn_handle) override;
    int get_fd() const override;
    int send_pdu(uint16_t conn_handle, const uint8_t* data, size_t len) override;
    int recv_pdu(uint16_t conn_handle, uint8_t* buf, size_t len) override;
    int set_mtu(uint16_t conn_handle, uint16_t mtu) override;
    uint16_t get_mtu(uint16_t conn_handle) const override;

private:
    int hci_fd;                    // HCI device for advertising
    int l2cap_listen_fd;           // L2CAP listening socket

    struct Connection {
        int fd;
        uint16_t conn_handle;
        std::string peer_addr;
        uint16_t mtu;
    };
    std::map<uint16_t, Connection> connections_;

    int setup_advertising(const AdvertisingParams& params);
    int setup_l2cap_server();
};

} // namespace BLEPP
```

### 4.3 ATBM Implementation

```cpp
// blepp/atbm_transport.h

namespace BLEPP {

class ATBMTransport : public BLETransport {
public:
    ATBMTransport(const char* device_path = "/dev/atbm_ioctl");
    ~ATBMTransport() override;

    int start_advertising(const AdvertisingParams& params) override;
    int stop_advertising() override;
    int accept_connection() override;
    int disconnect(uint16_t conn_handle) override;
    int get_fd() const override;
    int send_pdu(uint16_t conn_handle, const uint8_t* data, size_t len) override;
    int recv_pdu(uint16_t conn_handle, uint8_t* buf, size_t len) override;
    int set_mtu(uint16_t conn_handle, uint16_t mtu) override;
    uint16_t get_mtu(uint16_t conn_handle) const override;

private:
    int ioctl_fd;
    uint16_t next_conn_handle;
    std::map<uint16_t, uint16_t> mtu_map_;

    // ATBM-specific ioctl commands
    int atbm_ioctl_cmd(int cmd, void* data, size_t len);
    void event_loop_thread();
};

} // namespace BLEPP
```

---

## 5. GATT Server Implementation

### 5.1 Attribute Database

```cpp
// blepp/bleattributedb.h

namespace BLEPP {

struct Attribute {
    uint16_t handle;
    UUID uuid;
    uint8_t permissions;  // Read, Write, Read+Encrypt, etc.
    std::vector<uint8_t> value;

    // Callbacks
    std::function<int(uint16_t conn_handle, uint16_t offset, std::vector<uint8_t>& out_data)> read_cb;
    std::function<int(uint16_t conn_handle, const std::vector<uint8_t>& data)> write_cb;
};

class BLEAttributeDatabase {
public:
    BLEAttributeDatabase();

    // Service registration (NimBLE-style)
    uint16_t add_service(uint8_t type, const UUID& uuid);
    uint16_t add_characteristic(uint16_t service_handle,
                                const UUID& uuid,
                                uint8_t properties,
                                uint8_t permissions);
    uint16_t add_descriptor(uint16_t char_handle,
                           const UUID& uuid,
                           uint8_t permissions);

    // Attribute access
    Attribute* get_attribute(uint16_t handle);
    const Attribute* get_attribute(uint16_t handle) const;
    std::vector<Attribute*> find_by_type(uint16_t start_handle,
                                         uint16_t end_handle,
                                         const UUID& type);

    // Handle management
    uint16_t allocate_handle();
    uint16_t get_next_handle() const { return next_handle_; }

private:
    std::map<uint16_t, Attribute> attributes_;
    uint16_t next_handle_;
};

} // namespace BLEPP
```

### 5.2 GATT Server State Machine

```cpp
// blepp/blegattserver.h

namespace BLEPP {

// NimBLE-compatible service definition
struct GATTCharacteristicDef {
    UUID uuid;
    uint8_t properties;  // NOTIFY, INDICATE, READ, WRITE, etc.
    uint8_t permissions;
    std::function<int(uint16_t conn_handle, uint8_t op,
                     uint16_t offset, std::vector<uint8_t>& data)> access_cb;
    uint16_t* val_handle_ptr;  // Filled at registration
};

struct GATTServiceDef {
    uint8_t type;  // PRIMARY or SECONDARY
    UUID uuid;
    std::vector<GATTCharacteristicDef> characteristics;
};

class BLEGATTServer {
public:
    BLEGATTServer(std::unique_ptr<BLETransport> transport);
    ~BLEGATTServer();

    // Service registration
    int register_services(const std::vector<GATTServiceDef>& services);

    // Advertising
    int start_advertising(const std::string& device_name,
                         const std::vector<UUID>& service_uuids,
                         uint16_t appearance = 0);
    int stop_advertising();

    // Notifications/Indications
    int notify(uint16_t conn_handle, uint16_t char_val_handle,
              const std::vector<uint8_t>& data);
    int indicate(uint16_t conn_handle, uint16_t char_val_handle,
                const std::vector<uint8_t>& data);

    // Connection management
    void set_on_connected(std::function<void(uint16_t conn_handle)> cb);
    void set_on_disconnected(std::function<void(uint16_t conn_handle)> cb);

    // Event loop
    void run();  // Blocking event loop
    int get_fd() const;  // For integration with select()/poll()
    void process_events();  // Non-blocking process

private:
    std::unique_ptr<BLETransport> transport_;
    BLEAttributeDatabase attr_db_;

    // Per-connection state
    struct ConnectionState {
        uint16_t mtu;
        std::map<uint16_t, uint16_t> cccd_values;  // char_handle -> flags
    };
    std::map<uint16_t, ConnectionState> connections_;

    // ATT request handlers
    void handle_att_mtu_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_read_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_read_by_type_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_read_by_group_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_write_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_write_cmd(uint16_t conn_handle, const uint8_t* pdu, size_t len);
    void handle_att_find_info_req(uint16_t conn_handle, const uint8_t* pdu, size_t len);

    // Response builders
    void send_att_error(uint16_t conn_handle, uint8_t opcode,
                       uint16_t handle, uint8_t error_code);
    void send_att_read_response(uint16_t conn_handle, const std::vector<uint8_t>& data);
    void send_att_write_response(uint16_t conn_handle);
};

} // namespace BLEPP
```

### 5.3 Client Characteristic Configuration (CCCD) Handling

```cpp
// Automatic CCCD management for notify/indicate characteristics

void BLEGATTServer::handle_cccd_write(uint16_t conn_handle,
                                      uint16_t cccd_handle,
                                      uint16_t value) {
    // Find the characteristic this CCCD belongs to
    uint16_t char_handle = cccd_handle - 1;  // CCCD is always +1 from char value

    auto& conn_state = connections_[conn_handle];
    conn_state.cccd_values[char_handle] = value;

    if (value & 0x0001) {
        LOG(Info, "Notifications enabled for handle " << char_handle);
    }
    if (value & 0x0002) {
        LOG(Info, "Indications enabled for handle " << char_handle);
    }
}

int BLEGATTServer::notify(uint16_t conn_handle, uint16_t char_val_handle,
                         const std::vector<uint8_t>& data) {
    // Check if client enabled notifications
    auto& conn_state = connections_[conn_handle];
    uint16_t cccd = conn_state.cccd_values[char_val_handle];

    if (!(cccd & 0x0001)) {
        return -1;  // Notifications not enabled
    }

    // Build ATT_OP_HANDLE_NOTIFY PDU
    std::vector<uint8_t> pdu;
    pdu.push_back(ATT_OP_HANDLE_NOTIFY);
    pdu.push_back(char_val_handle & 0xFF);
    pdu.push_back((char_val_handle >> 8) & 0xFF);
    pdu.insert(pdu.end(), data.begin(), data.end());

    return transport_->send_pdu(conn_handle, pdu.data(), pdu.size());
}
```

---

## 6. Implementation Roadmap

### Phase 1: Foundation (Week 1-2)

1. **Create transport abstraction**
   - Define `BLETransport` interface
   - Implement `BlueZTransport` (L2CAP server socket, HCI advertising)
   - Write unit tests for transport layer

2. **Extend ATT protocol support**
   - Add server-side PDU builders (ATT responses)
   - Implement ATT error response generation
   - Add server-specific opcodes

### Phase 2: Attribute Database (Week 2-3)

1. **Implement `BLEAttributeDatabase`**
   - Handle allocation (0x0001 - 0xFFFF)
   - Service/characteristic/descriptor storage
   - UUID-based lookups

2. **CCCD auto-generation**
   - Automatically add CCCD for notify/indicate characteristics
   - Per-connection CCCD state tracking

### Phase 3: GATT Server Core (Week 3-4)

1. **Implement `BLEGATTServer`**
   - Service registration API
   - ATT request dispatch
   - Connection state management

2. **ATT Request Handlers**
   - MTU Exchange
   - Read Request
   - Read By Type (for service/char discovery)
   - Read By Group Type (for primary service discovery)
   - Write Request/Command
   - Find Information

### Phase 4: Advertising & Connection (Week 4-5)

1. **BlueZ advertising implementation**
   - HCI LE Set Advertising Data
   - HCI LE Set Scan Response Data
   - HCI LE Set Advertising Parameters
   - HCI LE Set Advertising Enable

2. **L2CAP connection acceptance**
   - Listening socket on CID 4 (ATT)
   - Connection parameter negotiation
   - Multi-connection support

### Phase 5: ATBM Transport (Week 5-6)

1. **Implement `ATBMTransport`**
   - `/dev/atbm_ioctl` integration
   - ATBM-specific command mapping
   - Event loop for ATBM events

2. **Test with ATBM hardware**
   - Verify advertising
   - Verify connection handling
   - Verify read/write operations

### Phase 6: Examples & Documentation (Week 6-7)

1. **Create example applications**
   - Simple read/write service
   - Temperature sensor (notify)
   - UART service (write + notify)

2. **Write documentation**
   - API reference
   - Migration guide (NimBLE → libblepp)
   - Best practices

---

## 7. Code Examples

### 7.1 Simple GATT Server (BlueZ)

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>

using namespace BLEPP;

int main() {
    // Create BlueZ transport
    auto transport = std::make_unique<BlueZTransport>();

    // Create GATT server
    BLEGATTServer server(std::move(transport));

    // Define a simple read/write characteristic
    uint16_t char_value_handle;
    std::vector<uint8_t> char_value = {0x42};  // Initial value

    GATTServiceDef my_service = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID("12345678-1234-5678-1234-56789abcdef0"),
        .characteristics = {
            {
                .uuid = UUID("12345678-1234-5678-1234-56789abcdef1"),
                .properties = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .permissions = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                .access_cb = [&](uint16_t conn_handle, uint8_t op,
                                uint16_t offset, std::vector<uint8_t>& data) {
                    if (op == BLE_GATT_ACCESS_OP_READ_CHR) {
                        data = char_value;
                        return 0;
                    } else if (op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
                        char_value = data;
                        std::cout << "Characteristic written: ";
                        for (auto b : data) std::cout << std::hex << (int)b << " ";
                        std::cout << std::endl;
                        return 0;
                    }
                    return BLE_ATT_ERR_UNLIKELY;
                },
                .val_handle_ptr = &char_value_handle
            }
        }
    };

    // Register service
    server.register_services({my_service});

    // Start advertising
    server.start_advertising("MyBLEDevice", {my_service.uuid});

    // Connection callbacks
    server.set_on_connected([](uint16_t conn_handle) {
        std::cout << "Client connected: " << conn_handle << std::endl;
    });

    server.set_on_disconnected([](uint16_t conn_handle) {
        std::cout << "Client disconnected: " << conn_handle << std::endl;
    });

    // Run event loop
    std::cout << "GATT server running..." << std::endl;
    server.run();

    return 0;
}
```

### 7.2 Temperature Sensor with Notifications

```cpp
#include <blepp/blegattserver.h>
#include <blepp/bluez_transport.h>
#include <thread>
#include <chrono>

using namespace BLEPP;

// Standard Health Thermometer Service
#define HEALTH_THERMOMETER_SERVICE_UUID  0x1809
#define TEMPERATURE_MEASUREMENT_UUID     0x2A1C

int main() {
    auto transport = std::make_unique<BlueZTransport>();
    BLEGATTServer server(std::move(transport));

    uint16_t temp_char_handle;

    GATTServiceDef thermometer_service = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID((uint16_t)HEALTH_THERMOMETER_SERVICE_UUID),
        .characteristics = {
            {
                .uuid = UUID((uint16_t)TEMPERATURE_MEASUREMENT_UUID),
                .properties = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                .permissions = BLE_ATT_F_READ,
                .access_cb = [](uint16_t conn_handle, uint8_t op,
                               uint16_t offset, std::vector<uint8_t>& data) {
                    if (op == BLE_GATT_ACCESS_OP_READ_CHR) {
                        // Return current temperature (example: 36.6°C)
                        float temp = 36.6f;
                        uint32_t temp_encoded = (uint32_t)(temp * 100);
                        data = {
                            0x00,  // Flags: Celsius, no timestamp
                            (uint8_t)(temp_encoded & 0xFF),
                            (uint8_t)((temp_encoded >> 8) & 0xFF),
                            (uint8_t)((temp_encoded >> 16) & 0xFF)
                        };
                        return 0;
                    }
                    return BLE_ATT_ERR_UNLIKELY;
                },
                .val_handle_ptr = &temp_char_handle
            }
        }
    };

    server.register_services({thermometer_service});
    server.start_advertising("TempSensor", {thermometer_service.uuid}, 0x0300);

    uint16_t connected_client = 0;

    server.set_on_connected([&](uint16_t conn_handle) {
        connected_client = conn_handle;
        std::cout << "Thermometer connected" << std::endl;
    });

    server.set_on_disconnected([&](uint16_t conn_handle) {
        connected_client = 0;
        std::cout << "Thermometer disconnected" << std::endl;
    });

    // Simulate temperature readings
    std::thread temp_thread([&]() {
        float temperature = 36.5f;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            if (connected_client != 0) {
                // Simulate temperature variation
                temperature += (rand() % 10 - 5) * 0.1f;
                uint32_t temp_encoded = (uint32_t)(temperature * 100);

                std::vector<uint8_t> temp_data = {
                    0x00,
                    (uint8_t)(temp_encoded & 0xFF),
                    (uint8_t)((temp_encoded >> 8) & 0xFF),
                    (uint8_t)((temp_encoded >> 16) & 0xFF)
                };

                server.notify(connected_client, temp_char_handle, temp_data);
                std::cout << "Sent temperature: " << temperature << "°C" << std::endl;
            }
        }
    });
    temp_thread.detach();

    server.run();
    return 0;
}
```

### 7.3 ATBM Transport Example

```cpp
#include <blepp/blegattserver.h>
#include <blepp/atbm_transport.h>

using namespace BLEPP;

int main() {
    // Use ATBM transport instead of BlueZ
    auto transport = std::make_unique<ATBMTransport>("/dev/atbm_ioctl");

    BLEGATTServer server(std::move(transport));

    // Service definition is identical to BlueZ version
    // ... (same as 7.1 or 7.2)

    server.register_services({my_service});
    server.start_advertising("ATBM_Device", {my_service.uuid});
    server.run();

    return 0;
}
```

---

## 8. Testing Strategy

### 8.1 Unit Tests

```cpp
// Test attribute database
TEST(AttributeDatabase, HandleAllocation) {
    BLEAttributeDatabase db;
    uint16_t h1 = db.allocate_handle();
    uint16_t h2 = db.allocate_handle();
    ASSERT_EQ(h1, 1);
    ASSERT_EQ(h2, 2);
}

// Test service registration
TEST(GATTServer, ServiceRegistration) {
    auto transport = std::make_unique<MockTransport>();
    BLEGATTServer server(std::move(transport));

    GATTServiceDef svc = {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID(0x1800),
        .characteristics = {}
    };

    ASSERT_EQ(server.register_services({svc}), 0);
}
```

### 8.2 Integration Tests

1. **Connect with Android nRF Connect**
   - Verify service discovery
   - Test read/write operations
   - Verify notifications

2. **Connect with iOS LightBlue**
   - Cross-platform compatibility
   - Test MTU exchange

3. **Interoperability with libblepp client**
   - Use existing BLEGATTStateMachine to connect to BLEGATTServer
   - Verify end-to-end communication

---

## 9. Migration from NimBLE

For users familiar with NimBLE (like ATBM), here's a comparison:

| NimBLE | libblepp (proposed) |
|--------|---------------------|
| `ble_gatts_register_svcs()` | `server.register_services()` |
| `ble_gap_adv_start()` | `server.start_advertising()` |
| `ble_gatts_chr_updated()` | `server.notify()` or `server.indicate()` |
| `ble_gatt_access_ctxt` | Lambda callback with `conn_handle, op, data` |
| `os_mbuf` | `std::vector<uint8_t>` |

**Key Difference:** libblepp uses modern C++ (lambdas, smart pointers) instead of C callback functions.

---

## 10. Build Configuration

### 10.1 Preprocessor Flags

All server functionality is **opt-in** via preprocessor flags to minimize code size for client-only applications.

**Configuration Header:** `blepp/blepp_config.h`

```cpp
// Enable BLE GATT Server support
#define BLEPP_SERVER_SUPPORT

// Enable ATBM transport (requires BLEPP_SERVER_SUPPORT)
#define BLEPP_ATBM_SUPPORT
```

### 10.2 Compilation Examples

#### Client-Only Build (Default)

```bash
# No additional flags needed - server code excluded
g++ -o my_ble_client my_client.cpp -lblepp
```

#### Server + Client Build (BlueZ)

```bash
# Enable server support
g++ -DBLEPP_SERVER_SUPPORT -o my_ble_server my_server.cpp -lblepp -lbluetooth
```

#### Server with ATBM Support

```bash
# Enable both server and ATBM transport
g++ -DBLEPP_SERVER_SUPPORT -DBLEPP_ATBM_SUPPORT \
    -o my_atbm_server my_server.cpp -lblepp -lbluetooth
```

### 10.3 CMake Integration

```cmake
# CMakeLists.txt

option(BLEPP_SERVER "Enable BLE GATT Server support" OFF)
option(BLEPP_ATBM "Enable ATBM transport support" OFF)

if(BLEPP_SERVER)
    add_definitions(-DBLEPP_SERVER_SUPPORT)
    message(STATUS "BLE GATT Server support: ENABLED")
endif()

if(BLEPP_ATBM)
    if(NOT BLEPP_SERVER)
        message(FATAL_ERROR "BLEPP_ATBM requires BLEPP_SERVER=ON")
    endif()
    add_definitions(-DBLEPP_ATBM_SUPPORT)
    message(STATUS "ATBM transport support: ENABLED")
endif()

# Build examples
cmake -DBLEPP_SERVER=ON -DBLEPP_ATBM=OFF ..
```

### 10.4 Code Gating

All server-specific code is wrapped in conditional compilation:

```cpp
// blepp/blegattserver.h
#ifndef __INC_BLEPP_GATTSERVER_H
#define __INC_BLEPP_GATTSERVER_H

#include <blepp/blepp_config.h>

#ifdef BLEPP_SERVER_SUPPORT

namespace BLEPP {

class BLEGATTServer {
    // Server implementation
};

} // namespace BLEPP

#endif // BLEPP_SERVER_SUPPORT
#endif // __INC_BLEPP_GATTSERVER_H
```

```cpp
// blepp/atbm_transport.h
#ifndef __INC_BLEPP_ATBM_TRANSPORT_H
#define __INC_BLEPP_ATBM_TRANSPORT_H

#include <blepp/blepp_config.h>

#ifdef BLEPP_ATBM_SUPPORT

namespace BLEPP {

class ATBMTransport : public BLETransport {
    // ATBM-specific implementation
};

} // namespace BLEPP

#endif // BLEPP_ATBM_SUPPORT
#endif // __INC_BLEPP_ATBM_TRANSPORT_H
```

### 10.5 Header Organization

```
blepp/
├── blepp_config.h          # Build configuration flags
├── blestatemachine.h       # Client (always compiled)
├── lescan.h                # Scanner (always compiled)
├── bledevice.h             # ATT layer (always compiled)
│
# Server-specific headers (only if BLEPP_SERVER_SUPPORT)
├── blegattserver.h         # GATT server
├── bleattributedb.h        # Attribute database
├── bletransport.h          # Transport abstraction
├── bluez_transport.h       # BlueZ transport
│
# ATBM-specific (only if BLEPP_ATBM_SUPPORT)
└── atbm_transport.h        # ATBM ioctl transport
```

### 10.6 Library Size Impact

Approximate binary size impact (x86_64):

| Configuration | Additional Size |
|---------------|----------------|
| Client-only (default) | 0 KB (baseline) |
| + Server support | ~50-80 KB |
| + ATBM transport | ~20-30 KB |

---

## 11. References

- **BlueZ Documentation:** https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc
- **Bluetooth Core Spec 5.4:** https://www.bluetooth.com/specifications/specs/core-specification-5-4/
- **Apache NimBLE:** https://github.com/apache/mynewt-nimble
- **libblepp GitHub:** https://github.com/edrosten/libble
- **ATBM BLE Host:** `/Users/yinzara/github/atbm-wifi/ble_host`

---

## Appendix A: ATT Protocol Quick Reference

| Opcode | Name | Direction | Description |
|--------|------|-----------|-------------|
| 0x01 | ATT_OP_ERROR | Server→Client | Error response |
| 0x02 | ATT_OP_MTU_REQ | Client→Server | MTU exchange request |
| 0x03 | ATT_OP_MTU_RESP | Server→Client | MTU exchange response |
| 0x04 | ATT_OP_FIND_INFO_REQ | Client→Server | Find information (UUIDs) |
| 0x05 | ATT_OP_FIND_INFO_RESP | Server→Client | Find information response |
| 0x08 | ATT_OP_READ_BY_TYPE_REQ | Client→Server | Read by type (chars) |
| 0x09 | ATT_OP_READ_BY_TYPE_RESP | Server→Client | Read by type response |
| 0x0A | ATT_OP_READ_REQ | Client→Server | Read request |
| 0x0B | ATT_OP_READ_RESP | Server→Client | Read response |
| 0x10 | ATT_OP_READ_BY_GROUP_REQ | Client→Server | Read by group (services) |
| 0x11 | ATT_OP_READ_BY_GROUP_RESP | Server→Client | Read by group response |
| 0x12 | ATT_OP_WRITE_REQ | Client→Server | Write request |
| 0x13 | ATT_OP_WRITE_RESP | Server→Client | Write response |
| 0x52 | ATT_OP_WRITE_CMD | Client→Server | Write without response |
| 0x1B | ATT_OP_HANDLE_NOTIFY | Server→Client | Notification |
| 0x1D | ATT_OP_HANDLE_IND | Server→Client | Indication |
| 0x1E | ATT_OP_HANDLE_CNF | Client→Server | Indication confirmation |

---

**End of Document**
