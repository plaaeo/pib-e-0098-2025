#pragma once
#include <esp_attr.h>
#include <esp_log.h>

//< Atributo. Define que uma função é segura para se executar em ISRs.
#define PORT_ISR_SAFE IRAM_ATTR

//< Atributo. Define que um dado irá persistir entre ciclos de sono do MCU.
#define PORT_PERSIST_SLEEP RTC_NOINIT_ATTR

#define PORT_LOGI ESP_LOGI
#define PORT_LOGE ESP_LOGE
#define PORT_LOGW ESP_LOGW
#define PORT_LOGD ESP_LOGD
#define PORT_LOGV ESP_LOGV