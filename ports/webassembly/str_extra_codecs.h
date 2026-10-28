/*
 * Additional CPython text codecs for the MicroPython WebAssembly port.
 *
 * Kept separate from objstr.c so that upstream encoding logic is unchanged.
 * These functions operate on MicroPython's UTF-8 internal string storage.
 * Encoding modes here are stateless; streaming codecs are a separate concern.
 */

#ifndef MICROPY_WEBASSEMBLY_STR_EXTRA_CODECS_H
#define MICROPY_WEBASSEMBLY_STR_EXTRA_CODECS_H

typedef enum {
    WASM_CODEC_UNKNOWN = 0,
    WASM_CODEC_UTF8_SIG,
    WASM_CODEC_UTF16,
    WASM_CODEC_UTF16_LE,
    WASM_CODEC_UTF16_BE,
    WASM_CODEC_UTF32,
    WASM_CODEC_UTF32_LE,
    WASM_CODEC_UTF32_BE,
    WASM_CODEC_CP1252,
    WASM_CODEC_CP437,
} wasm_text_codec_t;

// Normalise the usual aliases and case variants without allocating memory.
static wasm_text_codec_t wasm_find_text_codec(qstr encoding) {
    const char *name = qstr_str(encoding);
    char normalized[24];
    size_t length = 0;

    for (; *name != '\0'; ++name) {
        char ch = *name;
        if (ch == '-' || ch == '_' || ch == ' ') {
            continue;
        }
        if (length >= sizeof(normalized) - 1) {
            return WASM_CODEC_UNKNOWN;
        }
        if (ch >= 'A' && ch <= 'Z') {
            ch += 'a' - 'A';
        }
        normalized[length++] = ch;
    }
    normalized[length] = '\0';

    if (strcmp(normalized, "utf8sig") == 0) {
        return WASM_CODEC_UTF8_SIG;
    }
    if (strcmp(normalized, "utf16") == 0) {
        // Keep the default and explicit endian modes separate.
        return WASM_CODEC_UTF16;
    }
    if (strcmp(normalized, "utf16le") == 0) {
        return WASM_CODEC_UTF16_LE;
    }
    if (strcmp(normalized, "utf16be") == 0) {
        return WASM_CODEC_UTF16_BE;
    }
    if (strcmp(normalized, "utf32") == 0) {
        return WASM_CODEC_UTF32;
    }
    if (strcmp(normalized, "utf32le") == 0) {
        return WASM_CODEC_UTF32_LE;
    }
    if (strcmp(normalized, "utf32be") == 0) {
        return WASM_CODEC_UTF32_BE;
    }
    if (strcmp(normalized, "cp1252") == 0 || strcmp(normalized, "windows1252") == 0) {
        return WASM_CODEC_CP1252;
    }
    if (strcmp(normalized, "cp437") == 0 || strcmp(normalized, "ibm437") == 0) {
        return WASM_CODEC_CP437;
    }
    return WASM_CODEC_UNKNOWN;
}

static const uint16_t wasm_cp1252_extra[32] = {
    0x20ac, 0xffff, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xffff, 0x017d, 0xffff,
    0xffff, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xffff, 0x017e, 0x0178,
};

static const uint16_t wasm_cp437_extra[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

static bool wasm_codec_is_16(wasm_text_codec_t codec) {
    return codec == WASM_CODEC_UTF16 || codec == WASM_CODEC_UTF16_LE
        || codec == WASM_CODEC_UTF16_BE;
}

static bool wasm_codec_is_32(wasm_text_codec_t codec) {
    return codec == WASM_CODEC_UTF32 || codec == WASM_CODEC_UTF32_LE
        || codec == WASM_CODEC_UTF32_BE;
}

static uint32_t wasm_load_code_unit(const byte *source, unsigned count, bool big_endian) {
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned position = big_endian ? i : count - i - 1;
        value = (value << 8) | source[position];
    }
    return value;
}

static void wasm_store_code_unit(vstr_t *result, uint32_t value, unsigned count, bool big_endian) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned shift = 8 * (big_endian ? count - i - 1 : i);
        vstr_add_byte(result, (byte)(value >> shift));
    }
}

static void wasm_codec_decode_error(vstr_t *result, qstr errors) {
    if (errors == MP_QSTR_ignore) {
        return;
    }
    if (errors == MP_QSTR_replace) {
        vstr_add_char(result, 0xfffd);
        return;
    }
    mp_raise_msg(&mp_type_UnicodeDecodeError, MP_ERROR_TEXT("invalid encoded character"));
}

static void wasm_codec_encode_error(vstr_t *result, qstr errors) {
    if (errors == MP_QSTR_ignore) {
        return;
    }
    if (errors == MP_QSTR_replace) {
        vstr_add_byte(result, '?');
        return;
    }
    mp_raise_msg(&mp_type_UnicodeEncodeError, MP_ERROR_TEXT("character cannot be encoded"));
}

