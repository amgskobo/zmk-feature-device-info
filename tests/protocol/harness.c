/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdlib.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "framework.h"
#ifndef OWNER_HANDLER_SOURCE
#define OWNER_HANDLER_SOURCE "../../src/studio/device_info_handler.c"
#endif
#include OWNER_HANDLER_SOURCE

static unsigned decoded_count;

#ifdef FIXTURE_USE_SELECTED_CORE_PROFILES
/* Migrated unique assertions from the retained legacy extraction fixture:
 * selected core arithmetic, full handler/codec, presence and exact uptime. */
static void test_selected_core_profiles(void) {
    const uint8_t valid[] = {0x0a, 0x00};
    zmk_custom_CallRequest request = {0};
    memcpy(request.payload.bytes, valid, sizeof(valid));
    request.payload.size = sizeof(valid);
    pb_callback_t callback = {0};
    uptime = 12345;
    assert(device_info_rpc_handle_request(&request, &callback));
    uint8_t bytes[2048];
    pb_ostream_t output = pb_ostream_from_buffer(bytes, sizeof(bytes));
    assert(pb_encode(&output, zmk_device_info_Response_fields, &response_zmk__device_info));
    zmk_device_info_Response decoded = zmk_device_info_Response_init_zero;
    pb_istream_t input = pb_istream_from_buffer(bytes, output.bytes_written);
    assert(pb_decode(&input, zmk_device_info_Response_fields, &decoded));
    const zmk_device_info_DeviceInfoResponse *info = &decoded.response_type.device_info;
    assert(info->has_build && info->has_hardware && info->has_zmk_config && info->has_runtime);
    assert(ZMK_BLE_PROFILE_COUNT == 5 && info->zmk_config.ble_profile_count == 5);
    assert(info->runtime.uptime_ms == 12345);
    puts("device-info selected core profile header: PASS presence, count=5, uptime=12345");
}
#endif
static bool decode_device(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    (void)field;
    (void)arg;
    zmk_device_info_ZephyrDevice v = zmk_device_info_ZephyrDevice_init_zero;
    assert(pb_decode(stream, zmk_device_info_ZephyrDevice_fields, &v));
    if (decoded_count == 0)
        assert(!strcmp(v.name, "ready-device") && v.ready);
    else
        assert(!strcmp(v.name, "0123456789012345678901234567890") && !v.ready);
    decoded_count++;
    return true;
}
static void dispatch(const uint8_t *bytes, size_t length) {
    zmk_custom_CallRequest req = {0};
    assert(length <= sizeof(req.payload.bytes));
    if (length)
        memcpy(req.payload.bytes, bytes, length);
    req.payload.size = length;
    pb_callback_t callback = {0};
    assert(device_info_rpc_handle_request(&req, &callback));
}
static void test_requests_and_wire(void) {
    const uint8_t malformed[] = {0x0a, 0xff};
    dispatch(malformed, sizeof(malformed));
    assert(response_zmk__device_info.which_response_type == zmk_device_info_Response_error_tag);
    assert(
        !strcmp(response_zmk__device_info.response_type.error.message, "Failed to decode request"));
    dispatch(NULL, 0);
    assert(response_zmk__device_info.which_response_type == zmk_device_info_Response_error_tag);
    const uint8_t unknown[] = {0x12, 0x00};
    dispatch(unknown, sizeof(unknown));
    assert(!strcmp(response_zmk__device_info.response_type.error.message,
                   "Failed to process request"));
    const uint8_t valid[] = {0x0a, 0x00};
    uptime = UINT64_C(0x100000009);
    hw_id_result = 4;
    reset_result = 0;
#if CONFIG_HWINFO
    /* Read at boot; a reset audit clears the register before the request. */
    device_info_reset_cause_init();
    reset_register = 0;
#endif
    dispatch(valid, sizeof(valid));
    zmk_device_info_Response *response = &response_zmk__device_info;
    assert(response->which_response_type == zmk_device_info_Response_device_info_tag);
    zmk_device_info_DeviceInfoResponse *v = &response->response_type.device_info;
    assert(v->has_build && v->has_hardware && v->has_zmk_config && v->has_runtime);
    assert(v->build.zmk_dirty == EXPECT_ZMK_DIRTY &&
           v->build.zmk_config_dirty == EXPECT_CONFIG_DIRTY &&
           v->build.module_dirty == EXPECT_MODULE_DIRTY);
    /* The reported version is the build string, cut to its field if longer. */
    size_t version_len = strlen(ZMK_BUILD_VERSION);
    if (version_len > sizeof(v->build.zmk_version) - 1) {
        version_len = sizeof(v->build.zmk_version) - 1;
    }
    assert(strlen(v->build.zmk_version) == version_len &&
           !strncmp(v->build.zmk_version, ZMK_BUILD_VERSION, version_len));
    assert(!strcmp(v->build.board, "test-board"));
    assert(!strcmp(v->build.build_hash, "")); /* POSIX deliberately has no hardware build-id */
    assert(!strcmp(v->hardware.device_id, CONFIG_HWINFO ? "0001feff" : ""));
    assert(v->hardware.reset_cause == (CONFIG_HWINFO ? 0xa5 : 0));
    assert(v->hardware.flash_size_kb == 1024 && v->hardware.sram_size_kb == 256);
    assert(v->zmk_config.ble_enabled == !!CONFIG_ZMK_BLE &&
           v->zmk_config.usb_enabled == !!CONFIG_ZMK_USB);
    assert(v->zmk_config.split_enabled == !!CONFIG_ZMK_SPLIT && v->runtime.uptime_ms == 9);
    assert(v->zmk_config.display_enabled == !!CONFIG_ZMK_DISPLAY &&
           v->zmk_config.rgb_underglow_enabled == !!CONFIG_ZMK_RGB_UNDERGLOW &&
           v->zmk_config.backlight_enabled == !!CONFIG_ZMK_BACKLIGHT &&
           v->zmk_config.battery_level_enabled == !!CONFIG_ZMK_BATTERY_REPORTING);
    assert(!strcmp(v->zmk_config.split_role, CONFIG_ZMK_SPLIT_ROLE_CENTRAL ? "central"
                                             : CONFIG_ZMK_SPLIT            ? "peripheral"
                                                                           : "none"));
    assert(v->zmk_config.ble_profile_count == (CONFIG_ZMK_BLE ? FIXTURE_BLE_PROFILE_COUNT : 0));
    assert(device_info_meta.security == (CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC_REQUIRE_UNLOCK ? 1 : 0));
    pb_ostream_t sizing = PB_OSTREAM_SIZING;
    assert(pb_encode(&sizing, zmk_device_info_Response_fields, response));
    uint8_t bytes[2048];
    assert(sizing.bytes_written < sizeof(bytes));
    pb_ostream_t stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    assert(pb_encode(&stream, zmk_device_info_Response_fields, response));
    assert(stream.bytes_written == sizing.bytes_written);
    zmk_device_info_Response decoded = zmk_device_info_Response_init_zero;
    decoded.response_type.device_info.zephyr_devices.funcs.decode = decode_device;
    decoded_count = 0;
    pb_istream_t input = pb_istream_from_buffer(bytes, stream.bytes_written);
    assert(pb_decode(&input, zmk_device_info_Response_fields, &decoded));
    assert(decoded.response_type.device_info.runtime.uptime_ms == 9);
    assert(decoded.response_type.device_info.zmk_config.ble_profile_count ==
           (CONFIG_ZMK_BLE ? FIXTURE_BLE_PROFILE_COUNT : 0));
    /* nanopb initializes a newly selected oneof. Install the repeated-field
     * callback on the decoded submessage rather than its not-yet-selected union. */
    input = pb_istream_from_buffer(bytes, sizing.bytes_written);
    pb_wire_type_t wire;
    uint32_t tag;
    bool eof;
    assert(pb_decode_tag(&input, &wire, &tag, &eof) && tag == 2 && wire == PB_WT_STRING);
    pb_istream_t sub;
    assert(pb_make_string_substream(&input, &sub));
    zmk_device_info_DeviceInfoResponse detail = zmk_device_info_DeviceInfoResponse_init_zero;
    detail.zephyr_devices.funcs.decode = decode_device;
    assert(pb_decode(&sub, zmk_device_info_DeviceInfoResponse_fields, &detail));
    assert(pb_close_string_substream(&input, &sub) && !input.bytes_left);
    assert(decoded_count == 2 && detail.runtime.uptime_ms == 9);
    for (size_t capacity = 0; capacity < sizing.bytes_written; capacity++) {
        stream = pb_ostream_from_buffer(bytes, capacity);
        assert(!pb_encode(&stream, zmk_device_info_Response_fields, response));
    }
    /* Repeated malformed/valid requests must not retain prior response fields. */
    hw_id_result = -EIO;
    reset_result = -EIO;
#if CONFIG_HWINFO
    device_info_reset_cause_init(); /* a failed boot read reports no cause */
#endif
    dispatch(malformed, sizeof(malformed));
    dispatch(valid, sizeof(valid));
    v = &response->response_type.device_info;
    assert(!strcmp(v->hardware.device_id, "") && v->hardware.reset_cause == 0);
    assert(v->has_build && v->has_hardware && v->has_zmk_config && v->has_runtime);
    puts("device-info protocol: PASS dispatch, errors, roles, metadata, wire, bounds, recovery");
}
/* Parses an exact-size copy of note[0, len), so a read past the range is a
 * sanitizer error rather than a lucky in-array read. */
