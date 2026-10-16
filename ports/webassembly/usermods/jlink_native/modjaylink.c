/* Browser-only libjaylink -> MicroPython PyUSB bridge.
 * The USB device belongs to the Python caller; never close it from this module.
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/mpstate.h"
#include "py/nlr.h"
#include "compat/libusb.h"
#include "compat/pyusb_bridge.h"
#include "swd_engine.h"

#if JLINK_LIBJAYLINK_AVAILABLE
#include "libjaylink.h"
#endif

/* Store the Python object in a GC root, not in an untraced C static. */
MP_REGISTER_ROOT_POINTER(mp_obj_t jlink_pyusb_device);
static jlink_usb_config_t usb_config;
static int usb_ready;
static int last_usb_error;
/* Production builds do not emit native probe tracing. SWD status and errors
 * are propagated to Python, where the standard pyOCD logger owns diagnostics.
 * Set JLINK_NATIVE_DEBUG=1 during a local diagnostic build if needed.
 */
#ifndef JLINK_NATIVE_DEBUG
#define JLINK_NATIVE_DEBUG (0)
#endif
#if JLINK_NATIVE_DEBUG
#define JLINK_TRACE(...) mp_printf(&mp_plat_print, __VA_ARGS__)
#else
/* Keep the argument expressions type-checked without generating output or
 * retaining format strings in the optimized WASM binary. */
#define JLINK_TRACE(...) do { if (0) mp_printf(&mp_plat_print, __VA_ARGS__); } while (0)
#endif

#if JLINK_LIBJAYLINK_AVAILABLE
/* A live session owns the context, enumeration and device handle. */
static struct jaylink_context *active_ctx;
static struct jaylink_device **active_devices;
static struct jaylink_device_handle *active_handle;
/* The order of these device commands follows OpenOCD jlink_init(). */
static uint8_t active_caps[JAYLINK_DEV_EXT_CAPS_SIZE];
static bool active_caps_valid;
static struct jaylink_connection active_connection;
static bool active_registered;
#endif


const jlink_usb_config_t *jlink_usb_config(void) {
    return &usb_config;
}

int jlink_usb_is_ready(void) {
    return usb_ready && MP_STATE_VM(jlink_pyusb_device) != MP_OBJ_NULL;
}

static int call_usb(uint8_t endpoint, void *buffer, size_t length, unsigned timeout, int writing) {
    if (!jlink_usb_is_ready()) return LIBUSB_ERROR_NO_DEVICE;
    if (length > 0x7fffffffU) return LIBUSB_ERROR_INVALID_PARAM;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[5];
        mp_obj_t device = MP_STATE_VM(jlink_pyusb_device);
        mp_load_method(device, writing ? MP_QSTR_write : MP_QSTR_read, args);
        args[2] = mp_obj_new_int(endpoint);
        args[3] = writing ? mp_obj_new_bytes(buffer, length) : mp_obj_new_int_from_uint(length);
        args[4] = mp_obj_new_int_from_uint(timeout);
        mp_obj_t result = mp_call_method_n_kw(3, 0, args);
        int count;
        if (writing) {
            count = mp_obj_get_int(result);
        } else {
            mp_buffer_info_t info;
            mp_get_buffer_raise(result, &info, MP_BUFFER_READ);
            if (info.len > length) {
                nlr_pop();
                last_usb_error = LIBUSB_ERROR_OVERFLOW;
                return LIBUSB_ERROR_OVERFLOW;
            }
            memcpy(buffer, info.buf, info.len);
            count = (int)info.len;
        }
        nlr_pop();
        if (count < 0 || (size_t)count > length) {
            last_usb_error = LIBUSB_ERROR_OVERFLOW;
            return LIBUSB_ERROR_OVERFLOW;
        }
        last_usb_error = 0;
        return count;
    }
    /* A Python timeout/USB exception is converted to a C transport error.
     * No exception escapes across libjaylink's C resource-management frames.
     */
    JLINK_TRACE("[jaylink-usb] Python USB exception:\n");

    last_usb_error = LIBUSB_ERROR_IO;

    return LIBUSB_ERROR_IO;
}

