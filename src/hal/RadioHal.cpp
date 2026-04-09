#include "hal/RadioHal.h"
#include "config/Pins.h"

#include <M5Unified.h>
#include <RadioLib.h>

namespace mesh::hal {

// RadioLib SX1262 instance. The Module constructor takes:
// CS pin, IRQ (DIO1) pin, reset pin (NC = not connected), busy pin.
static SX1262 radio = new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY);

// Interrupt flag set by DIO1 ISR when a packet arrives.
// Volatile because it's written in ISR context and read in main loop.
static volatile bool rxPacketReady = false;
static void IRAM_ATTR onDio1Rx() {
    rxPacketReady = true;
}

void RadioHal::setupPowerAndRfSwitches() {
    using namespace mesh::config;
    auto& io = M5.getIOExpander(0);

    // Power-cycle the LoRa module to ensure clean state
    io.digitalWrite(kLoraEnablePin, false);
    delay(100);
    io.digitalWrite(kLoraEnablePin, true);
    delay(100);

    // Enable LNA (low-noise amplifier) and antenna switch
    io.digitalWrite(kLoraLnaEnablePin, true);
    io.digitalWrite(kLoraAntennaSwitchPin, true);
}

void RadioHal::disablePowerAndRfSwitches() {
    using namespace mesh::config;
    auto& io = M5.getIOExpander(0);

    // Cut power to LoRa module and disable RF path for deep sleep
    io.digitalWrite(kLoraEnablePin, false);
    io.digitalWrite(kLoraLnaEnablePin, false);
    io.digitalWrite(kLoraAntennaSwitchPin, false);
}

protocol::MeshError RadioHal::begin(const config::RadioConfig& cfg) {
    setupPowerAndRfSwitches();

    if (radio.begin() != RADIOLIB_ERR_NONE) {
        return protocol::MeshError::RadioInitFailed;
    }

    // Configure radio parameters to match Meshtastic channel preset
    radio.setDio2AsRfSwitch(true);  // SX1262: use DIO2 to control RF switch
    radio.setFrequency(cfg.frequencyMHz);
    radio.setBandwidth(cfg.bandwidthKHz);
    radio.setSpreadingFactor(cfg.spreadingFactor);
    radio.setCodingRate(cfg.codingRate);
    radio.setSyncWord(cfg.syncWord);
    radio.setPreambleLength(cfg.preambleLength);
    radio.setTCXO(cfg.tcxoVoltage);
    radio.setOutputPower(cfg.outputPowerDbm);
    radio.setCurrentLimit(cfg.currentLimitMa);

    // Start continuous RX with DIO1 interrupt on packet arrival
    radio.setDio1Action(onDio1Rx);
    radio.startReceive();

    return protocol::MeshError::Ok;
}

protocol::MeshError RadioHal::transmit(const uint8_t* data, size_t len) {
    // Blocking TX — radio switches from RX to TX, sends the frame, then we
    // manually restart RX mode. During TX, incoming packets are missed.
    const int result = radio.transmit(const_cast<uint8_t*>(data), len);
    if (result != RADIOLIB_ERR_NONE) {
        return protocol::MeshError::RadioTxFailed;
    }
    radio.startReceive();  // return to RX immediately after TX
    return protocol::MeshError::Ok;
}

void RadioHal::pollRx() {
    if (!rxPacketReady) return;
    rxPacketReady = false;

    // Read the received packet from the radio's FIFO
    const int packetLen = radio.getPacketLength();
    if (packetLen <= 0 || packetLen > static_cast<int>(protocol::kRxBufferLen)) {
        radio.startReceive();  // discard oversized packets, keep listening
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

    radio.startReceive();  // restart RX for next packet
}

void RadioHal::sleep() {
    radio.sleep();
    disablePowerAndRfSwitches();
}

}  // namespace mesh::hal
