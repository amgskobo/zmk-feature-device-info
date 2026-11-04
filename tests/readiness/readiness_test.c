/* SPDX-License-Identifier: MIT */
/* Real core dispatch, nanopb and deferred device initialization. */
#include <assert.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/studio/core.h>
#include <zmk/device_info/device_info.pb.h>

static unsigned init_calls;
static int deferred_init(const struct device *device) {
    ARG_UNUSED(device);
    init_calls++;
    return 0;
}
DEVICE_DT_DEFINE(DT_NODELABEL(codec_deferred), deferred_init, NULL, NULL, NULL, POST_KERNEL, 90,
                 NULL);
static const struct device *const deferred = DEVICE_DT_GET(DT_NODELABEL(codec_deferred));

struct output {
    uint8_t bytes[4096];
    size_t used;
    unsigned writes;
    bool initialize_after_sizing;
};

static bool output_bytes(pb_ostream_t *stream, const pb_byte_t *bytes, size_t count) {
    struct output *out = stream->state;
    out->writes++;
    /* The enclosing oneof has sized its submessage before writing its length.
     * Change actual kernel state at that public output boundary. */
    if (out->initialize_after_sizing && out->writes == 2) {
        assert(!device_is_ready(deferred));
        assert(device_init(deferred) == 0);
        assert(device_is_ready(deferred) && init_calls == 1);
    }
    assert(out->used + count <= sizeof(out->bytes));
    if (bytes)
        memcpy(out->bytes + out->used, bytes, count);
    out->used += count;
    return true;
}

static zmk_studio_Response request_info(void) {
    static zmk_studio_Request request;
    request = (zmk_studio_Request)zmk_studio_Request_init_zero;
    request.which_subsystem = zmk_studio_Request_custom_tag;
    request.subsystem.custom.which_request_type = zmk_custom_Request_call_tag;
    unsigned index = 0;
    bool found = false;
    STRUCT_SECTION_FOREACH(zmk_rpc_custom_subsystem, module) {
        if (!strcmp(module->identifier, "zmk__device_info")) {
            request.subsystem.custom.request_type.call.subsystem_index = index;
            found = true;
            break;
        }
        index++;
    }
    assert(found);
    request.subsystem.custom.request_type.call.payload.size = 2;
    request.subsystem.custom.request_type.call.payload.bytes[0] = 0x0a;
    request.subsystem.custom.request_type.call.payload.bytes[1] = 0;
    STRUCT_SECTION_FOREACH(zmk_rpc_subsystem, core) {
        if (core->subsystem_choice == zmk_studio_Request_custom_tag)
            return core->func(core, &request);
    }
    assert(false);
    return (zmk_studio_Response)zmk_studio_Response_init_zero;
}

static bool find_submessage(pb_istream_t *input, uint32_t wanted, pb_istream_t *output) {
    while (input->bytes_left) {
        pb_wire_type_t wire;
        uint32_t tag;
        bool eof;
        if (!pb_decode_tag(input, &wire, &tag, &eof))
            return false;
        if (tag == wanted)
            return wire == PB_WT_STRING && pb_make_string_substream(input, output);
        if (!pb_skip_field(input, wire))
            return false;
    }
    return false;
}

struct device_check {
    const struct device *devices;
    size_t count;
    size_t index;
    unsigned deferred_seen;
    bool expected_ready;
};
static bool decode_device(pb_istream_t *stream, const pb_field_t *field, void **arg) {
    ARG_UNUSED(field);
    struct device_check *check = *arg;
    while (check->index < check->count && !check->devices[check->index].name)
        check->index++;
    assert(check->index < check->count);
    const struct device *device = &check->devices[check->index++];
    zmk_device_info_ZephyrDevice value = zmk_device_info_ZephyrDevice_init_zero;
    assert(pb_decode(stream, zmk_device_info_ZephyrDevice_fields, &value));
    char expected_name[32];
    size_t length = MIN(strlen(device->name), sizeof(expected_name) - 1);
    memcpy(expected_name, device->name, length);
    expected_name[length] = '\0';
    assert(!strcmp(value.name, expected_name));
    assert(value.ready == device_is_ready(device));
    if (device == deferred) {
        check->deferred_seen++;
        assert(value.ready == check->expected_ready);
    }
    return true;
}

static void check_decoded(const struct output *out, bool expected_ready) {
    pb_istream_t outer = pb_istream_from_buffer(out->bytes, out->used);
    pb_istream_t reply, custom, call, payload, detail;
    assert(find_submessage(&outer, zmk_studio_Response_request_response_tag, &reply));
    assert(find_submessage(&reply, zmk_studio_RequestResponse_custom_tag, &custom));
    assert(find_submessage(&custom, zmk_custom_Response_call_tag, &call));
    assert(find_submessage(&call, zmk_custom_CallResponse_payload_tag, &payload));
    assert(find_submessage(&payload, zmk_device_info_Response_device_info_tag, &detail));
    struct device_check check = {.expected_ready = expected_ready};
    check.count = z_device_get_all_static(&check.devices);
    zmk_device_info_DeviceInfoResponse decoded = zmk_device_info_DeviceInfoResponse_init_zero;
    decoded.zephyr_devices.funcs.decode = decode_device;
    decoded.zephyr_devices.arg = &check;
    assert(pb_decode(&detail, zmk_device_info_DeviceInfoResponse_fields, &decoded));
    while (check.index < check.count && !check.devices[check.index].name)
        check.index++;
    assert(check.index == check.count && check.deferred_seen == 1);
}

static size_t encode_case(const char *label, bool trigger, bool expected_ready,
                          const zmk_studio_Response *response) {
    static struct output out;
    out = (struct output){.initialize_after_sizing = trigger};
    assert(response->type.request_response.which_subsystem ==
           zmk_studio_RequestResponse_custom_tag);
    pb_ostream_t stream = {.callback = output_bytes, .state = &out, .max_size = sizeof(out.bytes)};
    bool encoded = pb_encode(&stream, zmk_studio_Response_fields, response);
    printk("Device Info readiness %s encoded=%u ready=%u init_calls=%u bytes=%u\n", label, encoded,
           device_is_ready(deferred), init_calls, (unsigned)stream.bytes_written);
    assert(encoded && stream.bytes_written == out.used);
    check_decoded(&out, expected_ready);
    return out.used;
}

static int owner_checked_device_info_readiness(void) {
    assert(!device_is_ready(deferred) && init_calls == 0);
    zmk_studio_core_unlock();
    /* Reuse the captured response so uptime/build data cannot change the
     * length oracle. Device readiness is the only changing input. */
    static zmk_studio_Response response;
    response = request_info();
    size_t before = encode_case("not-ready", false, false, &response);
    size_t changing = encode_case("deferred-init-during-output", true, true, &response);
    assert(device_is_ready(deferred) && init_calls == 1);
    size_t after = encode_case("ready-recovery", false, true, &response);
    assert(before == changing && changing == after);
    response = request_info();
    (void)encode_case("fresh-ready-request", false, true, &response);
    printk("OWNER_TEST_RESULT device_info_readiness=0\n");
    return 0;
}
SYS_INIT(owner_checked_device_info_readiness, APPLICATION, 99);
