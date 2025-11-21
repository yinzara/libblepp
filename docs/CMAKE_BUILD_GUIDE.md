# CMake Build Guide

## Overview

libblepp now supports CMake for building the library with optional server and ATBM transport support. This guide covers all CMake build configurations and options.

---

## Quick Start

### Client-Only Build (Default)

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```

### Server Support Build

```bash
mkdir build
cd build
cmake -DWITH_SERVER_SUPPORT=ON ..
make
sudo make install
```

### Server + ATBM Support Build

```bash
mkdir build
cd build
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON ..
make
sudo make install
```

---

## CMake Options

### `WITH_EXAMPLES`

**Description:** Build example applications

**Default:** `OFF`

**Usage:**
```bash
cmake -DWITH_EXAMPLES=ON ..
```

---

### `WITH_SERVER_SUPPORT`

**Description:** Enable BLE GATT server (peripheral) functionality

**Default:** `OFF`

**Effect:**
- Compiles `src/bluez_transport.cc`
- Compiles `src/bleattributedb.cc`
- Compiles `src/blegattserver.cc`
- Adds `-DBLEPP_SERVER_SUPPORT` to compiler flags
- Links with `pthread` library
- Installs server headers

**Usage:**
```bash
cmake -DWITH_SERVER_SUPPORT=ON ..
```

---

### `WITH_ATBM_SUPPORT`

**Description:** Enable ATBM ioctl transport

**Default:** `OFF`

**Requires:** `WITH_SERVER_SUPPORT=ON`

**Effect:**
- Compiles `src/atbm_transport.cc`
- Adds `-DBLEPP_ATBM_SUPPORT` to compiler flags
- Installs ATBM headers

**Usage:**
```bash
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON ..
```

**Error if server not enabled:**
```
CMake Error: ATBM support requires server support to be enabled
```

---

## Build Configurations

### Configuration Matrix

| Configuration | Command | Headers | Objects | Size | Features |
|---------------|---------|---------|---------|------|----------|
| **Client-only** | `cmake ..` | 12 | 9 | ~150 KB | Client only |
| **+ Server** | `cmake -DWITH_SERVER_SUPPORT=ON ..` | 17 | 12 | ~230 KB | Client + Server (BlueZ) |
| **+ ATBM** | `cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON ..` | 18 | 13 | ~260 KB | Client + Server + ATBM |
| **+ Examples** | `cmake -DWITH_EXAMPLES=ON ..` | - | - | - | Build examples too |

---

## Build Types

### Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

**Features:**
- Debug symbols included
- No optimization
- Larger binaries
- Better for debugging

### Release Build (Default)

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

**Features:**
- Optimized for performance
- Smaller binaries
- No debug symbols
- Production-ready

---

## Installation Paths

### Default Install Locations

On Linux:
- Libraries: `/usr/local/lib/libble++.so`
- Headers: `/usr/local/include/blepp/`
- pkg-config: `/usr/local/lib/pkgconfig/libblepp.pc`

### Custom Install Prefix

```bash
cmake -DCMAKE_INSTALL_PREFIX=/opt/libblepp ..
make
sudo make install
```

Results in:
- `/opt/libblepp/lib/libble++.so`
- `/opt/libblepp/include/blepp/`

---

## Complete Build Examples

### Example 1: Client + Server + Examples

```bash
mkdir build && cd build
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_EXAMPLES=ON ..
make -j$(nproc)
sudo make install
```

### Example 2: Debug Build with Server

```bash
mkdir build-debug && cd build-debug
cmake -DCMAKE_BUILD_TYPE=Debug -DWITH_SERVER_SUPPORT=ON ..
make
```

### Example 3: ATBM Build with Custom Prefix

```bash
mkdir build && cd build
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_SERVER_SUPPORT=ON \
  -DWITH_ATBM_SUPPORT=ON \
  -DCMAKE_INSTALL_PREFIX=$HOME/opt/libblepp \
  ..
make -j$(nproc)
make install  # No sudo needed for user prefix
```

### Example 4: Cross-Compilation for ARM

```bash
mkdir build-arm && cd build-arm
cmake \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-toolchain.cmake \
  -DWITH_SERVER_SUPPORT=ON \
  ..
make -j$(nproc)
```

---

## Using libblepp in Your CMake Project

### Method 1: Find Package (After Installation)

```cmake
cmake_minimum_required(VERSION 3.4)
project(my_ble_app)

