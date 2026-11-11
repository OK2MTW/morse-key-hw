# SPDX-License-Identifier: MIT
# Cross toolchain: GNU Arm Embedded (arm-none-eabi-gcc) on PATH or in ARM_GCC_DIR.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(DEFINED ENV{ARM_GCC_DIR})
  file(TO_CMAKE_PATH "$ENV{ARM_GCC_DIR}" _dir)
  set(_p "${_dir}/")
else()
  set(_p "")
endif()
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
endif()
set(CMAKE_C_COMPILER   ${_p}arm-none-eabi-gcc${_exe})
set(CMAKE_ASM_COMPILER ${_p}arm-none-eabi-gcc${_exe})
set(CMAKE_OBJCOPY      ${_p}arm-none-eabi-objcopy${_exe})
set(CMAKE_SIZE         ${_p}arm-none-eabi-size${_exe})
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections --specs=nano.specs --specs=nosys.specs")
