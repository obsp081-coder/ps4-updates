/* Known-answer SHA-256 vectors verified against Python hashlib.
 * No PS4 firmware API, network or package material involved.
 */
#include "../../ps4/src/download_sha256.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t hex_nibble(char c) {
    if(c>='0' && c<='9')return (uint8_t)(c-'0');
    if(c>='a' && c<='f')return (uint8_t)(c-'a'+10);
    assert(0 && "invalid hex");
    return 0;
}
static void check_hex(const uint8_t actual[32],const char *expected) {
    uint8_t bytes[32];
    assert(strlen(expected)==64u);
    for(size_t i=0;i<32u;++i)
        bytes[i]=(uint8_t)((hex_nibble(expected[2*i])<<4u)|
                            hex_nibble(expected[2*i+1u]));
    assert(ds_sha256_digest_equal(actual,bytes)==1);
    bytes[0]^=1u;
    assert(ds_sha256_digest_equal(actual,bytes)==0);
    assert(ds_sha256_digest_equal(actual,NULL)==0);
}
static void vector(const uint8_t *data,size_t length,
                   const char *expected,unsigned stride) {
    DsSha256 ctx;
    uint8_t result[32];
    ds_sha256_init(&ctx);
    assert(ds_sha256_update(&ctx,NULL,0u)==1);
    for(size_t i=0;i<length;) {
        const size_t take=length-i<stride?length-i:stride;
        assert(ds_sha256_update(&ctx,data+i,take)==1);
        i+=take;
    }
    assert(ds_sha256_final(&ctx,result)==1);
    check_hex(result,expected);
    assert(ds_sha256_final(&ctx,result)==0);
    assert(ds_sha256_update(&ctx,data,length)==0);
}
int main(void) {
    uint8_t a[256];
    memset(a,'a',sizeof a);
    vector(NULL,0u,
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           1u);
    vector((const uint8_t *)"abc",3u,
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           1u);
    vector(a,55u,
           "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
           7u);
    vector(a,56u,
           "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
           56u);
    vector(a,64u,
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
           2u);
    vector(a,65u,
           "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0",
           5u);
    for(unsigned i=0u;i<256u;++i)a[i]=(uint8_t)i;
    vector(a,256u,
           "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880",
           31u);
    static const uint8_t ones[1000]={0}; /* Filled with 'a' below. */
    (void)ones;
    uint8_t block[1000];
    memset(block,'a',sizeof block);
    DsSha256 million;
    ds_sha256_init(&million);
    for(unsigned i=0;i<1000u;++i)
        assert(ds_sha256_update(&million,block,sizeof block)==1);
    uint8_t digest[32];
    assert(ds_sha256_final(&million,digest)==1);
    check_hex(digest,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    DsSha256 broken;
    ds_sha256_init(&broken);
    assert(ds_sha256_update(&broken,NULL,1u)==0);
    assert(ds_sha256_final(&broken,digest)==0);
    ds_sha256_init(&broken);
    assert(ds_sha256_update(&broken,(const uint8_t *)"a",1u)==1);
    /* Artificial length guard; must fail closed instead of wrapping bits. */
    broken.total_bytes=UINT64_MAX/8u;
    assert(ds_sha256_update(&broken,(const uint8_t *)"a",1u)==0);
    assert(ds_sha256_final(&broken,digest)==0);
    puts("NATIVE_SHA256_HOST_OK");
    return 0;
}