# Find the installed library
find_library(BLEPP_LIB ble++ REQUIRED)
find_path(BLEPP_INCLUDE blepp REQUIRED)

# Check for server support
try_compile(HAS_SERVER_SUPPORT
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_server.cpp
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${BLEPP_INCLUDE}"
    LINK_LIBRARIES ${BLEPP_LIB})

if(HAS_SERVER_SUPPORT)
    add_definitions(-DBLEPP_SERVER_SUPPORT)
    message(STATUS "libblepp has server support")
endif()

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${BLEPP_INCLUDE})
target_link_libraries(my_app ${BLEPP_LIB} bluetooth pthread)
```

### Method 2: pkg-config

```cmake
cmake_minimum_required(VERSION 3.4)
project(my_ble_app)

find_package(PkgConfig REQUIRED)
pkg_check_modules(BLEPP REQUIRED libblepp)

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${BLEPP_INCLUDE_DIRS})
target_link_libraries(my_app ${BLEPP_LIBRARIES})
```

### Method 3: Add as Subdirectory

```cmake
cmake_minimum_required(VERSION 3.4)
project(my_ble_app)

# Add libblepp as subdirectory
set(WITH_SERVER_SUPPORT ON CACHE BOOL "" FORCE)
add_subdirectory(external/libblepp)

add_executable(my_app main.cpp)
target_link_libraries(my_app ble++)
```

---

## Verification

### Check Build Configuration

After building, verify what was compiled:

```bash
# Check for server symbols
nm -D libble++.so | grep -i "BLEGATTServer\|BlueZTransport"

# Check for ATBM symbols
nm -D libble++.so | grep -i "ATBMTransport"

# List all headers that will be installed
find blepp -name "*.h" -type f
```

### Check Preprocessor Defines

Create a test file:

```cpp
// test_config.cpp
#include <blepp/blepp_config.h>
#include <iostream>

int main() {
#ifdef BLEPP_SERVER_SUPPORT
    std::cout << "Server support: YES" << std::endl;
#else
    std::cout << "Server support: NO" << std::endl;
#endif

#ifdef BLEPP_ATBM_SUPPORT
    std::cout << "ATBM support: YES" << std::endl;
#else
    std::cout << "ATBM support: NO" << std::endl;
#endif
    return 0;
}
```

Compile and run:
```bash
g++ -I/usr/local/include test_config.cpp -o test_config
./test_config
```

---

## Out-of-Source Builds

It's recommended to use out-of-source builds:

```bash
# Good: Build directory separate from source
mkdir build
cd build
cmake ..
make

# Bad: In-source build
cd libblepp
cmake .
make
```

Multiple build configurations:

```bash
# Client-only
mkdir build-client && cd build-client
cmake ..
make

# Server
mkdir build-server && cd build-server
cmake -DWITH_SERVER_SUPPORT=ON ..
make

# ATBM
mkdir build-atbm && cd build-atbm
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON ..
make
```

---

## Cleaning Builds

### Clean build artifacts

```bash
cd build
make clean
```

### Complete rebuild

```bash
rm -rf build
mkdir build
cd build
cmake ..
make
```

### Clean and rebuild specific target

```bash
cd build
make clean
make ble++
```

---

## Advanced CMake Options

### Compiler Selection

```bash
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ..
```

### Additional Compiler Flags

```bash
cmake -DCMAKE_CXX_FLAGS="-Wall -Wextra -O3" ..
```

### Verbose Build

```bash
make VERBOSE=1
```

Or:

```bash
cmake -DCMAKE_VERBOSE_MAKEFILE=ON ..
make
```

### Parallel Build

```bash
make -j$(nproc)  # Use all CPU cores
make -j4         # Use 4 cores
```

---

## Troubleshooting

### "Bluez not found"

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install libbluetooth-dev

# Fedora/RHEL
sudo dnf install bluez-libs-devel

# Arch
sudo pacman -S bluez-libs
```

### "ATBM requires server support"

**Cause:** Enabled ATBM without enabling server support

**Solution:**
```bash
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON ..
```

### "undefined reference to pthread_create"

**Cause:** Server support needs pthread but not linked

