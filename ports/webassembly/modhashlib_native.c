/* Compact native MD5 object for Python's hashlib compatibility layer.
 *
 * No JS bridge and no per-block Python arithmetic or retained data chunks.
 */
#include <string.h>

#include "py/runtime.h"
#include "py/objstr.h"
#include "md5_core.h"

typedef struct {
    mp_obj_base_t base;
    wasm_md5_context_t ctx;
} wasm_md5_object_t;

static mp_obj_t native_md5_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    wasm_md5_object_t *self = mp_obj_malloc(wasm_md5_object_t, type);
    wasm_md5_init(&self->ctx);
    if (n_args) {
        mp_buffer_info_t input;
        mp_get_buffer_raise(args[0], &input, MP_BUFFER_READ);
        wasm_md5_update(&self->ctx, input.buf, input.len);
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t native_md5_update(mp_obj_t self_in, mp_obj_t input_in) {
    wasm_md5_object_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t input;
    mp_get_buffer_raise(input_in, &input, MP_BUFFER_READ);
    wasm_md5_update(&self->ctx, input.buf, input.len);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(native_md5_update_obj, native_md5_update);

static mp_obj_t native_md5_digest(mp_obj_t self_in) {
    wasm_md5_object_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t digest[16];
    wasm_md5_digest(&self->ctx, digest);
    return mp_obj_new_bytes(digest, sizeof(digest));
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_md5_digest_obj, native_md5_digest);

static mp_obj_t native_md5_hexdigest(mp_obj_t self_in) {
    wasm_md5_object_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t digest[16];
    static const char alphabet[] = "0123456789abcdef";
    char hex[32];
    wasm_md5_digest(&self->ctx, digest);
    for (size_t i = 0; i < 16; ++i) {
        hex[i * 2] = alphabet[digest[i] >> 4];
        hex[i * 2 + 1] = alphabet[digest[i] & 15];
    }
    return mp_obj_new_str(hex, sizeof(hex));
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_md5_hexdigest_obj, native_md5_hexdigest);

static mp_obj_t native_md5_copy(mp_obj_t self_in) {
    wasm_md5_object_t *self = MP_OBJ_TO_PTR(self_in);
    wasm_md5_object_t *clone = mp_obj_malloc(wasm_md5_object_t, self->base.type);
    clone->ctx = self->ctx;
    return MP_OBJ_FROM_PTR(clone);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_md5_copy_obj, native_md5_copy);

static const mp_rom_map_elem_t native_md5_methods_table[] = {
    { MP_ROM_QSTR(MP_QSTR_update), MP_ROM_PTR(&native_md5_update_obj) },
    { MP_ROM_QSTR(MP_QSTR_digest), MP_ROM_PTR(&native_md5_digest_obj) },
    { MP_ROM_QSTR(MP_QSTR_hexdigest), MP_ROM_PTR(&native_md5_hexdigest_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&native_md5_copy_obj) },
    { MP_ROM_QSTR(MP_QSTR_digest_size), MP_ROM_INT(16) },
    { MP_ROM_QSTR(MP_QSTR_block_size), MP_ROM_INT(64) },
    { MP_ROM_QSTR(MP_QSTR_name), MP_ROM_QSTR(MP_QSTR_md5) },
};
static MP_DEFINE_CONST_DICT(native_md5_methods, native_md5_methods_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    native_md5_type,
    MP_QSTR_MD5,
    MP_TYPE_FLAG_NONE,
    make_new, native_md5_make_new,
    locals_dict, &native_md5_methods
);

static const mp_rom_map_elem_t native_md5_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__hashlib_native) },
    { MP_ROM_QSTR(MP_QSTR_md5), MP_ROM_PTR(&native_md5_type) },
};
static MP_DEFINE_CONST_DICT(native_md5_globals, native_md5_globals_table);

const mp_obj_module_t native_md5_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&native_md5_globals,
};
MP_REGISTER_MODULE(MP_QSTR__hashlib_native, native_md5_module);
