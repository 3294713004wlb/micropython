#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "device/device_serial.h"
#include "device/device_hid.h"
#include "device/device_usb.h"
#include "transport/transport.h"

static int kind_seen, opcode_seen, a_seen, b_seen;
static unsigned char buffer_seen[512];
static int size_seen, rx_seen, serial_read_count, serial_write_count;
static char error_text[256];

int transport_serial_available(void) { return 5; }
int transport_serial_read(uint8_t *out, int size) {
    serial_read_count++;
    if (size) memset(out, 0x5a, (size_t)size);
    return size;
}
int transport_serial_write(const uint8_t *data, int size) {
    serial_write_count++;
    assert(!size || data[0] == 0x11);
    return size;
}
int transport_serial_wait(int ms) { return ms == 50; }
int transport_transfer(int kind, int opcode, const char *id,
    int a, int b, const uint8_t *tx, int tx_len,
    uint8_t *rx, int rx_size, int timeout) {
    (void)timeout;
    kind_seen=kind; opcode_seen=opcode; a_seen=a; b_seen=b;
    if (kind==TP_KIND_HID) assert(!strcmp(id, "hid/1"));
    size_seen=tx_len; rx_seen=rx_size;
    if (tx_len) { assert(tx_len<=512); memcpy(buffer_seen,tx,(size_t)tx_len); }
    if (rx_size) { memset(rx, 0x42, (size_t)rx_size); }
    return rx_size ? rx_size : tx_len;
}
int transport_usb_control(int op,int bm,int req,int value,int index,
    const uint8_t *tx,int tx_len,uint8_t *rx,int rx_len,int timeout) {
    (void)req; (void)value; (void)index; (void)timeout;
    kind_seen=TP_KIND_USB;opcode_seen=op;a_seen=bm;
    size_seen=tx_len;rx_seen=rx_len;
    if (tx_len) assert(tx);
    if (rx_len) memset(rx,0x40,(size_t)rx_len);
    return rx_len ? rx_len : tx_len;
}
int transport_usb_control_for(const char *id,int op,int bm,int req,int value,int index,
    const uint8_t *tx,int tx_len,uint8_t *rx,int rx_len,int timeout) {
    assert(id && strlen(id) < TP_REGION_SIZE_USB_ID);
    return transport_usb_control(op,bm,req,value,index,tx,tx_len,rx,rx_len,timeout);
}
void transport_set_error(const char *message) {
    snprintf(error_text,sizeof(error_text),"%s",message);
}
const char *transport_last_error(void) { return error_text; }
static int pipe_kind;
int transport_usb_pipe_submit(int out,int in,const uint8_t *data,size_t size,size_t response_len,int timeout,bool send_zlp) {
    (void)out;(void)in;(void)timeout; assert(response_len==64 && !send_zlp);assert(data);pipe_kind=1;return (int)size;
}
int transport_usb_pipe_submit_typed(int out,int in,const uint8_t *data,size_t size,int timeout,uint8_t type,size_t response_len,bool send_zlp) {
    (void)out;(void)in;(void)timeout;assert(data && response_len==64 && !send_zlp);pipe_kind=type;return (int)size;
}
int transport_usb_pipe_take(int out,int in,uint8_t *data,size_t size,int timeout) {
    (void)out;(void)in;(void)timeout;memset(data,0x13,size);return (int)size;
}
int transport_usb_pipe_reset(void) {pipe_kind=9;return 0;}
int transport_usb_pipe_available(void) {return 8;}
uint64_t transport_usb_pipe_last_arrival(void) {return 9001;}

int main(void) {
    uint8_t tx[4]={0x11,1,2,3},rx[16]={0};
    assert(device_serial_available()==5);
    assert(device_serial_write(tx,4)==4 && serial_write_count==1);
    assert(device_serial_read(rx,4)==4 && serial_read_count==1 && rx[0]==0x5a);
    assert(device_serial_wait(50)==1 && device_serial_wait(0)==0);
    assert(device_serial_read(NULL,1)<0 && strstr(error_text,"Serial"));
    assert(device_hid_write("hid/1",tx,4)==4 && kind_seen==TP_KIND_HID && opcode_seen==TP_OPCODE_HID_WRITE);
    assert(device_hid_read("hid/1",rx,5,200)==5 && opcode_seen==TP_OPCODE_HID_READ && rx[4]==0x42);
    assert(device_hid_feature_read("hid/1",7,rx,5)==5 && a_seen==7 && opcode_seen==TP_OPCODE_HID_FEATURE_READ);
    assert(device_hid_exchange_batch("hid/1",tx,4,rx,5,200)==5 && opcode_seen==6);
    assert(device_hid_stream_reset("hid/1")==0 && opcode_seen==9);
    assert(device_hid_read("a",NULL,8,100)==-1 && strstr(error_text,"HID"));
    assert(device_usb_out_for("usb/1",2,tx,4,100)==4 && kind_seen==TP_KIND_USB && opcode_seen==TP_OPCODE_USB_OUT && a_seen==2);
    assert(device_usb_in_for("usb/2",3,rx,8,100)==8 && opcode_seen==TP_OPCODE_USB_IN && b_seen==3);
    assert(device_usb_out_in_for("usb/1",1,4,tx,4,rx,8,50)==8 && opcode_seen==TP_OPCODE_USB_OUT_IN);
    assert(device_usb_control_for("usb/1",0x80,1,0,0,NULL,0,rx,5,100)==5 && opcode_seen==TP_OPCODE_USB_CONTROL_IN);
    assert(device_usb_stream_submit(1,2,tx,4,50)==4 && opcode_seen==7);
    assert(device_usb_stream_take(1,2,rx,5,50)==5 && opcode_seen==8);
    assert(device_usb_stream_reset(1,2)==0 && opcode_seen==9);
    assert(device_usb_repeat(1,2,1,tx,4,NULL,0,10)==4 && opcode_seen==TP_OPCODE_USB_REPEAT_WRITE);
    assert(device_usb_pipe_submit(1,2,tx,4,64,50,false)==4 && pipe_kind==1);
    assert(device_usb_pipe_submit_typed(1,2,tx,4,50,3,64,false)==4 && pipe_kind==3);
    assert(device_usb_pipe_take(1,2,rx,8,50)==8 && rx[0]==0x13);
    assert(device_usb_pipe_reset()==0 && pipe_kind==9);
    assert(device_usb_pipe_available()==8 && device_usb_pipe_last_arrival()==9001);
    assert(device_usb_out_for("usb/1",2,tx,4,100)==4 && kind_seen==TP_KIND_USB);
    assert(device_usb_in_for("usb/2",3,rx,8,100)==8);
    assert(device_usb_control_for("usb/1",0x80,1,0,0,NULL,0,rx,5,100)==5);
    assert(device_usb_out_for("usb/1",1,NULL,4,0)<0 && strstr(error_text,"USB"));
    puts("native device layer: 27 API and validation checks passed");
    return 0;
}