int jlink_usb_transfer_write(uint8_t ep, const uint8_t *src, size_t length, unsigned timeout) {
    return call_usb(ep, (void *)src, length, timeout, 1);
}
int jlink_usb_transfer_read(uint8_t ep, uint8_t *dest, size_t length, unsigned timeout) {
    return call_usb(ep, dest, length, timeout, 0);
}

static mp_obj_t native_bind_usb(size_t n, const mp_obj_t *args) {
    /* bind_usb(device, ep_in, ep_out, interface=0, serial="", product="") */
    if (usb_ready) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("unbind existing J-Link USB first"));
    }
    int ep_in = mp_obj_get_int(args[1]);
    int ep_out = mp_obj_get_int(args[2]);
    int iface = n > 3 ? mp_obj_get_int(args[3]) : 0;
    if (ep_in < 0x80 || ep_in > 0xff || ep_out < 1 || ep_out >= 0x80 || iface != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("expected bulk endpoints on interface 0"));
    }
    int vid = mp_obj_get_int(mp_load_attr(args[0], MP_QSTR_idVendor));
    int pid = mp_obj_get_int(mp_load_attr(args[0], MP_QSTR_idProduct));
    if (vid != 0x1366 || pid < 0 || pid > 0xffff) {
        mp_raise_ValueError(MP_ERROR_TEXT("expected SEGGER J-Link USB VID 1366"));
    }
    memset(&usb_config, 0, sizeof(usb_config));
    usb_config.vid = vid;
    usb_config.pid = pid;
    usb_config.ep_in = ep_in;
    usb_config.ep_out = ep_out;
    usb_config.interface_number = iface;
    if (n > 4 && args[4] != mp_const_none) {
        const char *serial = mp_obj_str_get_str(args[4]);
        size_t len = strlen(serial);
        if (len >= sizeof(usb_config.serial)) {
            mp_raise_ValueError(MP_ERROR_TEXT("serial too long"));
        }
        memcpy(usb_config.serial, serial, len + 1);
    }
    if (n > 5 && args[5] != mp_const_none) {
        const char *product = mp_obj_str_get_str(args[5]);
        size_t len = strlen(product);
        if (len >= sizeof(usb_config.product)) {
            mp_raise_ValueError(MP_ERROR_TEXT("product too long"));
        }
        memcpy(usb_config.product, product, len + 1);
    }
    MP_STATE_VM(jlink_pyusb_device) = args[0];
    usb_ready = 1;
    last_usb_error = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_bind_usb_obj, 3, 6, native_bind_usb);

static void jaylink_release_session(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    if (active_handle && active_registered) {
        struct jaylink_connection connections[JAYLINK_MAX_CONNECTIONS];
        size_t count = 0;
        int rv = jaylink_unregister(active_handle, &active_connection, connections, &count);
        JLINK_TRACE("[jaylink-init] unregister returned %d\n", rv);
    }
    active_registered = false;
    active_caps_valid = false;
    memset(active_caps, 0, sizeof(active_caps));
    memset(&active_connection, 0, sizeof(active_connection));
    if (active_handle) { jaylink_close(active_handle); active_handle = NULL; }
    if (active_devices) { jaylink_free_devices(active_devices, true); active_devices = NULL; }
    if (active_ctx) { jaylink_exit(active_ctx); active_ctx = NULL; }
#endif
}

static mp_obj_t native_unbind_usb(void) {
    jaylink_release_session();
    usb_ready = 0;
    MP_STATE_VM(jlink_pyusb_device) = MP_OBJ_NULL;
    memset(&usb_config, 0, sizeof(usb_config));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_unbind_usb_obj, native_unbind_usb);

static mp_obj_t native_ready(void) { return mp_obj_new_bool(jlink_usb_is_ready()); }
static MP_DEFINE_CONST_FUN_OBJ_0(native_ready_obj, native_ready);

