#pragma once

/// Hardware abstraction for the SX1262 LoRa radio.
///
/// Manages the LoRa radio lifecycle: power rail control via IO Expander,
/// RadioLib configuration, interrupt-driven RX, and blocking TX.
///
/// RX flow (interrupt-driven):
///   1. Radio DIO1 interrupt fires when a packet is received
///   2. ISR sets a volatile flag (non-blocking)
///   3. pollRx() checks the flag in the main loop, reads the packet, calls rxCallback
///   4. Radio returns to RX mode immediately after reading
///
/// TX flow (blocking):
///   1. transmit() sends the frame via RadioLib (blocks until TX complete)
///   2. Radio returns to RX mode automatically after TX
///
/// Power control (Nesso N1 specific):
///   The LoRa module power rail and RF switches are controlled via the M5Stack
///   IO Expander. These must be enabled before radio init and disabled for sleep.

#include "config/RadioConfig.h"
#include "protocol/MeshTypes.h"

#include <cstdint>
#include <cstddef>

namespace mesh::hal {

class RadioHal {
public:
    using RxCallback = void (*)(const uint8_t* data, size_t len);

    /// Initialize LoRa: enable power rails, configure radio parameters, start RX.
    protocol::MeshError begin(const config::RadioConfig& cfg = config::kDefaultRadioConfig);

    /// Transmit raw frame bytes (blocking). Returns to RX mode after TX completes.
    protocol::MeshError transmit(const uint8_t* data, size_t len);

    /// Poll for received packets. Non-blocking — calls rxCallback if a packet is ready.
    /// Must be called frequently in the main loop.
    void pollRx();

    /// Put radio and power rails to sleep for deep sleep entry.
    void sleep();

    void setRxCallback(RxCallback cb) { rxCallback_ = cb; }

private:
    RxCallback rxCallback_ = nullptr;

    void setupPowerAndRfSwitches();
    void disablePowerAndRfSwitches();
};

}  // namespace mesh::hal
