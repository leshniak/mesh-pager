#pragma once

#include <Arduino.h>

namespace mesh::config {

// LoRa power and RF switch control (via IO Expander 0)
inline constexpr uint8_t kLoraEnablePin       = GPIO_NUM_7;
inline constexpr uint8_t kLoraLnaEnablePin    = GPIO_NUM_5;
inline constexpr uint8_t kLoraAntennaSwitchPin = GPIO_NUM_6;

// LoRa SPI (directly wired GPIOs on Nesso N1)
inline constexpr uint8_t kLoraSck  = 21;
inline constexpr uint8_t kLoraMiso = 20;
inline constexpr uint8_t kLoraMosi = 22;

// Peripherals
inline constexpr uint8_t kBuzzerGpio = 11;
inline constexpr uint8_t kIrTxGpio   = 9;

}  // namespace mesh::config