static mp_obj_t native_last_error(void) { return mp_obj_new_int(last_usb_error); }
static MP_DEFINE_CONST_FUN_OBJ_0(native_last_error_obj, native_last_error);

static mp_obj_t native_backend(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    return mp_obj_new_str("libjaylink+python-usb", 21);
#else
    return mp_obj_new_str("python-usb-transport-only", 25);
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_backend_obj, native_backend);

static mp_obj_t native_firmware_version(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    if (!jlink_usb_is_ready()) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("USB not bound"));
    struct jaylink_context *ctx = NULL;
    struct jaylink_device **devices = NULL;
    struct jaylink_device_handle *handle = NULL;
    size_t count = 0;
    char *version = NULL;
    size_t length = 0;
    int ret = jaylink_init(&ctx);
    if (ret == JAYLINK_OK) ret = jaylink_discovery_scan(ctx, JAYLINK_HIF_USB);
    if (ret == JAYLINK_OK) ret = jaylink_get_devices(ctx, &devices, &count);
    if (ret == JAYLINK_OK && count == 0) ret = JAYLINK_ERR;
    if (ret == JAYLINK_OK) ret = jaylink_open(devices[0], &handle);
    if (ret == JAYLINK_OK) ret = jaylink_get_firmware_version(handle, &version, &length);
    mp_obj_t result = mp_const_none;
    if (ret == JAYLINK_OK && version && length) result = mp_obj_new_str(version, strlen(version));
    free(version);
    if (handle) jaylink_close(handle);
    if (devices) jaylink_free_devices(devices, true);
    if (ctx) jaylink_exit(ctx);
    if (ret != JAYLINK_OK) mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("libjaylink error %d"), ret);
    return result;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("vendor libjaylink source missing"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_firmware_version_obj, native_firmware_version);