**Solution:** This should be automatic, but if it fails:
```bash
cmake -DWITH_SERVER_SUPPORT=ON ..
# If that doesn't work:
cmake -DWITH_SERVER_SUPPORT=ON -DCMAKE_CXX_FLAGS="-pthread" ..
```

### Headers not found after install

**Cause:** Non-standard install prefix

**Solution:**
```bash
# Add to pkg-config path
export PKG_CONFIG_PATH=/opt/libblepp/lib/pkgconfig:$PKG_CONFIG_PATH

# Or use full path
g++ -I/opt/libblepp/include myapp.cpp -L/opt/libblepp/lib -lble++
```

---

## Example CMakeLists.txt for Applications

### Simple Application

```cmake
cmake_minimum_required(VERSION 3.4)
project(ble_app)

set(CMAKE_CXX_STANDARD 11)

# Find libblepp
find_library(BLEPP ble++ REQUIRED)
find_package(Threads REQUIRED)

# Optional: Check for server support
option(USE_SERVER "Use server features" ON)
if(USE_SERVER)
    add_definitions(-DBLEPP_SERVER_SUPPORT)
endif()

add_executable(ble_app main.cpp)
target_link_libraries(ble_app ${BLEPP} bluetooth Threads::Threads)
```

### Application with Subdirectory

```cmake
cmake_minimum_required(VERSION 3.4)
project(ble_app)

# Build options
option(BUILD_SERVER "Build with server support" ON)

# Configure libblepp
set(WITH_SERVER_SUPPORT ${BUILD_SERVER} CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/libblepp)

# Your application
add_executable(ble_app
    src/main.cpp
    src/my_service.cpp
    src/my_service.h)

target_link_libraries(ble_app ble++)

if(BUILD_SERVER)
    target_compile_definitions(ble_app PRIVATE BLEPP_SERVER_SUPPORT)
endif()
```

---

## Comparison: Make vs CMake

| Feature | Makefile | CMake |
|---------|----------|-------|
| **Build Options** | `make BLEPP_SERVER_SUPPORT=1` | `cmake -DWITH_SERVER_SUPPORT=ON` |
| **Installation** | `make install` | `cmake .. && make install` |
| **Clean** | `make clean` | `make clean` or `rm -rf build` |
| **Parallel** | `make -j4` | `make -j4` |
| **Cross-compile** | Manual | Toolchain files |
| **IDE Support** | Limited | Excellent |
| **Windows** | Difficult | Native |

---

## IDE Integration

### CLion

1. Open libblepp directory in CLion
2. CMake options appear automatically
3. Set options in `File → Settings → Build → CMake`
4. Add profiles for different configurations

### Visual Studio Code

1. Install CMake Tools extension
2. Open libblepp directory
3. Select configure preset:
   - `Ctrl+Shift+P` → "CMake: Configure"
4. Build: `Ctrl+Shift+P` → "CMake: Build"

### Qt Creator

1. `File → Open File or Project`
2. Select `CMakeLists.txt`
3. Configure build settings
4. Build and run

---

## Packaging

### Create DEB Package

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DWITH_SERVER_SUPPORT=ON ..
make -j$(nproc)
make install DESTDIR=../debian/libblepp
# Create DEBIAN/control file
dpkg-deb --build debian/libblepp
```

### Create RPM Package

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DWITH_SERVER_SUPPORT=ON ..
make -j$(nproc)
make install DESTDIR=$HOME/rpmbuild/BUILDROOT/libblepp-1.2-1.x86_64
# Create RPM spec file
rpmbuild -ba libblepp.spec
```

---

## Related Documentation

- **Build Options:** `BUILD_OPTIONS.md` - Makefile-based build reference
- **Conditional Compilation:** `CONDITIONAL_COMPILATION.md` - Preprocessor flags guide
- **Phase 1 Complete:** `PHASE1_COMPLETE.md` - Transport layer
- **Phase 2 Complete:** `PHASE2_COMPLETE.md` - Attribute database
- **Phase 3 Complete:** `PHASE3_COMPLETE.md` - GATT server

---

## Summary

**Default build (client-only):**
```bash
cmake .. && make && sudo make install
```

**Server support:**
```bash
cmake -DWITH_SERVER_SUPPORT=ON .. && make && sudo make install
```

**Full build:**
```bash
cmake -DWITH_SERVER_SUPPORT=ON -DWITH_ATBM_SUPPORT=ON -DWITH_EXAMPLES=ON .. && make
```