static mp_obj_t wasm_decode_text_codec(
    wasm_text_codec_t codec,
    const mp_obj_type_t *type,
    const byte *data,
    size_t length,
    qstr errors
) {
    if (codec == WASM_CODEC_UTF8_SIG) {
        static const byte bom[] = {0xef, 0xbb, 0xbf};
        if (length >= 3 && memcmp(data, bom, 3) == 0) {
            data += 3;
            length -= 3;
        }
        if (!unicode_encoding_check(MP_ENCODING_UTF8, data, length)) {
            #if MICROPY_PY_BUILTINS_BYTES_DECODE_ERRORS
            if (errors == MP_QSTR_ignore || errors == MP_QSTR_replace) {
                return str_from_invalid(MP_ENCODING_UTF8, type, data, length, errors);
            }
            #endif
            mp_raise_msg(&mp_type_UnicodeDecodeError, MP_ERROR_TEXT("invalid UTF-8"));
        }
        return mp_obj_new_str_copy(type, data, length);
    }

    vstr_t result;
    vstr_init(&result, length);
    if (codec == WASM_CODEC_CP1252 || codec == WASM_CODEC_CP437) {
        for (size_t i = 0; i < length; ++i) {
            uint32_t ch = data[i];
            if (codec == WASM_CODEC_CP1252 && ch >= 0x80 && ch <= 0x9f) {
                ch = wasm_cp1252_extra[ch - 0x80];
            } else if (codec == WASM_CODEC_CP437 && ch >= 0x80) {
                ch = wasm_cp437_extra[ch - 0x80];
            }
            if (ch == 0xffff) {
                wasm_codec_decode_error(&result, errors);
            } else {
                vstr_add_char(&result, ch);
            }
        }
        return mp_obj_new_str_type_from_vstr(type, &result);
    }

    bool is16 = wasm_codec_is_16(codec);
    unsigned width = is16 ? 2 : 4;
    bool big_endian = codec == WASM_CODEC_UTF16_BE || codec == WASM_CODEC_UTF32_BE;
    size_t index = 0;
    if (codec == WASM_CODEC_UTF16 || codec == WASM_CODEC_UTF32) {
        if (length >= width) {
            uint32_t first = wasm_load_code_unit(data, width, false);
            if (first == 0xfeff) {
                index = width;
            } else if (first == (is16 ? 0xfffe : 0xfffe0000)) {
                big_endian = true;
                index = width;
            }
        }
    }

    while (index < length) {
        if (length - index < width) {
            wasm_codec_decode_error(&result, errors);
            break;
        }
        uint32_t ch = wasm_load_code_unit(data + index, width, big_endian);
        index += width;
        if (is16 && ch >= 0xd800 && ch <= 0xdbff) {
            if (length - index >= width) {
                uint32_t tail = wasm_load_code_unit(data + index, width, big_endian);
                if (tail >= 0xdc00 && tail <= 0xdfff) {
                    ch = 0x10000 + ((ch - 0xd800) << 10) + (tail - 0xdc00);
                    index += width;
                } else {
                    wasm_codec_decode_error(&result, errors);
                    continue;
                }
            } else {
                wasm_codec_decode_error(&result, errors);
                continue;
            }
        } else if (ch >= 0xd800 && ch <= 0xdfff) {
            wasm_codec_decode_error(&result, errors);
            continue;
        } else if (ch > 0x10ffff) {
            wasm_codec_decode_error(&result, errors);
            continue;
        }
        vstr_add_char(&result, ch);
    }
    return mp_obj_new_str_type_from_vstr(type, &result);
}

static mp_obj_t wasm_encode_text_codec(
    wasm_text_codec_t codec,
    const byte *data,
    size_t length,
    qstr errors
) {
    vstr_t result;
    vstr_init(&result, length + 4);
    if (codec == WASM_CODEC_UTF8_SIG) {
        vstr_add_byte(&result, 0xef);
        vstr_add_byte(&result, 0xbb);
        vstr_add_byte(&result, 0xbf);
        vstr_add_strn(&result, (const char *)data, length);
        return mp_obj_new_bytes_from_vstr(&result);
    }

    bool is16 = wasm_codec_is_16(codec);
    bool is32 = wasm_codec_is_32(codec);
    bool big_endian = codec == WASM_CODEC_UTF16_BE || codec == WASM_CODEC_UTF32_BE;
    unsigned width = is16 ? 2 : 4;
    if (codec == WASM_CODEC_UTF16 || codec == WASM_CODEC_UTF32) {
        wasm_store_code_unit(&result, 0xfeff, width, false);
    }

    const byte *cursor = data;
    const byte *end = data + length;
    while (cursor < end) {
        unichar ch = utf8_get_char(cursor);
        cursor = utf8_next_char(cursor);

        if (is16 || is32) {
            if (ch >= 0xd800 && ch <= 0xdfff) {
                // Replace with the Unicode replacement character, not '?'.
                if (errors == MP_QSTR_ignore) {
                    continue;
                }
                if (errors == MP_QSTR_replace) {
                    ch = '?';
                } else {
                    wasm_codec_encode_error(&result, errors);
                }
            }
            if (is16 && ch >= 0x10000) {
                ch -= 0x10000;
                wasm_store_code_unit(&result, 0xd800 | (ch >> 10), 2, big_endian);
                wasm_store_code_unit(&result, 0xdc00 | (ch & 0x3ff), 2, big_endian);
            } else {
                wasm_store_code_unit(&result, ch, width, big_endian);
            }
            continue;
        }

        uint32_t encoded = 256;
        if (codec == WASM_CODEC_CP1252) {
            if (ch < 0x80 || (ch >= 0xa0 && ch <= 0xff)) {
                encoded = ch;
            } else {
                for (unsigned i = 0; i < 32; ++i) {
                    if (wasm_cp1252_extra[i] == ch) {
                        encoded = i + 0x80;
                        break;
                    }
                }
            }
        } else if (codec == WASM_CODEC_CP437) {
            if (ch < 0x80) {
                encoded = ch;
            } else {
                for (unsigned i = 0; i < 128; ++i) {
                    if (wasm_cp437_extra[i] == ch) {
                        encoded = i + 0x80;
                        break;
                    }
                }
            }
        }
        if (encoded <= 0xff) {
            vstr_add_byte(&result, encoded);
        } else {
            wasm_codec_encode_error(&result, errors);
        }
    }
    return mp_obj_new_bytes_from_vstr(&result);
}

#endif // MICROPY_WEBASSEMBLY_STR_EXTRA_CODECS_H
