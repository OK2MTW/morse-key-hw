# SPDX-License-Identifier: MIT
# CW Key firmware for WeAct Black Pill (STM32F411CE, STM32F401Cx), TinyUSB + STM32CubeF4 HAL.
#   python tools/get_deps.py                     (once: pinned third-party sources)
#   cmake -G Ninja -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
#   cmake --build build                          -> build/cwkey-f411.bin, build/cwkey-f401.bin (application at 0x08010000)
# Usually run through build.ps1 / package.ps1.
cmake_minimum_required(VERSION 3.20)
project(cwkey C ASM)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
foreach(dep tinyusb cmsis_device_f4 stm32f4xx_hal_driver cmsis_core)
  if(NOT EXISTS ${TP}/${dep})
    message(FATAL_ERROR "${TP}/${dep} missing - run: python tools/get_deps.py")
  endif()
endforeach()

set(HAL ${TP}/stm32f4xx_hal_driver)
set(DEV ${TP}/cmsis_device_f4)

set(APP_SOURCES
  src/main.c src/board.c src/keyer.c src/settings.c src/protocol.c src/storage.c src/usb_descriptors.c)

set(HAL_SOURCES
  ${DEV}/Source/Templates/system_stm32f4xx.c
  ${HAL}/Src/stm32f4xx_hal.c
  ${HAL}/Src/stm32f4xx_hal_cortex.c
  ${HAL}/Src/stm32f4xx_hal_rcc.c
  ${HAL}/Src/stm32f4xx_hal_rcc_ex.c
  ${HAL}/Src/stm32f4xx_hal_gpio.c
  ${HAL}/Src/stm32f4xx_hal_flash.c
  ${HAL}/Src/stm32f4xx_hal_flash_ex.c
  ${HAL}/Src/stm32f4xx_hal_pwr.c)

set(TUSB_SOURCES
  ${TP}/tinyusb/tusb.c
  ${TP}/tinyusb/common/tusb_fifo.c
  ${TP}/tinyusb/device/usbd.c
  ${TP}/tinyusb/class/hid/hid_device.c
  ${TP}/tinyusb/class/midi/midi_device.c
  ${TP}/tinyusb/portable/synopsys/dwc2/dcd_dwc2.c
  ${TP}/tinyusb/portable/synopsys/dwc2/dwc2_common.c)

# name  device-define  startup               RAM
set(CHIPS "f411|STM32F411xE|startup_stm32f411xe.s|128K" "f401|STM32F401xE|startup_stm32f401xe.s|96K")

foreach(chip ${CHIPS})
  string(REPLACE "|" ";" c ${chip})
  list(GET c 0 name)
  list(GET c 1 define)
  list(GET c 2 startup)
  list(GET c 3 ram)
  set(target cwkey-${name})

  set(RAM_SIZE ${ram})
  configure_file(linker/cwkey.ld.in ${CMAKE_CURRENT_BINARY_DIR}/${target}.ld @ONLY)

  add_executable(${target} ${APP_SOURCES} ${HAL_SOURCES} ${TUSB_SOURCES} ${DEV}/Source/Templates/gcc/${startup})
  target_include_directories(${target} PRIVATE
    src ${TP}/tinyusb ${TP}/cmsis_core/CMSIS/Core/Include ${DEV}/Include ${HAL}/Inc)
  target_compile_definitions(${target} PRIVATE ${define} HSE_VALUE=25000000U USE_HAL_DRIVER)
  if(CWKEY_DIAG)
    target_compile_definitions(${target} PRIVATE CWKEY_DIAG)   # start-up log in flash (src/diag.h)
  endif()
  target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-unused-parameter -std=gnu11>)
  target_link_options(${target} PRIVATE -T${CMAKE_CURRENT_BINARY_DIR}/${target}.ld -Wl,-Map=${target}.map)
  set_target_properties(${target} PROPERTIES SUFFIX ".elf")

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${CMAKE_CURRENT_BINARY_DIR}/${target}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
    VERBATIM)
endforeach()
