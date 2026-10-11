#pragma once
#include <cstdint>
#define ESP_OK 0
struct esp_timer_create_args_t {
    void (*callback)(void*);
    const char* name;
};
using esp_timer_handle_t = void*;
inline void (*testTimerCallback)(void*) = nullptr;
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* handle) {
    testTimerCallback = args->callback;
    *handle = reinterpret_cast<void*>(1);
    return ESP_OK;
}
inline int esp_timer_start_periodic(esp_timer_handle_t, uint64_t) { return ESP_OK; }
inline int esp_timer_delete(esp_timer_handle_t) { return ESP_OK; }
