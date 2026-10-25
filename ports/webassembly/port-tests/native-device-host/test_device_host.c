#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device/device_host.h"
static char error[256];
void transport_set_error(const char *s) {snprintf(error,sizeof(error),"%s",s);}
const char *transport_last_error(void) {return error;}
int device_host_invoke_js(const char *kind,const char *operation,
    const char *arguments,char *response,int capacity) {
    assert(!strcmp(kind,"usb"));
    assert(!strcmp(operation,"list_devices"));
    assert(!strcmp(arguments,"{}"));
    const char *payload="{\"ok\":true,\"value\":[]}";
    if ((int)strlen(payload)+1>capacity) return -2;
    strcpy(response,payload);
    return (int)strlen(payload);
}
int main(void) {
    char out[64];
    assert(device_host_request("usb","list_devices","{}",out,sizeof(out))>0);
    assert(strstr(out,"\"ok\":true"));
    assert(device_host_request("serial","", "{}",out,sizeof(out))<0);
    assert(device_host_request("usb","list_devices","{}",out,2)==-2);
    assert(strstr(transport_last_error(),"large"));
    puts("NATIVE_DEVICE_HOST_OK");
}
