#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/device_info/device_info.pb.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <app_version.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif

#if IS_ENABLED(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_feature_device_info, CONFIG_ZMK_FEATURE_DEVICE_INFO_LOG_LEVEL);

#if IS_ENABLED(CONFIG_HWINFO)
/* The reset cause is read once at boot: the hardware register accumulates
 * until cleared, and another module (zmk-feature-watchdog's reset audit) may
 * clear it soon after boot. This module never clears it. */
static uint32_t boot_reset_cause;

static int device_info_reset_cause_init(void) {
    if (hwinfo_get_reset_cause(&boot_reset_cause) < 0) {
        boot_reset_cause = 0;
    }
    return 0;
}

SYS_INIT(device_info_reset_cause_init, PRE_KERNEL_2, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
#endif

/*
 * GNU build-id note. On real hardware it is emitted by the linker and pinned
 * into flash by src/build_id.ld (see CMakeLists.txt). It is skipped on
 * native_sim/posix, where there is no flash image to match and the native link
 * discards the note section, so the reader gets an empty range there.
 */
struct build_id_note_header {
    uint32_t namesz; /* size of the note name (e.g. "GNU\0") */
    uint32_t descsz; /* size of the descriptor (the build-id bytes) */
    uint32_t type;   /* NT_GNU_BUILD_ID == 3 */
    /* the name (padded to 4 bytes) and then the descriptor follow */
};

#define NT_GNU_BUILD_ID 3

/* Hex-encodes the descriptor of the build-id note in note[0, len) into out
 * (out_size > 0 includes the terminator; a longer id is cut to fit). out is
 * empty when the range holds no complete build-id note. */
static void format_build_id(const uint8_t *note, size_t len, char *out, size_t out_size) {
    struct build_id_note_header header;

    out[0] = '\0';
    if (len < sizeof(header)) {
        return;
    }
    memcpy(&header, note, sizeof(header));

    /* 64-bit so that no size read from the note can wrap the bound. */
    uint64_t name_bytes = ((uint64_t)header.namesz + 3) & ~(uint64_t)3;
    if (header.type != NT_GNU_BUILD_ID || name_bytes + header.descsz > len - sizeof(header)) {
        return;
    }

    const uint8_t *desc = note + sizeof(header) + (size_t)name_bytes;
    for (size_t i = 0; i < header.descsz && i * 2 + 2 < out_size; i++) {
        snprintf(out + i * 2, 3, "%02x", desc[i]);
    }
}

#if !IS_ENABLED(CONFIG_ARCH_POSIX)
/*
 * The linker always defines these boundary symbols; when build-id emission is
 * disabled the range is empty (start == end) and we simply report no hash.
 */
extern const uint8_t __build_id_note_start[];
extern const uint8_t __build_id_note_end[];
#endif

static void fill_build_hash(char *out, size_t out_size) {
#if !IS_ENABLED(CONFIG_ARCH_POSIX)
    format_build_id(__build_id_note_start, (size_t)(__build_id_note_end - __build_id_note_start),
                    out, out_size);
#else
    format_build_id(NULL, 0, out, out_size);
#endif
}

static struct zmk_rpc_custom_subsystem_meta device_info_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("https://cormoran.github.io/zmk-feature-device-info/"),
#if IS_ENABLED(CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC_REQUIRE_UNLOCK)
    .security = ZMK_STUDIO_RPC_HANDLER_SECURED,
#else
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
#endif
};

ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__device_info, &device_info_meta, device_info_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__device_info, zmk_device_info_Response);

static bool device_info_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                           pb_callback_t *encode_response);

