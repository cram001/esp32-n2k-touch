#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
using gpio_num_t = int;
constexpr int GPIO_NUM_5=5, GPIO_NUM_4=4, GPIO_NUM_NC=-1;
using BaseType_t=int;
constexpr int pdFALSE=0, pdTRUE=1;
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
#define portENTER_CRITICAL_ISR(m) (m)->lock()
#define portEXIT_CRITICAL_ISR(m) (m)->unlock()
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_TIMEOUT=2;
inline const char *esp_err_to_name(int) { return "fake"; }
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
struct twai_frame_t {
 struct { uint32_t id=0; uint16_t dlc=0; bool ide=false; } header;
 uint8_t *buffer=nullptr;
 size_t buffer_len=0;
};
using twai_node_handle_t=void *;
struct twai_tx_done_event_data_t { bool is_tx_success; const twai_frame_t *done_tx_frame; };
struct twai_rx_done_event_data_t {};
struct twai_error_event_data_t { struct { uint32_t val; } err_flags; };
constexpr int TWAI_ERROR_BUS_OFF=3;
struct twai_state_change_event_data_t { int old_sta; int new_sta; };
struct twai_event_callbacks_t {
 bool (*on_tx_done)(twai_node_handle_t,const twai_tx_done_event_data_t*,void*)=nullptr;
 bool (*on_rx_done)(twai_node_handle_t,const twai_rx_done_event_data_t*,void*)=nullptr;
 bool (*on_error)(twai_node_handle_t,const twai_error_event_data_t*,void*)=nullptr;
 bool (*on_state_change)(twai_node_handle_t,const twai_state_change_event_data_t*,void*)=nullptr;
};
struct twai_onchip_node_config_t {
 struct { int tx,rx,quanta_clk_out,bus_off_indicator; } io_cfg{};
 struct { int bitrate; } bit_timing{};
 int fail_retry_cnt=0; size_t tx_queue_depth=0;
};
struct twai_mask_filter_config_t { uint32_t id,mask; bool is_ext,no_classic,no_fd; };
using QueueHandle_t=void *;
inline QueueHandle_t xQueueCreate(size_t,size_t) { return reinterpret_cast<void*>(1); }
inline void vQueueDelete(QueueHandle_t) {}
inline int xQueueSendFromISR(QueueHandle_t,const void*,int*) { return pdTRUE; }
inline int xQueueReceive(QueueHandle_t,void*,int) { return pdFALSE; }
int twai_node_transmit(twai_node_handle_t,const twai_frame_t*,int);
int twai_new_node_onchip(const twai_onchip_node_config_t*,twai_node_handle_t*);
int twai_node_register_event_callbacks(twai_node_handle_t,const twai_event_callbacks_t*,void*);
inline int twai_node_receive_from_isr(twai_node_handle_t,twai_frame_t*) { return ESP_ERR_TIMEOUT; }
inline int twai_node_delete(twai_node_handle_t) { return ESP_OK; }
inline int twai_node_config_mask_filter(twai_node_handle_t,int,const twai_mask_filter_config_t*) { return ESP_OK; }
inline int twai_node_enable(twai_node_handle_t) { return ESP_OK; }
int twai_node_recover(twai_node_handle_t);
class tNMEA2000 {
protected:
 virtual bool CANSendFrame(unsigned long,unsigned char,const unsigned char*,bool)=0;
 virtual bool CANOpen()=0;
 virtual bool CANGetFrame(unsigned long&,unsigned char&,unsigned char*)=0;
public: virtual ~tNMEA2000()=default;
};
