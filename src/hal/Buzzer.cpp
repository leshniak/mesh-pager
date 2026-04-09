#include "hal/Buzzer.h"
#include "config/AppConfig.h"

#include <M5Unified.h>

namespace mesh::hal {

void playTxTone() {
    M5.Speaker.tone(config::kTxToneHz, config::kToneDurationMs);
}

void playRxTone() {
    M5.Speaker.tone(config::kRxToneHz, config::kToneDurationMs);
}

void playSleepTone() {
    M5.Speaker.tone(config::kSleepToneHz, config::kSleepToneDurMs);
    delay(config::kSleepToneGapMs);
    M5.Speaker.tone(config::kSleepToneHz, config::kSleepToneDurMs);
    delay(config::kSleepToneGapMs);
}

void playPowerOffTone() {
    M5.Speaker.tone(config::kPowerOffTone1Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
    M5.Speaker.tone(config::kPowerOffTone2Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
    M5.Speaker.tone(config::kPowerOffTone3Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
}

}  // namespace mesh::hal
