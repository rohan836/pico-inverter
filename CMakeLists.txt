cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH in your environment, or pass -DPICO_SDK_PATH=...
if (NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set")
endif()

if (NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(pico_inverter C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

# Output generator:
#   default        -> PIO generator: symmetric duty, 200 us dead time, adjustable duty
#   PWM_EXACT_50   -> hardware PWM exact 50 % complementary bench build (duty locked, zero dead time)
option(PWM_EXACT_50 "Hardware PWM exact 50 % complementary bench build (duty locked)" OFF)
option(PWM_TIMER_DEADTIME "Alias for the default timer generator (kept for compatibility)" OFF)
# Bench only: timer generator with zero dead time.
option(PWM_TEST_SQUARE "Timer generator with zero dead time (bench test)" OFF)
option(PWM_SOFT_START "Use timer generator with startup duty ramp" OFF)
if (PWM_EXACT_50 AND PWM_TEST_SQUARE)
    message(FATAL_ERROR "Select only one bench waveform mode")
endif()

if (PWM_EXACT_50)
    set(WAVEFORM_SOURCE src/waveform_hw.c)
elseif (PWM_TEST_SQUARE OR PWM_SOFT_START)
    set(WAVEFORM_SOURCE src/waveform.c)
else()
    set(WAVEFORM_SOURCE src/waveform_pio.c)
endif()

add_executable(pico_inverter
    src/main.c
    ${WAVEFORM_SOURCE}
    src/ir_nec.c
    src/display.c
    src/font.c
    src/digits.c
    src/digit_data.c
    src/dashboard.c
    src/ui.c
    src/config_flash.c
)

if (WAVEFORM_SOURCE STREQUAL "src/waveform_pio.c")
    pico_generate_pio_header(pico_inverter ${CMAKE_CURRENT_LIST_DIR}/src/inverter.pio)
endif()

target_include_directories(pico_inverter PRIVATE src)

target_compile_options(pico_inverter PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(pico_inverter
    pico_stdlib
    hardware_i2c
    hardware_timer
    hardware_irq
    hardware_flash
    hardware_watchdog
    hardware_pwm
    hardware_pio
)

# Keep printf on USB serial, disable UART stdio (GP0/GP1 are the OLED I2C bus).
pico_enable_stdio_usb(pico_inverter 1)
pico_enable_stdio_uart(pico_inverter 0)

# 1.3 inch I2C OLED: SH1106 by default. Use -DDISPLAY_SSD1306=1 for SSD1306.
option(DISPLAY_SSD1306 "Use SSD1306 controller instead of SH1106" OFF)
if (DISPLAY_SSD1306)
    target_compile_definitions(pico_inverter PRIVATE DISPLAY_SSD1306=1)
endif()

if (PWM_TEST_SQUARE)
    target_compile_definitions(pico_inverter PRIVATE PWM_HW_SQUARE=0 PWM_TEST_SQUARE=1)
elseif (PWM_EXACT_50)
    target_compile_definitions(pico_inverter PRIVATE PWM_HW_SQUARE=1)
else()
    target_compile_definitions(pico_inverter PRIVATE PWM_HW_SQUARE=0)
endif()

if (PWM_SOFT_START)
    target_compile_definitions(pico_inverter PRIVATE PWM_SOFT_START=1)
endif()

# Diagnostics only: reprints the boot banner once per second.
option(FW_BANNER_LOOP "Reprint the boot banner once per second (diagnostic)" OFF)
if (FW_BANNER_LOOP)
    target_compile_definitions(pico_inverter PRIVATE FW_BANNER_LOOP=1)
endif()

if (NOT PICO_NO_PICOTOOL)
    pico_add_extra_outputs(pico_inverter)
endif()
