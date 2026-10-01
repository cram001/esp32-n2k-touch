#include "n2k_input.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_service.hpp"

namespace {
struct Packet { uint32_t generation; ActisenseMessage message; };
SemaphoreHandle_t mutex = nullptr;
QueueHandle_t queue = nullptr;
N2kInputConfig config;
N2kInputStatus status;
uint32_t generation = 0;

bool same(const N2kInputConfig &a, const N2kInputConfig &b) {
    return a.mode == b.mode && a.ip == b.ip && a.port == b.port;
}
void set_status(const char *text, int error = 0) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    std::snprintf(status.message.data(), status.message.size(), "%s", text);
    status.socket_error = error;
    xSemaphoreGive(mutex);
}
void task(void *) {
    int socket_fd = -1;
    bool connecting = false;
    uint32_t active_generation = UINT32_MAX, retry_at = 0, connect_at = 0, last_data = 0, connection_received = 0;
    ActisenseAsciiDecoder decoder;
    Packet packet{};
    char bytes[512];
    for (;;) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        const N2kInputConfig current = config;
        const uint32_t gen = generation;
        xSemaphoreGive(mutex);
        const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        const WifiState wifi = wifi_service_get_status().state;
        const bool network = wifi == WifiState::Connected || wifi == WifiState::AccessPoint;
        if (gen != active_generation || !network || current.mode != N2kInputMode::W2kTcp) {
            if (socket_fd >= 0) close(socket_fd);
            socket_fd = -1; connecting = false; retry_at = now;
            decoder.reset(); active_generation = gen;
        }
        if (current.mode != N2kInputMode::W2kTcp) set_status("Wired CAN input");
        else if (!n2k_endpoint_valid(current.ip.data(), current.port)) set_status("Enter a valid gateway IP and port");
        else if (!network) set_status("Waiting for Wi-Fi");
        else {
            if (socket_fd < 0 && static_cast<int32_t>(now - retry_at) >= 0) {
                sockaddr_in address{};
                address.sin_family = AF_INET; address.sin_port = htons(current.port);
                inet_pton(AF_INET, current.ip.data(), &address.sin_addr);
                socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (socket_fd >= 0 && fcntl(socket_fd, F_SETFL, O_NONBLOCK) == 0) {
                    const int result = connect(socket_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
                    if (result == 0 || errno == EINPROGRESS) {
                        connecting = result != 0; connect_at = now; last_data = now;
                        decoder.reset(); connection_received = 0; set_status(connecting ? "Connecting to W2K-1..." : "Connected; waiting for N2K ASCII");
                    } else { const int error = errno; close(socket_fd); socket_fd = -1; set_status("Connection failed; retrying", error); }
                } else {
                    const int error = errno;
                    if (socket_fd >= 0) close(socket_fd);
                    socket_fd = -1; set_status("Socket setup failed; retrying", error);
                }
                retry_at = now + 5000;
            }
            if (socket_fd >= 0 && connecting) {
                fd_set writes, errors; FD_ZERO(&writes); FD_ZERO(&errors);
                FD_SET(socket_fd, &writes); FD_SET(socket_fd, &errors);
                timeval timeout{};
                const int ready = select(socket_fd + 1, nullptr, &writes, &errors, &timeout);
                int error = 0; socklen_t length = sizeof(error);
                if (ready < 0) error = errno;
                else if (ready > 0 && getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0) error = errno;
                else if (!ready && now - connect_at > 5000) error = ETIMEDOUT;
                if (error) {
                    close(socket_fd); socket_fd = -1; connecting = false;
                    retry_at = now + 5000; set_status("Connection failed; retrying", error);
                } else if (ready > 0) { connecting = false; last_data = now; set_status("Connected; waiting for N2K ASCII"); }
            }
            if (socket_fd >= 0 && !connecting) {
                const int count = recv(socket_fd, bytes, sizeof(bytes), 0);
                if (count > 0) {
                    last_data = now;
                    const uint32_t rejected_before = decoder.rejected();
                    for (int i = 0; i < count; ++i) {
                        if (!decoder.feed(bytes[i], packet.message)) continue;
                        ++connection_received;
                        packet.generation = gen;
                        const bool queued = xQueueSend(queue, &packet, 0) == pdTRUE;
                        xSemaphoreTake(mutex, portMAX_DELAY);
                        ++status.received; if (!queued) ++status.dropped;
                        xSemaphoreGive(mutex);
                    }
                    xSemaphoreTake(mutex, portMAX_DELAY);
                    status.rejected += decoder.rejected() - rejected_before;
                    std::snprintf(status.message.data(), status.message.size(), "%s",
                        connection_received ? "Connected; receiving N2K ASCII" :
                        decoder.rejected() ? "Check gateway format: N2K ASCII" : "Connected; waiting for N2K ASCII");
                    xSemaphoreGive(mutex);
                } else if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    const int error = count == 0 ? 0 : errno;
                    close(socket_fd); socket_fd = -1; retry_at = now + 5000;
                    set_status("Gateway disconnected; retrying", error);
                } else if (now - last_data > 5000) set_status("Connected; no recent gateway data");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}
bool n2k_input_start(const N2kInputConfig &initial) {
    if (mutex) { n2k_input_apply(initial); return true; }
    mutex = xSemaphoreCreateMutex(); queue = xQueueCreate(16, sizeof(Packet));
    if (!mutex || !queue) {
        if (mutex) vSemaphoreDelete(mutex);
        if (queue) vQueueDelete(queue);
        mutex = nullptr; queue = nullptr; return false;
    }
    config = initial;
    if (xTaskCreate(task, "w2k_input", 4096, nullptr, 4, nullptr) == pdPASS) return true;
    vSemaphoreDelete(mutex); vQueueDelete(queue); mutex = nullptr; queue = nullptr;
    return false;
}
void n2k_input_apply(const N2kInputConfig &next) {
    if (!mutex) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!same(config, next)) { config = next; ++generation; status = {}; }
    xSemaphoreGive(mutex);
}
bool n2k_input_receive(ActisenseMessage &out) {
    if (!queue) return false;
    Packet packet;
    // Bounded drain; stale packets from a previous endpoint never reach instruments.
    for (unsigned i = 0; i < 16 && xQueueReceive(queue, &packet, 0) == pdTRUE; ++i) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        const bool current = packet.generation == generation && config.mode == N2kInputMode::W2kTcp;
        xSemaphoreGive(mutex);
        if (current) { out = packet.message; return true; }
    }
    return false;
}
N2kInputStatus n2k_input_status() {
    if (!mutex) return {};
    xSemaphoreTake(mutex, portMAX_DELAY); const N2kInputStatus copy = status; xSemaphoreGive(mutex);
    return copy;
}