/* Target debug control; keep the vendor protocol in libjaylink. */
static mp_obj_t native_open_session(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    if (!jlink_usb_is_ready()) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("USB not bound"));
    if (active_handle) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("J-Link session already open"));
    size_t count = 0;
    const char *stage = "init";
    JLINK_TRACE("[jaylink] init\n");
    int ret = jaylink_init(&active_ctx);
    if (ret == JAYLINK_OK) {
        stage = "discovery_scan";
        JLINK_TRACE("[jaylink] discovery_scan begin\n");
        ret = jaylink_discovery_scan(active_ctx, JAYLINK_HIF_USB);
        JLINK_TRACE("[jaylink] discovery_scan returned %d\n", ret);
    }
    if (ret == JAYLINK_OK) {
        stage = "get_devices";
        ret = jaylink_get_devices(active_ctx, &active_devices, &count);
        JLINK_TRACE("[jaylink] get_devices returned %d count=%u\n", ret, (unsigned)count);
    }
    if (ret == JAYLINK_OK && count == 0) ret = JAYLINK_ERR;
    if (ret == JAYLINK_OK) {
        stage = "open";
        JLINK_TRACE("[jaylink] open begin\n");
        ret = jaylink_open(active_devices[0], &active_handle);
        JLINK_TRACE("[jaylink] open returned %d\n", ret);
    }
    if (ret == JAYLINK_OK) {
        /* OpenOCD jlink_init(): the order matters for older probes, including
         * the J-Link ARM-OB STM32 firmware from 2012 (hardware version 7).
         * Never skip directly from jaylink_open() to select_interface().
         */
        char *version = NULL;
        size_t length = 0;
        stage = "firmware_version";
        JLINK_TRACE("[jaylink-init] firmware version begin\n");
        ret = jaylink_get_firmware_version(active_handle, &version, &length);
        /* Keep the firmware query and its ordering, without copying its
         * value to a debug-only printable buffer in production builds. */
        (void)length;
        free(version);
    }
    if (ret == JAYLINK_OK) {
        stage = "get_caps";
        memset(active_caps, 0, sizeof(active_caps));
        ret = jaylink_get_caps(active_handle, active_caps);
        JLINK_TRACE("[jaylink-init] get_caps returned %d\n", ret);
        if (ret == JAYLINK_OK)
            active_caps_valid = true;
    }
    if (ret == JAYLINK_OK && jaylink_has_cap(active_caps, JAYLINK_DEV_CAP_GET_EXT_CAPS)) {
        stage = "get_extended_caps";
        ret = jaylink_get_extended_caps(active_handle, active_caps);
        JLINK_TRACE("[jaylink-init] get_extended_caps returned %d\n", ret);
    }
    if (ret == JAYLINK_OK && jaylink_has_cap(active_caps, JAYLINK_DEV_CAP_GET_HW_VERSION)) {
        stage = "get_hardware_version";
        struct jaylink_hardware_version version;
        ret = jaylink_get_hardware_version(active_handle, &version);
        if (ret == JAYLINK_OK)
            JLINK_TRACE("[jaylink-init] hardware version: %u.%02u\n",
                (unsigned)version.major, (unsigned)version.minor);
    }
    if (ret == JAYLINK_OK && jaylink_has_cap(active_caps, JAYLINK_DEV_CAP_GET_FREE_MEMORY)) {
        /* OpenOCD checks the SWD command queue capacity before enabling SWD.
         * We currently submit only one 54-bit transfer per USB request.
         */
        stage = "get_free_memory";
        uint32_t free_bytes = 0;
        ret = jaylink_get_free_memory(active_handle, &free_bytes);
        if (ret == JAYLINK_OK) {
            JLINK_TRACE("[jaylink-init] device free memory=%u bytes\n",
                (unsigned)free_bytes);
            if (free_bytes < 143) {
                ret = JAYLINK_ERR;
                JLINK_TRACE("[jaylink-init] not enough probe memory for SWD\n");
            }
        }
    }
    if (ret == JAYLINK_OK) {
        stage = "get_hardware_status";
        struct jaylink_hardware_status hw_status;
        ret = jaylink_get_hardware_status(active_handle, &hw_status);
        if (ret == JAYLINK_OK)
            JLINK_TRACE("[jaylink-init] VTref=%u mV\n",
                (unsigned)hw_status.target_voltage);
    }
    if (ret == JAYLINK_OK && jaylink_has_cap(active_caps, JAYLINK_DEV_CAP_REGISTER)) {
        stage = "register";
        /* Registration only when advertised, as in OpenOCD. */
        struct jaylink_connection connections[JAYLINK_MAX_CONNECTIONS];
        size_t conn_count = 0;
        memset(&active_connection, 0, sizeof(active_connection));
        strcpy(active_connection.hid, "0.0.0.0");
        JLINK_TRACE("[jaylink-init] register begin\n");
        ret = jaylink_register(active_handle, &active_connection, connections, &conn_count);
        if (ret == JAYLINK_OK) {
            bool found = false;
            for (size_t i = 0; i < conn_count; ++i) {
                if (connections[i].handle == active_connection.handle) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ret = JAYLINK_ERR;
            } else {
                active_registered = true;
            }
        }
        JLINK_TRACE("[jaylink-init] register returned %d handle=%u\n",
            ret, (unsigned)active_connection.handle);
    }
    if (ret != JAYLINK_OK) {
        jaylink_release_session();
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("libjaylink %s failed: %d"), stage, ret);
    }
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink source not installed"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_open_session_obj, native_open_session);

static mp_obj_t native_close_session(void) {
    jaylink_release_session();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_close_session_obj, native_close_session);

static mp_obj_t native_session_open(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    return mp_obj_new_bool(active_handle != NULL);
#else
    return mp_const_false;
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_session_open_obj, native_session_open);

#if JLINK_LIBJAYLINK_AVAILABLE
static struct jaylink_device_handle *require_handle(void) {
    if (!active_handle) mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("J-Link session not open"));
    return active_handle;
}
static void check_jaylink(int result) {
    if (result != JAYLINK_OK) mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("libjaylink error %d"), result);
}
#endif

