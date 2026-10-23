#pragma once
#include "N2kMsg.h"
// Allocate navigation tables before starting the sole N2K task. Returns false
// rather than falling back to internal RAM when PSRAM is unavailable.
bool n2k_instruments_init();
void n2k_instruments_receive(const tN2kMsg &message);
void n2k_instruments_reset();
