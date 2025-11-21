# Build Options Reference

## Preprocessor Flags

### `BLEPP_SERVER_SUPPORT`

Enables BLE GATT Server functionality.

**Default:** Not defined (disabled)

**Effect:**
- Compiles `src/bluez_transport.cc`
- Adds server-related headers to installation
- Adds ~50-80 KB to library size

**Usage:**
```bash
./configure && make BLEPP_SERVER_SUPPORT=1
```

**CMake:**
```cmake
add_definitions(-DBLEPP_SERVER_SUPPORT)
```

**Direct compilation:**
```bash
g++ -DBLEPP_SERVER_SUPPORT -c src/bluez_transport.cc
```

---

### `BLEPP_ATBM_SUPPORT`

Enables ATBM ioctl transport support.

**Default:** Not defined (disabled)

**Requires:** `BLEPP_SERVER_SUPPORT` must be defined

**Effect:**
- Compiles `src/atbm_transport.cc`
- Adds ATBM-specific headers
- Adds ~20-30 KB to library size

**Usage:**
```bash
./configure && make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

**CMake:**
```cmake
add_definitions(-DBLEPP_SERVER_SUPPORT -DBLEPP_ATBM_SUPPORT)
```

---

## Build Configurations

### Client-Only (Default)

**Size:** Baseline (~150 KB)

**Compile:**
```bash
./configure
make
```

**Features:**
- ✅ BLE Central/Client mode
- ✅ Service discovery
- ✅ Read/Write characteristics
- ✅ Receive notifications
- ❌ BLE Peripheral/Server mode
- ❌ Advertising
- ❌ Accept connections

---

### Client + Server (BlueZ)

**Size:** ~200-230 KB (+50-80 KB)

**Compile:**
```bash
./configure
make BLEPP_SERVER_SUPPORT=1
```

**Features:**
- ✅ All client features
- ✅ BLE Peripheral/Server mode
- ✅ Advertising
- ✅ Accept connections
- ✅ Handle read/write requests
- ✅ Send notifications/indications
- ✅ BlueZ HCI/L2CAP transport
- ❌ ATBM transport

---

### Client + Server + ATBM

**Size:** ~220-260 KB (+70-110 KB)

**Compile:**
```bash
./configure
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

**Features:**
- ✅ All client + server features
- ✅ ATBM ioctl transport
- ✅ Hardware-specific optimizations

---

## Makefile Variables

### Setting Variables

**Command line:**
```bash
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

**Environment:**
```bash
export BLEPP_SERVER_SUPPORT=1
export BLEPP_ATBM_SUPPORT=1
make
```

**Makefile override:**
```makefile
# At top of Makefile or Makefile.local
BLEPP_SERVER_SUPPORT = 1
BLEPP_ATBM_SUPPORT = 1
```

---

## Configure Options (Future)

These options will be added to `configure.ac` in the future:

```bash
./configure \
    --enable-server \
    --enable-atbm \
    --with-atbm-ioctl=/dev/atbm_ioctl
```

---

## CMake Options

Example `CMakeLists.txt`:

```cmake
project(my_ble_app)

option(ENABLE_BLE_SERVER "Enable BLE server support" ON)
option(ENABLE_ATBM "Enable ATBM transport" OFF)

if(ENABLE_BLE_SERVER)
    add_definitions(-DBLEPP_SERVER_SUPPORT)
endif()

if(ENABLE_ATBM)
    if(NOT ENABLE_BLE_SERVER)
        message(FATAL_ERROR "ATBM requires server support")
    endif()
    add_definitions(-DBLEPP_ATBM_SUPPORT)
endif()

find_library(BLEPP_LIB blepp REQUIRED)
find_library(BLUETOOTH_LIB bluetooth REQUIRED)

add_executable(my_server main.cpp)
target_link_libraries(my_server ${BLEPP_LIB} ${BLUETOOTH_LIB})
```

**Build:**
```bash
cmake -DENABLE_BLE_SERVER=ON -DENABLE_ATBM=OFF ..
make
```

---

## Verification

### Check Compiled Features

```bash
# Check for server symbols
nm -D libble++.so | grep -i "bluez\|transport"

# Check for ATBM symbols
nm -D libble++.so | grep -i "atbm"
```

### Runtime Check

```cpp
#ifdef BLEPP_SERVER_SUPPORT
    std::cout << "Server support: ENABLED" << std::endl;
#else
    std::cout << "Server support: DISABLED" << std::endl;
#endif

#ifdef BLEPP_ATBM_SUPPORT
    std::cout << "ATBM support: ENABLED" << std::endl;
#else
    std::cout << "ATBM support: DISABLED" << std::endl;
#endif
```

---

## Dependencies

### Client-Only
- `libbluetooth.so` (BlueZ 5.0+)

### Server (BlueZ)
- `libbluetooth.so` (BlueZ 5.0+)
- Root privileges or `CAP_NET_ADMIN` + `CAP_NET_RAW` capabilities

### Server (ATBM)
- ATBM driver loaded
- `/dev/atbm_ioctl` device accessible
- Appropriate permissions on ioctl device

---

## Examples by Configuration

### Client-Only Example
```bash
cd examples
g++ lescan.cc -o lescan -lblepp
./lescan
```

### Server Example (when implemented)
```bash
cd examples
g++ -DBLEPP_SERVER_SUPPORT server_example.cc -o server -lblepp -lbluetooth
sudo ./server  # Requires elevated permissions
```

### ATBM Example (when implemented)
```bash
cd examples
g++ -DBLEPP_SERVER_SUPPORT -DBLEPP_ATBM_SUPPORT atbm_server.cc -o atbm_server -lblepp
./atbm_server  # May require device permissions
```

---

## Troubleshooting

### "undefined reference to BlueZTransport"

**Cause:** Trying to use server features without defining `BLEPP_SERVER_SUPPORT`

**Fix:**
```bash
g++ -DBLEPP_SERVER_SUPPORT your_code.cpp -lblepp
```

### "BLEPP_ATBM_SUPPORT requires BLEPP_SERVER_SUPPORT"

**Cause:** Defining `BLEPP_ATBM_SUPPORT` without `BLEPP_SERVER_SUPPORT`

**Fix:**
```bash
make BLEPP_SERVER_SUPPORT=1 BLEPP_ATBM_SUPPORT=1
```

### "Failed to open HCI device"

**Cause:** Missing permissions

**Fix:**
```bash
# Add capabilities
sudo setcap 'cap_net_admin,cap_net_raw+eip' ./your_binary

# Or run as root
sudo ./your_binary
```

### "/dev/atbm_ioctl: No such file or directory"

**Cause:** ATBM driver not loaded

**Fix:**
```bash
# Load ATBM driver
sudo modprobe atbm_wifi_ble

# Check device
ls -l /dev/atbm_ioctl
```
