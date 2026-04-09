#include "hal/Buzzer.h"
#include "config/AppConfig.h"

#include <M5Unified.h>

namespace mesh::hal {

/// Single 50ms beep at 4000Hz — confirms that a message was sent.
void playTxTone() {
    M5.Speaker.tone(config::kTxToneHz, config::kToneDurationMs);
}

/// Single 50ms beep at 6000Hz — notifies of an incoming message.
/// Higher pitch than TX so user can tell them apart by ear.
void playRxTone() {
    M5.Speaker.tone(config::kRxToneHz, config::kToneDurationMs);
}

/// Two 100ms beeps at 2500Hz with a 150ms gap — signals deep sleep entry.
/// The double-beep pattern is distinctive enough to recognize while not
/// being alarming. Blocking: total duration ~350ms.
void playSleepTone() {
    M5.Speaker.tone(config::kSleepToneHz, config::kSleepToneDurMs);
    delay(config::kSleepToneGapMs);
    M5.Speaker.tone(config::kSleepToneHz, config::kSleepToneDurMs);
    delay(config::kSleepToneGapMs);
}

/// Three descending beeps (6000→4000→2500Hz) — signals full PMIC power-off.
/// The descending pitch conveys "powering down" intuitively.
/// Blocking: total duration ~450ms. Called right before power::powerOff().
void playPowerOffTone() {
    M5.Speaker.tone(config::kPowerOffTone1Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
    M5.Speaker.tone(config::kPowerOffTone2Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
    M5.Speaker.tone(config::kPowerOffTone3Hz, config::kPowerOffToneDurMs);
    delay(config::kPowerOffToneGapMs);
}

}  // namespace mesh::hal