static void expect_build_id(const uint8_t *note, size_t len, size_t out_size,
                            const char *expected) {
    char out[48];
    assert(out_size <= sizeof(out));
    memset(out, 'x', sizeof(out));
    uint8_t *copy = len ? malloc(len) : NULL;
    assert(!len || copy);
    if (len) {
        memcpy(copy, note, len);
    }
    format_build_id(copy, len, out, out_size);
    free(copy);
    assert(!strcmp(out, expected));
}
static void test_build_id(void) {
    /* As the linker emits it: namesz 4 ("GNU"), descsz 20, NT_GNU_BUILD_ID. */
    uint8_t note[12 + 4 + 20] = {4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0};
    for (int i = 0; i < 20; i++) {
        note[16 + i] = (uint8_t)(0xa0 + i);
    }
    expect_build_id(note, sizeof(note), 48, "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3");
    expect_build_id(note, sizeof(note), 9, "a0a1a2a3"); /* cut to the field */
    expect_build_id(note, sizeof(note) - 1, 48, "");    /* descriptor cut off */
    expect_build_id(note, 11, 48, "");                  /* shorter than a header */
    expect_build_id(NULL, 0, 48, "");                   /* no note emitted */
    note[8] = 1;                                        /* NT_GNU_ABI_TAG */
    expect_build_id(note, sizeof(note), 48, "");
    note[8] = 3;
    memset(note, 0xff, 4); /* a corrupt name size must not wrap the bound */
    expect_build_id(note, sizeof(note), 48, "");
    /* A 5-byte name is padded to 8 before the descriptor. */
    uint8_t padded[12 + 8 + 2] = {5, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 'X'};
    padded[20] = 0x12;
    padded[21] = 0x34;
    expect_build_id(padded, sizeof(padded), 48, "1234");
    expect_build_id(padded, sizeof(padded) - 1, 48, "");
    puts("device-info build-id: PASS parse, padding, bounds, cut");
}
int main(void) {
    test_requests_and_wire();
    test_build_id();
#ifdef FIXTURE_USE_SELECTED_CORE_PROFILES
    test_selected_core_profiles();
#endif
    return 0;
}
