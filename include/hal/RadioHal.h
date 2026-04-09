#pragma once

#include "config/RadioConfig.h"
#include "protocol/MeshTypes.h"

#include <cstdint>
#include <cstddef>

namespace mesh::hal {

class RadioHal {
public:
    using RxCallback = void (*)(const uint8_t* data, size_t len);

    /// Initialize LoRa power rails, reset radio, configure parameters.
    protocol::MeshError begin(const config::RadioConfig& cfg = config::kDefaultRadioConfig);

    /// Transmit raw frame bytes.
    protocol::MeshError transmit(const uint8_t* data, size_t len);

    /// Check for received packet. Calls rxCallback if set.
    void pollRx();

    /// Put radio to sleep.
    void sleep();

    void setRxCallback(RxCallback cb) { rxCallback_ = cb; }

private:
    RxCallback rxCallback_ = nullptr;

    void setupPowerAndRfSwitches();
    void disablePowerAndRfSwitches();
};

}  // namespace mesh::hal