static mp_obj_t native_set_interface(mp_obj_t iface) {
#if JLINK_LIBJAYLINK_AVAILABLE
    const char *name = mp_obj_str_get_str(iface);
    enum jaylink_target_interface target;
    if (!strcmp(name, "swd")) target = JAYLINK_TIF_SWD;
    else if (!strcmp(name, "jtag")) target = JAYLINK_TIF_JTAG;
    else mp_raise_ValueError(MP_ERROR_TEXT("interface must be swd or jtag"));
    check_jaylink(jaylink_select_interface(require_handle(), target, NULL));
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_set_interface_obj, native_set_interface);

/* Follows OpenOCD's select_interface() and jlink_reset(0, 0) sequence.
 * This is deliberately separate from set_interface() to preserve legacy
 * users. Use only for an explicit SWD connect, after open_session().
 */
static mp_obj_t native_openocd_prepare_swd(mp_obj_t speed_obj) {
#if JLINK_LIBJAYLINK_AVAILABLE
    struct jaylink_device_handle *handle = require_handle();
    if (!active_caps_valid)
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("J-Link capabilities not initialized"));
    int khz = mp_obj_get_int(speed_obj);
    if (khz <= 0 || khz > 65535)
        mp_raise_ValueError(MP_ERROR_TEXT("speed kHz out of range"));
    if (!jaylink_has_cap(active_caps, JAYLINK_DEV_CAP_SELECT_TIF))
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("probe does not support SWD interface selection"));

    uint32_t interfaces = 0;
    check_jaylink(jaylink_get_available_interfaces(handle, &interfaces));
    if ((interfaces & (1u << JAYLINK_TIF_SWD)) == 0)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("probe does not advertise SWD interface"));
    JLINK_TRACE("[jaylink-init] available interfaces=0x%x\n", (unsigned)interfaces);

    check_jaylink(jaylink_select_interface(handle, JAYLINK_TIF_SWD, NULL));
    JLINK_TRACE("[jaylink-init] SWD interface selected\n");

    /* Match OpenOCD jlink_reset(0, 0), including both reset signals.
     * OpenOCD uses active-low signal convention. In particular, do not
     * treat the reset() API as a target-power control operation.
     */
    check_jaylink(jaylink_set_reset(handle));
    check_jaylink(jaylink_jtag_set_trst(handle));
    JLINK_TRACE("[jaylink-init] initial SRST/TRST state applied\n");

    check_jaylink(jaylink_set_speed(handle, (uint16_t)khz));
    JLINK_TRACE("[jaylink-init] SWD speed=%d kHz\n", khz);
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_openocd_prepare_swd_obj, native_openocd_prepare_swd);

static mp_obj_t native_set_speed(mp_obj_t speed_obj) {
#if JLINK_LIBJAYLINK_AVAILABLE
    int khz = mp_obj_get_int(speed_obj);
    if (khz <= 0 || khz > 65535) mp_raise_ValueError(MP_ERROR_TEXT("speed kHz out of range"));
    check_jaylink(jaylink_set_speed(require_handle(), (uint16_t)khz));
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_set_speed_obj, native_set_speed);

static mp_obj_t native_assert_reset(mp_obj_t asserted) {
#if JLINK_LIBJAYLINK_AVAILABLE
    check_jaylink(mp_obj_is_true(asserted) ? jaylink_set_reset(require_handle()) : jaylink_clear_reset(require_handle()));
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_assert_reset_obj, native_assert_reset);

/* Target voltage diagnostics and explicit target power (J-Link pin 19).
 * Never enable 5V automatically: only the caller can opt in.
 */
