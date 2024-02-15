set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Configure SDK

set(toolchain aarch64-unknown-linux-gnu)
set(sdk_dir /Users/mac/Documents/hobby/OrangePi)
set(sysroot_target ${sdk_dir}/sysroot)
set(tools ${sdk_dir}/${toolchain}/bin)

# Set tools

set(CMAKE_C_COMPILER ${tools}/aarch64-unknown-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${tools}/aarch64-unknown-linux-gnu-g++)
set(CMAKE_SYSROOT ${sysroot_target})

set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/usr/lib64/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})

# configure includes

set(includes "-I${sysroot_target}/usr/include/aarch64-linux-gnu")
set(includes "${includes} -I${sysroot_target}/usr/local/include")
set(includes "${includes} -I${sysroot_target}/usr/include")
set(includes "${includes} -I${sysroot_target}/usr/include/dbus-1.0")
set(includes "${includes} -I${sysroot_target}/lib/aarch64-linux-gnu/dbus-1.0/include")
set(includes "${includes} -I${sysroot_target}/usr/include/glib-2.0")
set(includes "${includes} -I${sysroot_target}/lib/aarch64-linux-gnu/glib-2.0/include")

# configure libraries

set(libraries "-L${sysroot_target}/usr/local/lib")
set(libraries "${libraries} -L${sysroot_target}/usr/lib")
set(libraries "${libraries} -L${sysroot_target}/usr/aarch64-linux-gnu")

# configure CXX_FLAGS && C_FLAGS

set(CMAKE_CXX_FLAGS "--sysroot=${sysroot_target} ${includes}")
set(CMAKE_C_FLAGS ${CMAKE_CXX_FLAGS})

# configure linker

set(CMAKE_EXE_LINKER_FLAGS "--sysroot=${sysroot_target} ${libraries}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)