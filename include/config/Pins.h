#pragma once

/// GPIO pin assignments for the Arduino Nesso N1 board.
///
/// The Nesso N1 uses an IO Expander (via I2C) for LoRa power control,
/// while the LoRa SPI bus and peripheral GPIOs are directly wired.
///
/// Note: LORA_CS, LORA_IRQ, and LORA_BUSY are defined by the board
/// support package (BSP) and are not redefined here.

#include <Arduino.h>

namespace mesh::config {

// LoRa power and RF switch control (accent via M5Stack IO Expander 0)
inline constexpr uint8_t kLoraEnablePin       = GPIO_NUM_7;   ///< LoRa module power rail enable
inline constexpr uint8_t kLoraLnaEnablePin    = GPIO_NUM_5;   ///< Low-noise amplifier enable
inline constexpr uint8_t kLoraAntennaSwitchPin = GPIO_NUM_6;  ///< Antenna path switch

// LoRa SPI bus (directly wired GPIOs on Nesso N1)
inline constexpr uint8_t kLoraSck  = 21;  ///< SPI clock
inline constexpr uint8_t kLoraMiso = 20;  ///< SPI data in (radio → MCU)
inline constexpr uint8_t kLoraMosi = 22;  ///< SPI data out (MCU → radio)

// Peripherals
inline constexpr uint8_t kBuzzerGpio = 11;  ///< PWM buzzer for audio feedback
inline constexpr uint8_t kIrTxGpio   = 9;   ///< IR transmitter (unused, set Hi-Z for sleep)

}  // namespace mesh::config