static mp_obj_t native_get_target_status(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    struct jaylink_device_handle *handle = require_handle();
    struct jaylink_hardware_status status;
    check_jaylink(jaylink_get_hardware_status(handle, &status));

    bool can_power = false;
    int power_enabled = -1;  /* -1 means unknown, not necessarily off. */
    uint8_t caps[JAYLINK_DEV_CAPS_SIZE];
    if (jaylink_get_caps(handle, caps) == JAYLINK_OK) {
        can_power = jaylink_has_cap(caps, JAYLINK_DEV_CAP_SET_TARGET_POWER);
        if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_GET_HW_INFO)) {
            uint32_t info = 0;
            if (jaylink_get_hardware_info(handle, JAYLINK_HW_INFO_TARGET_POWER,
                                           &info) == JAYLINK_OK) {
                power_enabled = info != 0;
            }
        }
    }

    mp_obj_t fields[3] = {
        mp_obj_new_int(status.target_voltage),
        mp_obj_new_bool(can_power),
        mp_obj_new_int(power_enabled),
    };
    return mp_obj_new_tuple(3, fields);
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_get_target_status_obj, native_get_target_status);

static mp_obj_t native_set_target_power(mp_obj_t enabled) {
#if JLINK_LIBJAYLINK_AVAILABLE
    struct jaylink_device_handle *handle = require_handle();
    uint8_t caps[JAYLINK_DEV_CAPS_SIZE];
    check_jaylink(jaylink_get_caps(handle, caps));
    if (!jaylink_has_cap(caps, JAYLINK_DEV_CAP_SET_TARGET_POWER)) {
        mp_raise_msg(&mp_type_NotImplementedError,
                     MP_ERROR_TEXT("J-Link does not support target power control"));
    }
    check_jaylink(jaylink_set_target_power(handle, mp_obj_is_true(enabled)));
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_set_target_power_obj, native_set_target_power);

static mp_obj_t native_swd_io(mp_obj_t directions, mp_obj_t outputs, mp_obj_t count_obj) {
#if JLINK_LIBJAYLINK_AVAILABLE
    mp_int_t count = mp_obj_get_int(count_obj);
    if (count < 1 || count > 4096) mp_raise_ValueError(MP_ERROR_TEXT("SWD bit count out of range"));
    size_t nbytes = ((size_t)count + 7) / 8;
    mp_buffer_info_t direction, output;
    mp_get_buffer_raise(directions, &direction, MP_BUFFER_READ);
    mp_get_buffer_raise(outputs, &output, MP_BUFFER_READ);
    if (direction.len < nbytes || output.len < nbytes) mp_raise_ValueError(MP_ERROR_TEXT("SWD buffer too small"));
    uint8_t *received = malloc(nbytes);
    if (!received) mp_raise_OSError(12);
    int ret = jaylink_swd_io(require_handle(), direction.buf, output.buf, received, (uint16_t)count);
    mp_obj_t result = mp_const_none;
    if (ret == JAYLINK_OK) result = mp_obj_new_bytes(received, nbytes);
    free(received);
    check_jaylink(ret);
    return result;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_3(native_swd_io_obj, native_swd_io);

/* OpenOCD-style ADIv5 SWD frame executor.
 * The transaction, including turnarounds, ACK, data, parity and idle clocks,
 * is submitted as ONE libjaylink operation instead of being assembled by Python.
 */
#if JLINK_LIBJAYLINK_AVAILABLE
static int native_swd_exchange(void *context, const uint8_t *direction,
        const uint8_t *output, uint8_t *input, uint16_t bit_count) {
    return jaylink_swd_io((struct jaylink_device_handle *)context,
                          direction, output, input, bit_count);
}
#endif

static mp_obj_t native_swd_transfer_impl(size_t n, const mp_obj_t *args, bool compact) {
#if JLINK_LIBJAYLINK_AVAILABLE
    /* swd_transfer(ap, read, addr, data=0, retries=20, ap_delay=0)
     * Return (status, ack, data, request, input_bytes, attempts, direction, output).
     * Bad ACK and parity are returned as status, not ignored or silently retried.
     */
    bool ap = mp_obj_is_true(args[0]);
    bool read = mp_obj_is_true(args[1]);
    int address = mp_obj_get_int(args[2]);
    uint32_t value = n > 3 ? (uint32_t)mp_obj_get_int_truncated(args[3]) : 0;
    int retries = n > 4 ? mp_obj_get_int(args[4]) : JLINK_SWD_MAX_RETRIES;
    int ap_delay = n > 5 ? mp_obj_get_int(args[5]) : 0;
    if (address < 0 || address > 12 || (address & 3) != 0 ||
            retries < 1 || retries > (int)JLINK_SWD_MAX_RETRIES ||
            ap_delay < 0 || ap_delay > (int)JLINK_SWD_MAX_AP_DELAY_CLOCKS) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid SWD address, retries or AP delay"));
    }
    struct jlink_swd_result result;
    int status = jlink_swd_transfer(native_swd_exchange, require_handle(),
            ap, read, (uint8_t)address, value, (uint8_t)retries,
            (uint16_t)ap_delay, &result);
    if (status == JLINK_SWD_TRANSPORT) {
        mp_raise_msg_varg(&mp_type_OSError,
                MP_ERROR_TEXT("libjaylink SWD transport error %d"), result.transport_status);
    }
    if (compact && status == JLINK_SWD_OK) {
        return mp_obj_new_int_from_uint(result.data);
    }
    mp_obj_t items[8] = {
        mp_obj_new_int(status),
        mp_obj_new_int(result.ack),
        mp_obj_new_int_from_uint(result.data),
        mp_obj_new_int(result.request),
        mp_obj_new_bytes(result.input, (result.bits + 7u) / 8u),
        mp_obj_new_int(result.attempts),
        mp_obj_new_bytes(result.direction, (result.bits + 7u) / 8u),
        mp_obj_new_bytes(result.output, (result.bits + 7u) / 8u),
    };
    return mp_obj_new_tuple(8, items);
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static mp_obj_t native_swd_transfer(size_t n, const mp_obj_t *args) {
    return native_swd_transfer_impl(n, args, false);
}
static mp_obj_t native_swd_transfer_fast(size_t n, const mp_obj_t *args) {
    return native_swd_transfer_impl(n, args, true);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_swd_transfer_obj, 3, 6, native_swd_transfer);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_swd_transfer_fast_obj, 3, 6, native_swd_transfer_fast);

