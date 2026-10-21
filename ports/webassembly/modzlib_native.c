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

/* Stateful inflate for zlib.decompressobj().
 *
 * A DeflateIO decoder cannot resume after temporary input exhaustion.  Using
 * a persistent z_stream instead avoids replaying the entire compressed prefix
 * for each feed (quadratic runtime and GC allocation for long input streams).
 * All native input pointers are cleared before returning to Python.  This
 * makes it safe for the caller to release or reuse the input buffer.
 */
#include <string.h>
#include "py/obj.h"
#include "py/misc.h"

typedef struct {
    mp_obj_base_t base;
    z_stream stream;
    mp_obj_t dictionary;
    bool open;
    bool eof;
} native_zlib_inflate_t;

static void native_zlib_inflate_release(native_zlib_inflate_t *self) {
    if (self->open) {
        inflateEnd(&self->stream);
        self->open = false;
    }
    self->stream.next_in = Z_NULL;
    self->stream.avail_in = 0;
}

static mp_obj_t native_zlib_inflate_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 2, false);
    mp_int_t wbits = mp_obj_get_int(args[0]);
    native_zlib_inflate_t *self = mp_obj_malloc_with_finaliser(native_zlib_inflate_t, type);
    memset(&self->stream, 0, sizeof(self->stream));
    self->dictionary = n_args == 2 ? args[1] : mp_const_none;
    self->open = false;
    self->eof = false;
    int rc = inflateInit2(&self->stream, wbits);
    if (rc != Z_OK) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid inflate initialization"));
    }
    self->open = true;
    // With raw DEFLATE there is no FDICT header / Z_NEED_DICT handshake.
    if (wbits < 0 && self->dictionary != mp_const_none) {
        mp_buffer_info_t dict;
        mp_get_buffer_raise(self->dictionary, &dict, MP_BUFFER_READ);
        if (dict.len > UINT_MAX || inflateSetDictionary(&self->stream, dict.buf, (uInt)dict.len) != Z_OK) {
            native_zlib_inflate_release(self);
            mp_raise_ValueError(MP_ERROR_TEXT("invalid raw inflate dictionary"));
        }
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t native_zlib_inflate_feed(size_t n_args, const mp_obj_t *args) {
    native_zlib_inflate_t *self = MP_OBJ_TO_PTR(args[0]);
    if (!self->open) {
        mp_raise_ValueError(MP_ERROR_TEXT("inflate stream is closed"));
    }
    mp_buffer_info_t input;
    mp_get_buffer_raise(args[1], &input, MP_BUFFER_READ);
    if (input.len > UINT_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("inflate input exceeds zlib limit"));
    }
    mp_int_t limit = n_args == 3 ? mp_obj_get_int(args[2]) : 0;
    if (limit < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("negative output limit"));
    }
    self->stream.next_in = (Bytef *)input.buf;
    self->stream.avail_in = (uInt)input.len;

    vstr_t output;
    vstr_init(&output, 0);
    while (!self->eof) {
        size_t space = 16384;
        if (limit != 0 && (size_t)limit - output.len < space) {
            space = (size_t)limit - output.len;
        }
        if (space == 0) {
            break;
        }
        Bytef *dst = (Bytef *)vstr_add_len(&output, space);
        self->stream.next_out = dst;
        self->stream.avail_out = (uInt)space;
        uInt before_in = self->stream.avail_in;
        int rc = inflate(&self->stream, Z_NO_FLUSH);
        size_t produced = space - self->stream.avail_out;
        vstr_cut_tail_bytes(&output, space - produced);
        if (rc == Z_NEED_DICT && self->dictionary != mp_const_none) {
            mp_buffer_info_t dict;
            mp_get_buffer_raise(self->dictionary, &dict, MP_BUFFER_READ);
            if (dict.len <= UINT_MAX) {
                rc = inflateSetDictionary(&self->stream, dict.buf, (uInt)dict.len);
                if (rc == Z_OK) {
                    continue;
                }
            }
        }
        if (rc == Z_STREAM_END) {
            self->eof = true;
            break;
        }
        if (rc != Z_OK && rc != Z_BUF_ERROR) {
            self->stream.next_in = Z_NULL;
            self->stream.avail_in = 0;
            mp_raise_ValueError(MP_ERROR_TEXT("invalid or corrupted compressed data"));
        }
        // No more input and the output buffer was not filled: the stream
        // needs another feed.  A zero-progress iteration must never spin.
        if ((produced == 0 && before_in == self->stream.avail_in)
            || (self->stream.avail_in == 0 && produced < space)) {
            break;
        }
    }
    size_t consumed = input.len - self->stream.avail_in;
    self->stream.next_in = Z_NULL;
    self->stream.avail_in = 0;
    self->stream.next_out = Z_NULL;
    self->stream.avail_out = 0;
    mp_obj_t result[3] = {
        mp_obj_new_bytes_from_vstr(&output),
        mp_obj_new_int_from_uint(consumed),
        mp_obj_new_bool(self->eof),
    };
    return mp_obj_new_tuple(3, result);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(native_zlib_inflate_feed_obj, 2, 3, native_zlib_inflate_feed);

static mp_obj_t native_zlib_inflate_copy(mp_obj_t self_in) {
    native_zlib_inflate_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->open) {
        mp_raise_ValueError(MP_ERROR_TEXT("inflate stream is closed"));
    }
    native_zlib_inflate_t *other = mp_obj_malloc_with_finaliser(native_zlib_inflate_t, self->base.type);
    memset(&other->stream, 0, sizeof(other->stream));
    other->dictionary = self->dictionary;
    other->open = false;
    other->eof = self->eof;
    if (inflateCopy(&other->stream, &self->stream) != Z_OK) {
        mp_raise_OSError(MP_ENOMEM);
    }
    other->open = true;
    other->stream.next_in = Z_NULL;
    other->stream.avail_in = 0;
    other->stream.next_out = Z_NULL;
    other->stream.avail_out = 0;
    return MP_OBJ_FROM_PTR(other);
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_zlib_inflate_copy_obj, native_zlib_inflate_copy);

static mp_obj_t native_zlib_inflate_close(mp_obj_t self_in) {
    native_zlib_inflate_release(MP_OBJ_TO_PTR(self_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(native_zlib_inflate_close_obj, native_zlib_inflate_close);

static const mp_rom_map_elem_t native_zlib_inflate_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&native_zlib_inflate_feed_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy), MP_ROM_PTR(&native_zlib_inflate_copy_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&native_zlib_inflate_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&native_zlib_inflate_close_obj) },
};
static MP_DEFINE_CONST_DICT(native_zlib_inflate_locals, native_zlib_inflate_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    native_zlib_inflate_type,
    MP_QSTR_Inflate,
    MP_TYPE_FLAG_NONE,
    make_new, native_zlib_inflate_make_new,
    locals_dict, &native_zlib_inflate_locals
);

static const mp_rom_map_elem_t native_zlib_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__zlib_native) },
    { MP_ROM_QSTR(MP_QSTR_compress), MP_ROM_PTR(&native_zlib_compress_obj) },
    { MP_ROM_QSTR(MP_QSTR_Inflate), MP_ROM_PTR(&native_zlib_inflate_type) },
};
static MP_DEFINE_CONST_DICT(native_zlib_globals, native_zlib_globals_table);

const mp_obj_module_t native_zlib_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&native_zlib_globals,
};
MP_REGISTER_MODULE(MP_QSTR__zlib_native, native_zlib_module);

