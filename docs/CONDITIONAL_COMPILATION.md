# Conditional Compilation Guide

## Overview

libblepp uses preprocessor flags to allow selective compilation of features. This enables building client-only, server-enabled, or ATBM-specific variants from the same source code with zero overhead for disabled features.

---

## Preprocessor Flags

### `BLEPP_SERVER_SUPPORT`

**Purpose:** Enable BLE GATT Server (peripheral) functionality

**Default:** Not defined (disabled)

**Dependencies:** None

**Enables:**
- BlueZ HCI/L2CAP transport (`bluez_transport.h/cc`)
- Attribute database (`bleattributedb.h/cc`)
- GATT service definitions (`gatt_services.h`)
- Transport abstraction (`bletransport.h`)

---

### `BLEPP_ATBM_SUPPORT`

**Purpose:** Enable ATBM ioctl transport

**Default:** Not defined (disabled)

**Dependencies:** Requires `BLEPP_SERVER_SUPPORT`

**Enables:**
- ATBM ioctl transport (`atbm_transport.h/cc`)
- ATBM-specific event handling
- SIGIO-based async I/O

---

## File-by-File Breakdown

### Always Compiled (Client Core)

These files are compiled in all configurations:

| File | Purpose |
|------|---------|
| `src/att.cc` | ATT protocol client |
| `src/uuid.cc` | UUID handling |
| `src/bledevice.cc` | BLE device abstraction |
| `src/att_pdu.cc` | ATT PDU parsing |
| `src/pretty_printers.cc` | Debug output |
| `src/blestatemachine.cc` | Connection state machine |
| `src/float.cc` | Float conversions |
| `src/logging.cc` | Logging framework |
| `src/lescan.cc` | LE scanning |

**Total:** ~9 object files, ~150 KB

---

### Compiled with `BLEPP_SERVER_SUPPORT`

Additional files when server support is enabled:

| File | Guards | Purpose |
|------|--------|---------|
| `blepp/bletransport.h` | `#ifdef BLEPP_SERVER_SUPPORT` | Transport interface |
| `blepp/bluez_transport.h` | `#ifdef BLEPP_SERVER_SUPPORT` | BlueZ transport header |
| `src/bluez_transport.cc` | `#ifdef BLEPP_SERVER_SUPPORT` | BlueZ implementation |
| `blepp/bleattributedb.h` | `#ifdef BLEPP_SERVER_SUPPORT` | Attribute DB header |
| `src/bleattributedb.cc` | `#ifdef BLEPP_SERVER_SUPPORT` | Attribute DB implementation |
| `blepp/gatt_services.h` | `#ifdef BLEPP_SERVER_SUPPORT` | Service definitions |

**Additional:** +2 object files, +50-80 KB

---

### Compiled with `BLEPP_ATBM_SUPPORT`

Additional files when ATBM support is enabled (requires server support):

| File | Guards | Purpose |
|------|--------|---------|
| `blepp/atbm_transport.h` | `#ifdef BLEPP_ATBM_SUPPORT` | ATBM transport header |
| `src/atbm_transport.cc` | `#ifdef BLEPP_ATBM_SUPPORT` | ATBM implementation |

**Additional:** +1 object file, +20-30 KB

---

## Makefile Integration

### Makefile.in Structure

```makefile
# Core objects (always compiled)
LIBOBJS=src/att.o src/uuid.o src/bledevice.o src/att_pdu.o \
        src/pretty_printers.o src/blestatemachine.o src/float.o \
        src/logging.o src/lescan.o

# Conditionally add server support objects
ifdef BLEPP_SERVER_SUPPORT
LIBOBJS+=src/bluez_transport.o src/bleattributedb.o
CXXFLAGS+=-DBLEPP_SERVER_SUPPORT
endif

# Conditionally add ATBM support objects
ifdef BLEPP_ATBM_SUPPORT
ifndef BLEPP_SERVER_SUPPORT
$(error BLEPP_ATBM_SUPPORT requires BLEPP_SERVER_SUPPORT to be defined)
endif
LIBOBJS+=src/atbm_transport.o
CXXFLAGS+=-DBLEPP_ATBM_SUPPORT
endif
```

### Build Commands

**Client-only (default):**
```bash
./configure
make
```

**Client + Server (BlueZ):**
```bash
./configure
make BLEPP_SERVER_SUPPORT=1
```

**Client + Server + ATBM:**
```bash
./configure
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

---

## Header Guard Pattern

### Standard Pattern

All server-related headers use this pattern:

```cpp
#ifndef __INC_BLEPP_FILENAME_H
#define __INC_BLEPP_FILENAME_H

#include <blepp/blepp_config.h>

#ifdef BLEPP_SERVER_SUPPORT

// ... header contents ...