/* Fast repeated AP register transfers. pyOCD's MEM-AP TAR/CSW owns
 * target-memory auto-increment; address here is the constant AP register
 * offset. The C engine adaptively batches up to 24 words per operation.
 */
static mp_obj_t native_swd_ap_batch(mp_obj_t read_obj,
        mp_obj_t addr_obj, mp_obj_t payload) {
#if JLINK_LIBJAYLINK_AVAILABLE
    const bool read = mp_obj_is_true(read_obj);
    const int address = mp_obj_get_int(addr_obj);
    if (address < 0 || address > 12 || (address & 3)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid AP register offset"));
    }
    uint32_t count;
    uint32_t *words;
    if (read) {
        mp_int_t requested = mp_obj_get_int(payload);
        if (requested < 0 || requested > 16384) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid AP burst word count"));
        }
        count = (uint32_t)requested;
    } else {
        size_t sequence_length;
        mp_obj_t *items;
        mp_obj_get_array(payload, &sequence_length, &items);
        if (sequence_length > 16384) {
            mp_raise_ValueError(MP_ERROR_TEXT("AP burst too large"));
        }
        count = (uint32_t)sequence_length;
    }
    if (!count) {
        if (read) return mp_obj_new_list(0, NULL);
        return mp_const_none;
    }
    words = m_new(uint32_t, count);
    if (!read) {
        size_t sequence_length;
        mp_obj_t *items;
        mp_obj_get_array(payload, &sequence_length, &items);
        for (uint32_t i = 0; i < count; ++i) {
            words[i] = (uint32_t)mp_obj_get_int_truncated(items[i]);
        }
    }
    struct jlink_swd_batch_result result;
    const int rv = jlink_swd_ap_batch(native_swd_exchange,
            require_handle(), read, (uint8_t)address,
            read ? NULL : words, read ? words : NULL, count, &result);
    if (rv != JLINK_SWD_OK) {
        m_del(uint32_t, words, count);
        mp_raise_msg_varg(&mp_type_OSError,
                MP_ERROR_TEXT("J-Link AP batch failed status=%d ack=%d index=%u transport=%d"),
                rv, result.ack, (unsigned)result.failed_index, result.transport_status);
    }
    mp_obj_t value = mp_const_none;
    if (read) {
        value = mp_obj_new_list(0, NULL);
        for (uint32_t i = 0; i < count; ++i)
            mp_obj_list_append(value, mp_obj_new_int_from_uint(words[i]));
    }
    m_del(uint32_t, words, count);
    return value;
