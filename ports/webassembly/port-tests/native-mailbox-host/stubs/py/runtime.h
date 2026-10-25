#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void* mp_obj_t;
typedef long mp_int_t;
typedef unsigned char byte;
typedef struct { void *buf; size_t len; } mp_buffer_info_t;
typedef struct {void *base[1]; void *globals;} mp_obj_module_t;
typedef struct { int x;} mp_obj_dict_t;
typedef struct { int x; int y;} mp_rom_map_elem_t;
extern int mp_type_OSError, mp_type_KeyboardInterrupt, mp_type_module;
extern mp_obj_t mp_const_none;
#define MP_BUFFER_READ 1
#define MP_BUFFER_WRITE 2
#define MP_OBJ_NEW_SMALL_INT(n) ((mp_obj_t)(intptr_t)(n))
#define MP_ERROR_TEXT(s) (s)
#define MP_DEFINE_CONST_FUN_OBJ_0(name,fun) int name
#define MP_DEFINE_CONST_FUN_OBJ_1(name,fun) int name
#define MP_DEFINE_CONST_FUN_OBJ_2(name,fun) int name
#define MP_DEFINE_CONST_FUN_OBJ_3(name,fun) int name
#define MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(name,min,max,fun) int name
#define MP_ROM_QSTR(x) 0
#define MP_QSTR___name__ 0
#define MP_QSTR_wasm_device 0
#define MP_QSTR_serial_available 0
#define MP_QSTR_serial_read 0
#define MP_QSTR_serial_readinto 0
#define MP_QSTR_serial_write 0
#define MP_QSTR_serial_wait 0
#define MP_QSTR_usb_out 0
#define MP_QSTR_usb_in 0
#define MP_QSTR_usb_out_in 0
#define MP_QSTR_usb_control_transfer 0
#define MP_QSTR_hid_write 0
#define MP_QSTR_hid_read 0
#define MP_QSTR_hid_output 0
#define MP_QSTR_hid_feature_write 0
#define MP_QSTR_hid_feature_read 0
#define MP_ROM_PTR(p) 0
#define MP_DEFINE_CONST_DICT(name,table) int name
#define MP_REGISTER_MODULE(q,module)
#define m_new(type,count) ((type*)malloc(sizeof(type)*(count)))
#define m_del(type,ptr,count) free(ptr)
mp_int_t mp_obj_get_int(mp_obj_t);
void mp_raise_ValueError(const char*);
void mp_raise_msg_varg(void*,const char*,...);
void mp_raise_type(void*);
void mp_get_buffer_raise(mp_obj_t,mp_buffer_info_t*,int);
mp_obj_t mp_obj_new_bytes(const byte*,size_t);
mp_obj_t mp_obj_new_str(const char*,size_t);
mp_obj_t mp_obj_new_int(mp_int_t);
mp_obj_t mp_obj_new_bool(bool);
const char* mp_obj_str_get_str(mp_obj_t);
/* Real MicroPython py/obj.h declares this API; keep the host stub in sync. */
mp_obj_t mp_obj_new_int_from_ull(unsigned long long value);
