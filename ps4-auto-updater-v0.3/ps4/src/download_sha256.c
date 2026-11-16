#include "download_sha256.h"
#include <limits.h>
#include <string.h>

/* Independently authored SHA-256 compression following FIPS 180-4. */
static const uint32_t round_k[64]={
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
    0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
    0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
    0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
    0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
    0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static uint32_t ror(uint32_t x,unsigned n) {
    return (x>>n)|(x<<(32u-n));
}
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static void store_be32(uint8_t *p,uint32_t w) {
    p[0]=(uint8_t)(w>>24);
    p[1]=(uint8_t)(w>>16);
    p[2]=(uint8_t)(w>>8);
    p[3]=(uint8_t)w;
}
static void compress(DsSha256 *ctx,const uint8_t b[64]) {
    uint32_t w[64];
    for(unsigned i=0;i<16u;++i)w[i]=load_be32(b+4u*i);
    for(unsigned i=16u;i<64u;++i) {
        const uint32_t x=w[i-15u],y=w[i-2u];
        const uint32_t s0=ror(x,7u)^ror(x,18u)^(x>>3u);
        const uint32_t s1=ror(y,17u)^ror(y,19u)^(y>>10u);
        w[i]=w[i-16u]+s0+w[i-7u]+s1;
    }
    uint32_t a=ctx->state[0],b0=ctx->state[1],c=ctx->state[2];
    uint32_t d=ctx->state[3],e=ctx->state[4],f=ctx->state[5];
    uint32_t g=ctx->state[6],h=ctx->state[7];
    for(unsigned i=0;i<64u;++i) {
        const uint32_t s1=ror(e,6u)^ror(e,11u)^ror(e,25u);
        const uint32_t ch=(e&f)^(~e&g);
        const uint32_t t1=h+s1+ch+round_k[i]+w[i];
        const uint32_t s0=ror(a,2u)^ror(a,13u)^ror(a,22u);
        const uint32_t maj=(a&b0)^(a&c)^(b0&c);
        const uint32_t t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b0;b0=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b0;ctx->state[2]+=c;
    ctx->state[3]+=d;ctx->state[4]+=e;ctx->state[5]+=f;
    ctx->state[6]+=g;ctx->state[7]+=h;
}
void ds_sha256_init(DsSha256 *ctx) {
    if(!ctx)return;
    memset(ctx,0,sizeof *ctx);
    const uint32_t iv[8]={
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memcpy(ctx->state,iv,sizeof iv);
}
int ds_sha256_update(DsSha256 *ctx,const uint8_t *data,size_t count) {
    if(!ctx || ctx->failed)return 0;
    if(count && !data) {ctx->failed=1u;return 0;}
    if(count>(UINT64_MAX/8u)-ctx->total_bytes) {
        ctx->failed=1u;return 0;
    }
    ctx->total_bytes+=(uint64_t)count;
    while(count) {
        size_t take=64u-ctx->buffered;
        if(take>count)take=count;
        memcpy(ctx->block+ctx->buffered,data,take);
        ctx->buffered+=take;
        data+=take;
        count-=take;
        if(ctx->buffered==64u) {
            compress(ctx,ctx->block);
            ctx->buffered=0u;
        }
    }
    return 1;
}
int ds_sha256_final(DsSha256 *ctx,uint8_t out[32]) {
    if(!ctx || !out || ctx->failed || ctx->buffered>=64u)return 0;
    const uint64_t bits=ctx->total_bytes*8u;
    ctx->block[ctx->buffered++]=0x80u;
    if(ctx->buffered>56u) {
        memset(ctx->block+ctx->buffered,0,64u-ctx->buffered);
        compress(ctx,ctx->block);
        ctx->buffered=0u;
    }
    memset(ctx->block+ctx->buffered,0,56u-ctx->buffered);
    for(unsigned i=0;i<8u;++i)
        ctx->block[56u+i]=(uint8_t)(bits>>(56u-8u*i));
    compress(ctx,ctx->block);
    for(unsigned i=0;i<8u;++i)
        store_be32(out+4u*i,ctx->state[i]);
    memset(ctx,0,sizeof *ctx);
    ctx->failed=1u; /* final() and update() cannot be called twice. */
    return 1;
}
int ds_sha256_digest_equal(const uint8_t a[32],const uint8_t b[32]) {
    if(!a || !b)return 0;
    unsigned diff=0u;
    for(unsigned i=0;i<32u;++i)diff|=(unsigned)(a[i]^b[i]);
    return diff==0u;
}
int ds_sha256_verify_stored(const DsSha256Reader *r,
                            const char *leaf,uint64_t expected_size,
                            const uint8_t expected_digest[32]) {
    if(!r || !r->open_read || !r->read_next || !r->close_read ||
       !leaf || !leaf[0] || !expected_digest ||
       expected_size>UINT64_MAX/8u)return 0;
    if(r->open_read(r->context,leaf)!=0)return 0;
    uint8_t buffer[4096],actual[32];
    DsSha256 hash;
    ds_sha256_init(&hash);
    uint64_t received=0u;
    int ok=1;
    for(;;) {
        size_t got=0u;
        const int rc=r->read_next(r->context,buffer,sizeof buffer,&got);
        if(rc!=0 || got>sizeof buffer) {ok=0;break;}
        if(!got)break;
        if((uint64_t)got>expected_size-received ||
           !ds_sha256_update(&hash,buffer,got)) {ok=0;break;}
        received+=(uint64_t)got;
    }
    if(ok && received!=expected_size)ok=0;
    if(ok && !ds_sha256_final(&hash,actual))ok=0;
    if(r->close_read(r->context)!=0)ok=0;
    return ok && ds_sha256_digest_equal(actual,expected_digest);
}
