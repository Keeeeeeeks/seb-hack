cmake_minimum_required(VERSION 3.22)

# ---- Toolchain must be set before project() ----------------------------
# Pass via: cmake -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake ..
# or set ARM_TOOLCHAIN_DIR in environment.

project(air-synth
    VERSION 0.1.0
    LANGUAGES C ASM
)

# ---- Target MCU --------------------------------------------------------
set(MCU_FAMILY   STM32G4xx)
set(MCU_MODEL    STM32G474xx)
set(CPU_FLAGS    "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# ---- Compiler flags ----------------------------------------------------
set(COMMON_FLAGS "${CPU_FLAGS} -Wall -Wextra -fdata-sections -ffunction-sections")
set(CMAKE_C_FLAGS   "${COMMON_FLAGS} -std=gnu11")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS} -x assembler-with-cpp")

set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# ---- HAL / CMSIS paths (adjust to your SDK location) ------------------
# Expected layout:
#   ${HAL_ROOT}/Inc/          — STM32G4xx HAL headers
#   ${HAL_ROOT}/Src/          — STM32G4xx HAL sources
#   ${CMSIS_ROOT}/Include/    — CMSIS core headers
#   ${CMSIS_ROOT}/Device/ST/STM32G4xx/Include/ — device headers

if(NOT DEFINED HAL_ROOT)
    set(HAL_ROOT "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver")
endif()
if(NOT DEFINED CMSIS_ROOT)
    set(CMSIS_ROOT "${CMAKE_SOURCE_DIR}/Drivers/CMSIS")
endif()

# ---- Source files ------------------------------------------------------
set(APP_SOURCES
    Core/Src/main.c
    Core/Src/system_stm32g4xx.c
    Core/Src/stm32g4xx_it.c
    Core/Startup/startup_stm32g474xx.s
)

# HAL sources (add only what is enabled in stm32g4xx_hal_conf.h)
file(GLOB HAL_SOURCES
    "${HAL_ROOT}/Src/stm32g4xx_hal.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_cortex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_dma.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_dma_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_exti.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_flash.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_flash_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_gpio.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_i2c.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_i2c_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_pwr.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_pwr_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_rcc.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_rcc_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_tim.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_tim_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_uart.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_uart_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_dac.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_dac_ex.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_cordic.c"
    "${HAL_ROOT}/Src/stm32g4xx_hal_fmac.c"
)

# ---- Include directories -----------------------------------------------
set(INCLUDE_DIRS
    Core/Inc
    ${HAL_ROOT}/Inc
    ${CMSIS_ROOT}/Include
    ${CMSIS_ROOT}/Device/ST/STM32G4xx/Include
)

# ---- Preprocessor defines ----------------------------------------------
set(COMPILE_DEFS
    USE_HAL_DRIVER
    ${MCU_MODEL}
)

# ---- Executable --------------------------------------------------------
add_executable(${PROJECT_NAME}.elf
    ${APP_SOURCES}
    ${HAL_SOURCES}
)

target_include_directories(${PROJECT_NAME}.elf PRIVATE ${INCLUDE_DIRS})
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${COMPILE_DEFS})

# ---- Linker ------------------------------------------------------------
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/STM32G474RETx_FLASH.ld")

target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${CPU_FLAGS}
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--print-memory-usage
    --specs=nano.specs
    --specs=nosys.specs
    -lc -lm
)

set_target_properties(${PROJECT_NAME}.elf PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
)

# ---- Post-build: generate .bin and .hex --------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE}              $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "Generating ${PROJECT_NAME}.hex / .bin"
)

# ---- Flash target (OpenOCD / ST-Link) ----------------------------------
find_program(OPENOCD openocd)
if(OPENOCD)
    add_custom_target(flash
        COMMAND ${OPENOCD}
            -f interface/stlink.cfg
            -f target/stm32g4x.cfg
            -c "program ${PROJECT_NAME}.elf verify reset exit"
        DEPENDS ${PROJECT_NAME}.elf
        COMMENT "Flashing via OpenOCD + ST-Link"
    )
endif()
