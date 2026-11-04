# Cross compilation for 64-bit Windows with llvm-mingw (in the build image:
# /opt/llvm-mingw). Qt for Windows is given by CMAKE_PREFIX_PATH, the host
# tools of the same Qt version (moc, uic, rcc, lrelease) by QT_HOST_PATH.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW)
    set(LLVM_MINGW /opt/llvm-mingw)
endif()
set(TRIPLE x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-clang)
set(CMAKE_CXX_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-clang++)
set(CMAKE_RC_COMPILER ${LLVM_MINGW}/bin/${TRIPLE}-windres)

# The Qt prefix is a root too, otherwise its packages are looked for under
# the toolchain sysroot only.
set(CMAKE_FIND_ROOT_PATH ${LLVM_MINGW}/${TRIPLE} ${CMAKE_PREFIX_PATH})
# Programs come from the host, libraries, headers and packages only from the
# target roots.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
