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

enum class LocalServerStage : uint8_t { NotStarted, Starting, Listening, StartFailed, RoutesFailed };
struct LocalServerStatus {
    LocalServerStage stage=LocalServerStage::NotStarted;
    int error=0;
};
LocalServerStatus ota_local_server_status();
const char *ota_local_server_stage_name(LocalServerStage stage);
void ota_log_local_server_status();

// Call once during boot. If the bootloader marked this image pending verification,
// this confirms it as healthy and prevents automatic rollback.
void ota_prepare();
void ota_confirm_running_image();
void ota_reject_running_image();
bool ota_partition_layout_valid();
bool ota_start_local_server();

// Starts an HTTPS OTA update in a background FreeRTOS task.
// Requires an already-working network connection. Returns false if an update is
// already active or the URL is invalid.
bool ota_start_https(const char *url);

OtaStatus ota_get_status();
const char *ota_running_version();

// Serialize network reconfiguration with the two OTA transports.
bool ota_update_in_progress();
bool ota_begin_network_change();
void ota_end_network_change();
