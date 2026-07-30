# # Embedded C++ Temperature Control Loop Using Electrofan

This project implements an embedded temperature control system using C++ on an STM32F407 microcontroller. The system monitors temperature through a sensor and controls an electrofan using a closed-loop control algorithm to maintain the desired temperature level.

The firmware is developed using STM32Cube HAL drivers, providing hardware abstraction for peripherals such as ADC, PWM, GPIO, and timers. The STM32F407 board is used as the main control platform, taking advantage of its ARM Cortex-M4 core for real-time processing and efficient control execution.

## Features
- Real-time temperature monitoring
- Closed-loop temperature regulation
- Electrofan speed control using PWM
- STM32Cube HAL-based peripheral management
- Embedded C++ firmware architecture
- Timer and ADC peripheral configuration
- Modular driver-based software design

## Hardware
- STM32F407 development board
- Temperature sensor
- Electrofan
- PWM-controlled fan driver circuit

## Software
- Language: C++
- Framework: STM32Cube HAL
- IDE: STM32CubeIDE
- MCU: STM32F407 (ARM Cortex-M4)


