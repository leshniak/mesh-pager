#pragma once

#include <Arduino.h>

// Inicjalizacja: I2C + konfiguracja ekspandera tak, aby KEY1 (E0.P0) generował IRQ na SYS_IRQ (GPIO3)
bool setupDeepSleep();

// Wejście w deep sleep z wybudzaniem EXT1 po SYS_IRQ=LOW
// (w środku czyści zaległe IRQ, ustawia wake i wchodzi w deep sleep)
[[noreturn]] void enterDeepSleep();
