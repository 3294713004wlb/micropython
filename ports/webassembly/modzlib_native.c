/* Native zlib backend for the browser MicroPython port.
 *
 * Keep the public CPython-compatible zlib API in Python's zlib.py; expose
 * only the performance-critical deflate operation through this module.
 */
#include <limits.h>
#include <stdlib.h>
#include <zlib.h>

#include "py/runtime.h"
#include "py/objstr.h"
#include "py/mperrno.h"

static mp_obj_t native_zlib_compress(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t source;
    mp_get_buffer_raise(args[0], &source, MP_BUFFER_READ);
    mp_int_t level = n_args == 2 ? mp_obj_get_int(args[1]) : Z_DEFAULT_COMPRESSION;
    if (level < Z_DEFAULT_COMPRESSION || level > Z_BEST_COMPRESSION) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid compression level"));
    }
    uLong src_len = (uLong)source.len;
    uLongf capacity = compressBound(src_len);
    // A zero-length allocation is not useful and may be platform dependent.
    if (capacity == 0 || capacity < src_len) {
        mp_raise_ValueError(MP_ERROR_TEXT("input too large"));
    }
    Bytef *buffer = malloc((size_t)capacity);
    if (buffer == NULL) {
        mp_raise_OSError(MP_ENOMEM);
    }
    uLongf out_len = capacity;
    int result = compress2(buffer, &out_len, (const Bytef *)source.buf, src_len, (int)level);
    if (result != Z_OK) {
        free(buffer);
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("native zlib compression failed"));
    }
    mp_obj_t output = mp_obj_new_bytes(buffer, (size_t)out_len);
    free(buffer);
    return output;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_zlib_compress_obj, 1, 2, native_zlib_compress);

static const mp_rom_map_elem_t native_zlib_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__zlib_native) },
    { MP_ROM_QSTR(MP_QSTR_compress), MP_ROM_PTR(&native_zlib_compress_obj) },
};
static MP_DEFINE_CONST_DICT(native_zlib_globals, native_zlib_globals_table);

const mp_obj_module_t native_zlib_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&native_zlib_globals,
};
MP_REGISTER_MODULE(MP_QSTR__zlib_native, native_zlib_module);
