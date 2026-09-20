#pragma once

#include <cstddef>
#include <cstdint>

enum class OtaState : uint8_t {
    Idle = 0,
    Starting,
    Downloading,
    Validating,
    ReadyToReboot,
    Failed,
};

struct OtaStatus {
    OtaState state = OtaState::Idle;
    int progress_percent = 0;
    int last_error = 0;
    char message[64]{};
};

// Call once during boot. If the bootloader marked this image pending verification,
// this confirms it as healthy and prevents automatic rollback.
void ota_confirm_running_image();

// Starts an HTTPS OTA update in a background FreeRTOS task.
// Requires an already-working network connection. Returns false if an update is
// already active or the URL is invalid.
bool ota_start_https(const char *url);

OtaStatus ota_get_status();
const char *ota_running_version();
