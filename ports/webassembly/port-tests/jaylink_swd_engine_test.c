/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../usermods/jlink_native/swd_engine.h"

static unsigned calls, last_bits, wait_once, ack_code, broken_parity;
static uint64_t recorded_raw;
static int use_recorded_raw;
static uint8_t last_dir[32], last_out[32];
static uint32_t expected_idcode = 0x2ba01477;
static unsigned bitget(const uint8_t *b, unsigned i) { return (b[i/8] >> (i%8)) & 1; }
static void bitset(uint8_t *b, unsigned i, unsigned v) { if(v) b[i/8] |= 1u<<(i%8); }
static int fake(void *ctx,const uint8_t *dir,const uint8_t *out,uint8_t *in,uint16_t bits) {
    (void)ctx;
    calls++; last_bits=bits;
    memcpy(last_dir,dir,(bits+7)/8); memcpy(last_out,out,(bits+7)/8);
    memset(in,0,(bits+7)/8);
    if (use_recorded_raw) {
        for (unsigned i = 0; i < 7; ++i) in[i] = (uint8_t)(recorded_raw >> (i * 8));
        return 0;
    }
    unsigned ack = wait_once && calls==1 ? 2u : ack_code;
    for(unsigned i=0;i<3;i++) bitset(in,8+i,(ack>>i)&1);
    if(ack==1) {
      unsigned parity = 0;
      for(unsigned i=0;i<32;i++) { unsigned v=(expected_idcode>>i)&1;bitset(in,11+i,v);parity^=v; }
      bitset(in,43,parity ^ broken_parity);
    }
    return 0;
}
#define T(c) do { if(!(c)){ fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1);} }while(0)
static void init(void) {calls=0;last_bits=0;wait_once=0;ack_code=1;broken_parity=0;recorded_raw=0;use_recorded_raw=0;memset(last_dir,0,sizeof(last_dir));memset(last_out,0,sizeof(last_out));}
int main(void) {
    struct jlink_swd_result r;
    init();
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,20,0,&r)==JLINK_SWD_OK);
    T(r.data==expected_idcode && r.ack==1 && r.request==0xa5 && r.attempts==1);
    T(last_bits==54 && last_out[0]==0xa5);
    for(unsigned i=0;i<8;i++)T(bitget(last_dir,i)==1);
    for(unsigned i=8;i<46;i++)T(bitget(last_dir,i)==0);
    for(unsigned i=46;i<54;i++)T(bitget(last_dir,i)==1);
    init(); wait_once=1;
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,20,0,&r)==JLINK_SWD_OK && calls==2);
    init(); ack_code=7;
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,20,0,&r)==JLINK_SWD_BAD_ACK && r.ack==7);
    init(); ack_code=4;
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,20,0,&r)==JLINK_SWD_FAULT);
    init(); broken_parity=1;
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,20,0,&r)==JLINK_SWD_BAD_PARITY);
    init();
    T(jlink_swd_transfer(fake,NULL,true,false,4,0x80000001,20,2,&r)==JLINK_SWD_OK);
    T(r.request==0x8b && r.bits==56);
    for(unsigned i=8;i<13;i++)T(bitget(last_dir,i)==0);
    for(unsigned i=13;i<56;i++)T(bitget(last_dir,i)==1);
    T(bitget(last_out,13)==1 && bitget(last_out,44)==1);
    /* Real hardware samples from the user's WebUSB J-Link ARM-OB:
     * Old +1 indexing misread DP IDCODE as FAULT and DP ABORT as ACK 0.
     * Same probe/target using OpenOCD reports DPIDR=0x2ba01477. */
    init(); use_recorded_raw=1; recorded_raw=UINT64_C(0x15d00a3b9a5);
    T(jlink_swd_transfer(fake,NULL,false,true,0,0,1,0,&r)==JLINK_SWD_OK);
    T(r.request==0xa5 && r.ack==1 && r.data==UINT32_C(0x2ba01477));
    init(); use_recorded_raw=1; recorded_raw=UINT64_C(0x3c181);
    T(jlink_swd_transfer(fake,NULL,false,false,0,0x1e,1,0,&r)==JLINK_SWD_OK);
    T(r.request==0x81 && r.ack==1);
    init(); ack_code=7;
    T(jlink_swd_transfer(fake,NULL,false,false,12,0,1,0,&r)==JLINK_SWD_OK);
    T(jlink_swd_transfer(fake,NULL,false,true,3,0,1,0,&r)==JLINK_SWD_INVALID_ARGUMENT);
    puts("swd_engine: protocol and recorded-hardware regressions PASSED");
    return 0;
}