#else
    mp_raise_msg(&mp_type_NotImplementedError,
                 MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_3(native_swd_ap_batch_obj, native_swd_ap_batch);

static mp_obj_t native_switch_to_swd(void) {
#if JLINK_LIBJAYLINK_AVAILABLE
    /* OpenOCD JTAG_TO_SWD format: >=50 ones, 0xe79e LSB-first,
     * >=50 ones, two low clocks. Whole sequence is a single USB command.
     */
    static const uint8_t swj_bytes[17] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x9e, 0xe7,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00
    };
    uint8_t direction[17];
    uint8_t input[17];
    memset(direction, 0xff, sizeof(direction));
    memset(input, 0, sizeof(input));
    int rv = jaylink_swd_io(require_handle(), direction, swj_bytes, input, 136);
    check_jaylink(rv);
    return mp_const_none;
#else
    mp_raise_msg(&mp_type_NotImplementedError, MP_ERROR_TEXT("libjaylink unavailable"));
#endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(native_switch_to_swd_obj, native_switch_to_swd);

static const mp_rom_map_elem_t module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_jaylink_native) },
    { MP_ROM_QSTR(MP_QSTR_bind_usb), MP_ROM_PTR(&native_bind_usb_obj) },
    { MP_ROM_QSTR(MP_QSTR_unbind_usb), MP_ROM_PTR(&native_unbind_usb_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&native_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_error), MP_ROM_PTR(&native_last_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_backend), MP_ROM_PTR(&native_backend_obj) },
    { MP_ROM_QSTR(MP_QSTR_firmware_version), MP_ROM_PTR(&native_firmware_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_session), MP_ROM_PTR(&native_open_session_obj) },
    { MP_ROM_QSTR(MP_QSTR_close_session), MP_ROM_PTR(&native_close_session_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_open), MP_ROM_PTR(&native_session_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_interface), MP_ROM_PTR(&native_set_interface_obj) },
    { MP_ROM_QSTR(MP_QSTR_openocd_prepare_swd), MP_ROM_PTR(&native_openocd_prepare_swd_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_speed), MP_ROM_PTR(&native_set_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_assert_reset), MP_ROM_PTR(&native_assert_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_swd_io), MP_ROM_PTR(&native_swd_io_obj) },
    { MP_ROM_QSTR(MP_QSTR_swd_transfer), MP_ROM_PTR(&native_swd_transfer_obj) },
    { MP_ROM_QSTR(MP_QSTR_swd_transfer_fast), MP_ROM_PTR(&native_swd_transfer_fast_obj) },
    { MP_ROM_QSTR(MP_QSTR_swd_ap_batch), MP_ROM_PTR(&native_swd_ap_batch_obj) },
    { MP_ROM_QSTR(MP_QSTR_switch_to_swd), MP_ROM_PTR(&native_switch_to_swd_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_target_status), MP_ROM_PTR(&native_get_target_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_target_power), MP_ROM_PTR(&native_set_target_power_obj) },
};
static MP_DEFINE_CONST_DICT(module_globals, module_globals_table);
const mp_obj_module_t jaylink_native_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_jaylink_native, jaylink_native_module);