#endif // BLEPP_SERVER_SUPPORT
#endif // __INC_BLEPP_FILENAME_H
```

### ATBM Pattern

ATBM headers have nested guards:

```cpp
#ifndef __INC_BLEPP_ATBM_TRANSPORT_H
#define __INC_BLEPP_ATBM_TRANSPORT_H

#include <blepp/blepp_config.h>

#ifdef BLEPP_ATBM_SUPPORT

// ... ATBM-specific content ...

#endif // BLEPP_ATBM_SUPPORT
#endif // __INC_BLEPP_ATBM_TRANSPORT_H
```

---

## Implementation Guard Pattern

### Source Files

```cpp
#include <blepp/blepp_config.h>

#ifdef BLEPP_SERVER_SUPPORT

#include <blepp/bluez_transport.h>
// ... other includes ...

namespace BLEPP
{
    // ... implementation ...
} // namespace BLEPP

#endif // BLEPP_SERVER_SUPPORT
```

---

## Config Header (blepp_config.h)

The central configuration header with dependency checking:

```cpp
#ifndef __INC_BLEPP_CONFIG_H
#define __INC_BLEPP_CONFIG_H

// Uncomment to enable server support
#ifndef BLEPP_SERVER_SUPPORT
// #define BLEPP_SERVER_SUPPORT
#endif

// Uncomment to enable ATBM transport
#ifndef BLEPP_ATBM_SUPPORT
// #define BLEPP_ATBM_SUPPORT
#endif

// Dependency check: ATBM requires SERVER
#ifdef BLEPP_ATBM_SUPPORT
  #ifndef BLEPP_SERVER_SUPPORT
    #error "BLEPP_ATBM_SUPPORT requires BLEPP_SERVER_SUPPORT to be enabled"
  #endif
#endif

#endif // __INC_BLEPP_CONFIG_H
```

---

## Build Matrix

| Configuration | `BLEPP_SERVER_SUPPORT` | `BLEPP_ATBM_SUPPORT` | Object Files | Size | Valid |
|---------------|------------------------|----------------------|--------------|------|-------|
| Client-only | ❌ | ❌ | 9 | ~150 KB | ✅ |
| Client + Server | ✅ | ❌ | 11 | ~200-230 KB | ✅ |
| Client + ATBM | ❌ | ✅ | - | - | ❌ Build error |
| Client + Server + ATBM | ✅ | ✅ | 12 | ~220-260 KB | ✅ |

---

## User Application Integration

### Option 1: Runtime Configuration

Define flags before including headers:

```cpp
// In your main.cpp or build system
#define BLEPP_SERVER_SUPPORT
#define BLEPP_ATBM_SUPPORT

#include <blepp/bleattributedb.h>
#include <blepp/atbm_transport.h>
```

### Option 2: Compiler Flags

```bash
g++ -DBLEPP_SERVER_SUPPORT -DBLEPP_ATBM_SUPPORT \
    myapp.cpp -o myapp -lble++ -lbluetooth
```

### Option 3: CMake

```cmake
option(USE_BLE_SERVER "Enable BLE server" ON)
option(USE_ATBM "Enable ATBM transport" OFF)

if(USE_BLE_SERVER)
    add_definitions(-DBLEPP_SERVER_SUPPORT)
endif()

if(USE_ATBM)
    if(NOT USE_BLE_SERVER)
        message(FATAL_ERROR "ATBM requires server support")
    endif()
    add_definitions(-DBLEPP_ATBM_SUPPORT)
endif()

target_link_libraries(myapp blepp bluetooth)
```

---

## Feature Detection at Runtime

### Checking Compiled Features

```cpp
#include <blepp/blepp_config.h>
#include <iostream>

void print_build_config() {
    std::cout << "libblepp build configuration:" << std::endl;

#ifdef BLEPP_SERVER_SUPPORT
    std::cout << "  Server support: ENABLED" << std::endl;
#else
    std::cout << "  Server support: DISABLED" << std::endl;
#endif

#ifdef BLEPP_ATBM_SUPPORT
    std::cout << "  ATBM transport: ENABLED" << std::endl;
#else
    std::cout << "  ATBM transport: DISABLED" << std::endl;
#endif
}
```

### Compile-Time Assertions

```cpp
#ifdef MY_APP_NEEDS_SERVER
  #ifndef BLEPP_SERVER_SUPPORT
    #error "This application requires BLEPP_SERVER_SUPPORT"
  #endif
#endif

#ifdef MY_APP_NEEDS_ATBM
  #ifndef BLEPP_ATBM_SUPPORT
    #error "This application requires BLEPP_ATBM_SUPPORT"
  #endif
#endif
```

---

## Verification Commands

### Check Compiled Symbols

```bash
# Check for server support
nm -D libble++.so | grep -i "BlueZTransport\|BLEAttributeDatabase"