/* Copies src into a fixed message field, cut to fit and always terminated. */
static void copy_field(char *dst, size_t size, const char *src) {
    size_t len = 0;
    while (len + 1 < size && src[len] != '\0') {
        len++;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#define COPY_FIELD(dst, src) copy_field(dst, sizeof(dst), src)

static void set_error(zmk_device_info_Response *resp, const char *message) {
    resp->which_response_type = zmk_device_info_Response_error_tag;
    snprintf(resp->response_type.error.message, sizeof(resp->response_type.error.message), "%s",
             message);
}

/* `git describe --dirty` marks a modified tree with a trailing "-dirty". Read
 * from the full build string: the reported copy may be cut to its field. */
static bool version_is_dirty(const char *version) {
    static const char suffix[] = "-dirty";
    size_t len = strlen(version);
    return len >= sizeof(suffix) - 1 && strcmp(version + len - (sizeof(suffix) - 1), suffix) == 0;
}

static bool encode_zephyr_devices(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    const struct device *devices;
    size_t count = z_device_get_all_static(&devices);

    for (size_t i = 0; i < count; i++) {
        if (!devices[i].name) {
            continue;
        }
        zmk_device_info_ZephyrDevice dev = zmk_device_info_ZephyrDevice_init_zero;
        COPY_FIELD(dev.name, devices[i].name);
        dev.has_ready = true;
        dev.ready = device_is_ready(&devices[i]);
        if (!pb_encode_tag_for_field(stream, field)) {
            return false;
        }
        if (!pb_encode_submessage(stream, zmk_device_info_ZephyrDevice_fields, &dev)) {
            return false;
        }
    }
    return true;
}

static void handle_get_device_info(zmk_device_info_Response *resp) {
    /* DYA clears this persistent response at allocation. Populate it in place;
     * deferred encoding still uses that buffer after this handler returns. */
    zmk_device_info_DeviceInfoResponse *result = &resp->response_type.device_info;

    /* Build info */
    result->has_build = true;
    COPY_FIELD(result->build.zmk_version, ZMK_BUILD_VERSION);
    result->build.zmk_dirty = version_is_dirty(ZMK_BUILD_VERSION);

    COPY_FIELD(result->build.zmk_config_version, ZMK_CONFIG_BUILD_VERSION);
    result->build.zmk_config_dirty = version_is_dirty(ZMK_CONFIG_BUILD_VERSION);

    COPY_FIELD(result->build.module_version, MODULE_BUILD_VERSION);
    result->build.module_dirty = version_is_dirty(MODULE_BUILD_VERSION);

    COPY_FIELD(result->build.zephyr_version, KERNEL_VERSION_STRING);

    COPY_FIELD(result->build.build_timestamp, BUILD_TIMESTAMP);

    COPY_FIELD(result->build.board, CONFIG_BOARD);

    fill_build_hash(result->build.build_hash, sizeof(result->build.build_hash));

    /* Hardware info */
    result->has_hardware = true;
#if IS_ENABLED(CONFIG_HWINFO)
    {
        uint8_t dev_id[16];
        ssize_t id_len = hwinfo_get_device_id(dev_id, sizeof(dev_id));
        if (id_len > 0) {
            for (int i = 0; i < id_len && (i * 2 + 2) < (int)sizeof(result->hardware.device_id);
                 i++) {
                snprintf(result->hardware.device_id + i * 2, 3, "%02x", dev_id[i]);
            }
        }
        result->hardware.reset_cause = boot_reset_cause;
    }
#endif

#ifdef CONFIG_FLASH_SIZE
    result->hardware.flash_size_kb = CONFIG_FLASH_SIZE;
#endif
#ifdef CONFIG_SRAM_SIZE
    result->hardware.sram_size_kb = CONFIG_SRAM_SIZE;
#endif

    /* Zephyr device list via callback */
    result->zephyr_devices.funcs.encode = encode_zephyr_devices;

    /* ZMK config (compile-time Kconfig/DT values) */
    result->has_zmk_config = true;
#if DT_HAS_CHOSEN(zmk_kscan)
    /* DT_PROP_BY_IDX for compatible is unreliable; check known types explicitly */
#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zmk_kscan), zmk_kscan_gpio_matrix)
    COPY_FIELD(result->zmk_config.kscan_compatible, "zmk,kscan-gpio-matrix");
#elif DT_NODE_HAS_COMPAT(DT_CHOSEN(zmk_kscan), zmk_kscan_gpio_direct)
    COPY_FIELD(result->zmk_config.kscan_compatible, "zmk,kscan-gpio-direct");
#elif DT_NODE_HAS_COMPAT(DT_CHOSEN(zmk_kscan), zmk_kscan_gpio_demux)
    COPY_FIELD(result->zmk_config.kscan_compatible, "zmk,kscan-gpio-demux");
#elif DT_NODE_HAS_COMPAT(DT_CHOSEN(zmk_kscan), zmk_kscan_composite)
    COPY_FIELD(result->zmk_config.kscan_compatible, "zmk,kscan-composite");
#elif DT_NODE_HAS_COMPAT(DT_CHOSEN(zmk_kscan), zmk_kscan_mock)
    COPY_FIELD(result->zmk_config.kscan_compatible, "zmk,kscan-mock");
#endif
#endif

    result->zmk_config.ble_enabled = IS_ENABLED(CONFIG_ZMK_BLE);
#if IS_ENABLED(CONFIG_ZMK_BLE)
    result->zmk_config.ble_profile_count = ZMK_BLE_PROFILE_COUNT;
#endif

    result->zmk_config.usb_enabled = IS_ENABLED(CONFIG_ZMK_USB);
    result->zmk_config.split_enabled = IS_ENABLED(CONFIG_ZMK_SPLIT);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    COPY_FIELD(result->zmk_config.split_role, "central");
#elif IS_ENABLED(CONFIG_ZMK_SPLIT)
    COPY_FIELD(result->zmk_config.split_role, "peripheral");
#else
    COPY_FIELD(result->zmk_config.split_role, "none");
#endif

    result->zmk_config.display_enabled = IS_ENABLED(CONFIG_ZMK_DISPLAY);
    result->zmk_config.rgb_underglow_enabled = IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW);
    result->zmk_config.backlight_enabled = IS_ENABLED(CONFIG_ZMK_BACKLIGHT);
    result->zmk_config.battery_level_enabled = IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING);

    /* Runtime status */
    result->has_runtime = true;
    result->runtime.uptime_ms = (uint32_t)k_uptime_get();

    resp->which_response_type = zmk_device_info_Response_device_info_tag;
}

static bool device_info_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                           pb_callback_t *encode_response) {
    zmk_device_info_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__device_info, encode_response);

    zmk_device_info_Request req = zmk_device_info_Request_init_zero;

    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_device_info_Request_fields, &req)) {
        LOG_WRN("Failed to decode device info request: %s", PB_GET_ERROR(&req_stream));
        set_error(resp, "Failed to decode request");
        return true;
    }

    switch (req.which_request_type) {
    case zmk_device_info_Request_get_device_info_tag:
        handle_get_device_info(resp);
        break;
    default:
        LOG_WRN("Unsupported device info request type: %d", req.which_request_type);
        set_error(resp, "Failed to process request");
        break;
    }
    return true;
}
