/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#define IS_ENABLED(x) (x)
#define LOG_MODULE_REGISTER(...)
#define LOG_WRN(...)                                                                               \
    do {                                                                                           \
        if (0)                                                                                     \
            fprintf(stderr, __VA_ARGS__);                                                          \
    } while (0)
#define DT_HAS_CHOSEN(x) 0
#define PRE_KERNEL_2 2
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 40
#define SYS_INIT(fn, level, prio) static int (*const fn##_ref)(void) __attribute__((unused)) = fn
/* Build strings and the dirty flags they must produce; a role may override them. */
#ifndef ZMK_BUILD_VERSION
#define ZMK_BUILD_VERSION "zmk-dirty"
#define ZMK_CONFIG_BUILD_VERSION "config-clean"
#define MODULE_BUILD_VERSION "module-dirty"
#define EXPECT_ZMK_DIRTY 1
#define EXPECT_CONFIG_DIRTY 0
#define EXPECT_MODULE_DIRTY 1
#endif
#define KERNEL_VERSION_STRING "4.1.0"
#define BUILD_TIMESTAMP "2026-10-14T00:00:00Z"
#define CONFIG_BOARD "test-board"
#define CONFIG_FLASH_SIZE 1024
#define CONFIG_SRAM_SIZE 256
#define CONFIG_BT_MAX_PAIRED 5
/* Controlled core API values include peer slots reserved by split centrals. */
#define ZMK_BLE_PROFILE_COUNT FIXTURE_BLE_PROFILE_COUNT
struct device {
    const char *name;
    bool ready;
};
static const struct device fixture_devices[] = {
    {"ready-device", true}, {NULL, true}, {"0123456789012345678901234567890123456789", false}};
static size_t z_device_get_all_static(const struct device **out) {
    *out = fixture_devices;
    return 3;
}
static bool device_is_ready(const struct device *dev) { return dev->ready; }
static uint64_t uptime;
static int hw_id_result, reset_result;
/* The accumulating hardware register; another module may clear it after boot. */
static uint32_t reset_register = 0xa5;
static uint64_t k_uptime_get(void) { return uptime; }
static ssize_t hwinfo_get_device_id(uint8_t *out, size_t max) {
    assert(max >= 4);
    const uint8_t bytes[] = {0, 1, 0xfe, 0xff};
    memcpy(out, bytes, 4);
    return hw_id_result;
}
static int hwinfo_get_reset_cause(uint32_t *out) {
    if (!reset_result)
        *out = reset_register;
    return reset_result;
}
typedef struct {
    struct {
        size_t size;
        uint8_t bytes[128];
    } payload;
} zmk_custom_CallRequest;
struct zmk_rpc_custom_subsystem_meta {
    const char *ui_urls;
    int security;
};
#define ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(x) .ui_urls = x
#define ZMK_STUDIO_RPC_HANDLER_SECURED 1
#define ZMK_STUDIO_RPC_HANDLER_UNSECURED 0
#define ZMK_RPC_CUSTOM_SUBSYSTEM(n, m, h)
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(n, t) static t response_##n
#define ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(n, cb)                                   \
    ((void)(cb), memset(&response_##n, 0, sizeof(response_##n)), &response_##n)