# Check for ATBM support
nm -D libble++.so | grep -i "ATBMTransport"

# List all exported symbols
nm -D libble++.so | grep " T "
```

### Check Object Files

```bash
# List object files in archive
ar -t libble++.a

# Check for server objects
ar -t libble++.a | grep -E "bluez_transport|bleattributedb"

# Check for ATBM objects
ar -t libble++.a | grep "atbm_transport"
```

### Check Library Size

```bash
# Compare sizes
ls -lh libble++.so

# Client-only:    ~150 KB
# + Server:       ~200-230 KB
# + ATBM:         ~220-260 KB
```

---

## Common Errors and Solutions

### Error: "BLEPP_ATBM_SUPPORT requires BLEPP_SERVER_SUPPORT"

**Cause:** Tried to enable ATBM without server support

**Fix:**
```bash
# Wrong:
make BLEPP_ATBM_SUPPORT=1

# Correct:
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

---

### Error: "undefined reference to `BLEPP::BlueZTransport::BlueZTransport()'"

**Cause:** Application uses server features but library compiled without them

**Fix:** Recompile library with server support:
```bash
make clean
make BLEPP_SERVER_SUPPORT=1
sudo make install
```

---

### Error: "undefined reference to `BLEPP::ATBMTransport::ATBMTransport()'"

**Cause:** Application uses ATBM but library compiled without it

**Fix:** Recompile library with ATBM support:
```bash
make clean
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
sudo make install
```

---

### Warning: Including server headers in client-only build

**Symptom:** Compile-time errors or warnings about missing types

**Cause:** Included a server header (`blepp/bluez_transport.h`) without defining `BLEPP_SERVER_SUPPORT`

**Fix:** Either enable server support or use conditional includes:
```cpp
#ifdef BLEPP_SERVER_SUPPORT
#include <blepp/bluez_transport.h>
#endif
```

---

## Best Practices

### 1. Use blepp_config.h

Always include the config header first:
```cpp
#include <blepp/blepp_config.h>
#include <blepp/bledevice.h>
```

### 2. Conditional Code

Wrap server-specific code in your application:
```cpp
#ifdef BLEPP_SERVER_SUPPORT
    auto transport = std::make_unique<BLEPP::BlueZTransport>();
    transport->start_advertising(params);
#else
    std::cerr << "Server support not compiled in" << std::endl;
#endif
```

### 3. Graceful Degradation

Provide fallbacks for disabled features:
```cpp
#ifdef BLEPP_ATBM_SUPPORT
    // Prefer ATBM if available
    auto transport = std::make_unique<BLEPP::ATBMTransport>();
#elif defined(BLEPP_SERVER_SUPPORT)
    // Fall back to BlueZ
    auto transport = std::make_unique<BLEPP::BlueZTransport>();
#else
    #error "No server transport available"
#endif
```

### 4. Build Configurations

Create separate build targets:
```makefile
.PHONY: client server atbm

client:
	$(MAKE) clean
	$(MAKE)

server:
	$(MAKE) clean
	$(MAKE) BLEPP_SERVER_SUPPORT=1

atbm:
	$(MAKE) clean
	$(MAKE) BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

---

## Size Optimization

### Minimal Build (Client-Only)

For embedded systems or size-constrained environments:
```bash
make clean
make CXXFLAGS="-Os -ffunction-sections -fdata-sections"
strip --strip-unneeded libble++.so
```

Result: ~100-120 KB (optimized from ~150 KB)

### Link-Time Optimization

```bash
make clean
make CXXFLAGS="-O3 -flto" LDFLAGS="-flto"
```

---

## Testing Different Configurations

### Test Script

```bash
#!/bin/bash

echo "Testing client-only build..."
make clean && make || exit 1
./examples/lescan &
sleep 2
killall lescan

echo "Testing server build..."
make clean && make BLEPP_SERVER_SUPPORT=1 || exit 1
# Test server examples when available

echo "Testing ATBM build..."
make clean && make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1 || exit 1
# Test ATBM examples when available

echo "All configurations built successfully!"
```

---

## Related Documentation

- **Build Options:** `BUILD_OPTIONS.md` - Detailed build configuration reference
- **Server Guide:** `BLE_SERVER_IMPLEMENTATION_GUIDE.md` - Server implementation details
- **ATBM API:** `ATBM_IOCTL_API.md` - ATBM ioctl API reference
- **Phase 1:** `PHASE1_COMPLETE.md` - Transport abstraction layer
- **Phase 2:** `PHASE2_COMPLETE.md` - Attribute database

---

**Summary:** libblepp uses preprocessor guards to enable zero-overhead conditional compilation. All server and ATBM features can be compiled out completely for client-only builds.
