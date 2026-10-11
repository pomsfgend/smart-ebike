#pragma once
/* Tiny portability layer so the pure modules build on the host (host_tests) and on ESP-IDF. */
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
static inline uint32_t fc_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
#define FC_LOGI(tag, ...) ESP_LOGI(tag, __VA_ARGS__)
#define FC_LOGW(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define FC_LOGE(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
void fc_lock(void);
void fc_unlock(void);
#else
#include <stdio.h>
uint32_t fc_now_ms(void);                 /* provided by the test harness */
#define FC_LOGI(tag, ...) do { printf("I %s: ", tag); printf(__VA_ARGS__); printf("\n"); } while (0)
#define FC_LOGW FC_LOGI
#define FC_LOGE FC_LOGI
static inline void fc_lock(void) {}
static inline void fc_unlock(void) {}
#endif
