# cmake/toolchain-armhf.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   /usr/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/arm-linux-gnueabihf-g++)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Treat /usr as the root; do NOT add /usr/lib/arm-linux-gnueabihf here
set(CMAKE_FIND_ROOT_PATH /usr)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- Critical: tell FindBoost where the ARMHF libraries are ---
set(BOOST_ROOT       /usr)
set(BOOST_INCLUDEDIR /usr/include)
set(BOOST_LIBRARYDIR /usr/lib/arm-linux-gnueabihf)

# Optional but often helpful globally for other find_* calls
set(CMAKE_LIBRARY_PATH /usr/lib/arm-linux-gnueabihf)

# pkg-config: force target .pc files
find_program(PKG_CONFIG_EXECUTABLE arm-linux-gnueabihf-pkg-config)
if(PKG_CONFIG_EXECUTABLE)
  set(PKG_CONFIG_EXECUTABLE "${PKG_CONFIG_EXECUTABLE}" CACHE FILEPATH "" FORCE)
endif()
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/")
