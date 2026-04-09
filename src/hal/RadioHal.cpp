#include "hal/RadioHal.h"
#include "config/Pins.h"

#include <M5Unified.h>
#include <RadioLib.h>

namespace mesh::hal {

static SX1262 radio = new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY);

static volatile bool rxPacketReady = false;
static void IRAM_ATTR onDio1Rx() {
    rxPacketReady = true;
}

void RadioHal::setupPowerAndRfSwitches() {
    using namespace mesh::config;
    auto& io = M5.getIOExpander(0);

    io.digitalWrite(kLoraEnablePin, false);
    delay(100);
    io.digitalWrite(kLoraEnablePin, true);
    delay(100);

    io.digitalWrite(kLoraLnaEnablePin, true);
    io.digitalWrite(kLoraAntennaSwitchPin, true);
}

void RadioHal::disablePowerAndRfSwitches() {
    using namespace mesh::config;
    auto& io = M5.getIOExpander(0);

    io.digitalWrite(kLoraEnablePin, false);
    io.digitalWrite(kLoraLnaEnablePin, false);
    io.digitalWrite(kLoraAntennaSwitchPin, false);
}

protocol::MeshError RadioHal::begin(const config::RadioConfig& cfg) {
    setupPowerAndRfSwitches();

    if (radio.begin() != RADIOLIB_ERR_NONE) {
        return protocol::MeshError::RadioInitFailed;
    }

    radio.setDio2AsRfSwitch(true);
    radio.setFrequency(cfg.frequencyMHz);
    radio.setBandwidth(cfg.bandwidthKHz);
    radio.setSpreadingFactor(cfg.spreadingFactor);
    radio.setCodingRate(cfg.codingRate);
    radio.setSyncWord(cfg.syncWord);
    radio.setPreambleLength(cfg.preambleLength);
    radio.setTCXO(cfg.tcxoVoltage);
    radio.setOutputPower(cfg.outputPowerDbm);
    radio.setCurrentLimit(cfg.currentLimitMa);

    radio.setDio1Action(onDio1Rx);
    radio.startReceive();

    return protocol::MeshError::Ok;
}

protocol::MeshError RadioHal::transmit(const uint8_t* data, size_t len) {
    const int result = radio.transmit(const_cast<uint8_t*>(data), len);
    if (result != RADIOLIB_ERR_NONE) {
        return protocol::MeshError::RadioTxFailed;
    }
    radio.startReceive();
    return protocol::MeshError::Ok;
}

void RadioHal::pollRx() {
    if (!rxPacketReady) return;
    rxPacketReady = false;

    const int packetLen = radio.getPacketLength();
    if (packetLen <= 0 || packetLen > static_cast<int>(protocol::kRxBufferLen)) {
        radio.startReceive();
        return;
    }

    uint8_t buf[protocol::kRxBufferLen];
    if (radio.readData(buf, packetLen) != RADIOLIB_ERR_NONE) {
        radio.startReceive();
        return;
    }

    if (rxCallback_) {
        rxCallback_(buf, static_cast<size_t>(packetLen));
    }

    radio.startReceive();
}

void RadioHal::sleep() {
    radio.sleep();
    disablePowerAndRfSwitches();
}

}  // namespace mesh::hal
