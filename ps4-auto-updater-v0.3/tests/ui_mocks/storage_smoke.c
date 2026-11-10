/* Host POSIX fixture backend only. Native PS4 runtime remains READ ONLY.
 * This test hashes actual stored bytes with the native C SHA-256 verifier.
 * It does not implement or verify a PS4 filesystem backend.
 */
#define _POSIX_C_SOURCE 200809L
#include "../../ps4/src/download_storage.h"
#include "../../ps4/src/download_queue.h"
#include "../../ps4/src/download_sha256.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *CID="IV0000-CUSA02343_00-SCANNER000000000";
static const uint8_t PAYLOAD[]="HOST-ONLY-BYTES-not-a-game-pkg-0123456789";
/* Independent Python hashlib.sha256(PAYLOAD) reference; 41 bytes. */
static const uint8_t DIGEST[32]={
    0x09u,0x0au,0x7bu,0x33u,0xdbu,0x4du,0x25u,0xdeu,0xa8u,0x0fu,0x9cu,0x87u,0xc1u,0x72u,0x59u,0x1au,
    0xfeu,0xd2u,0x25u,0xd0u,0x37u,0x4au,0xd2u,0x43u,0x05u,0x77u,0xe5u,0x5bu,0xc4u,0x7bu,0x3au,0x3fu
};

typedef struct Fixture {
    char root[128];
    int fd;
    int fail_write,fail_flush,fail_verify,fail_publish,fail_unlink;
    int fail_read;
} Fixture;

static void path_for(char *out,size_t cap,const Fixture *f,const char *leaf) {
    assert(leaf && !strchr(leaf,'/') && !strstr(leaf,".."));
    const int n=snprintf(out,cap,"%s/%s",f->root,leaf);
    assert(n>0 && (size_t)n<cap);
}
static int host_create(void *ctx,const char *leaf) {
    Fixture *f=(Fixture *)ctx;
    char path[256];
    path_for(path,sizeof path,f,leaf);
    assert(f->fd==-1);
    f->fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    return f->fd>=0 ? 0 : -1;
}
static int host_write(void *ctx,const uint8_t *bytes,size_t length) {
    Fixture *f=(Fixture *)ctx;
    if(f->fail_write) {
        if(length && write(f->fd,bytes,1u)!=1)return -1;
        return -1; /* Simulate partial write followed by I/O error. */
    }
    size_t done=0;
    while(done<length) {
        const ssize_t n=write(f->fd,bytes+done,length-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return -1;
        done+=(size_t)n;
    }
    return 0;
}
static int host_flush(void *ctx) {
    Fixture *f=(Fixture *)ctx;
    return f->fail_flush ? -1 : fsync(f->fd);
}
static int host_close(void *ctx) {
    Fixture *f=(Fixture *)ctx;
    assert(f->fd>=0);
    const int rc=close(f->fd);
    f->fd=-1;
    return rc;
}
static int host_open_read(void *ctx,const char *leaf) {
    Fixture *f=(Fixture *)ctx;
    char path[256];
    path_for(path,sizeof path,f,leaf);
    if(f->fd!=-1)return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return -1;
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode)) {
        const int rc=close(fd);
        (void)rc;
        return -1;
    }
    f->fd=fd;
    return 0;
}
static int host_read_next(void *ctx,uint8_t *buf,size_t cap,size_t *got) {
    Fixture *f=(Fixture *)ctx;
    if(f->fail_read)return -1;
    ssize_t n;
    do {n=read(f->fd,buf,cap);} while(n<0 && errno==EINTR);
    if(n<0)return -1;
    *got=(size_t)n;
    return 0;
}
static int host_verify(void *ctx,const char *leaf,uint64_t expected_size,
                       const uint8_t digest[32]) {
    Fixture *f=(Fixture *)ctx;
    if(f->fail_verify)return 0;
    DsSha256Reader r={0};
    r.context=ctx;
    r.open_read=host_open_read;
    r.read_next=host_read_next;
    r.close_read=host_close;
    return ds_sha256_verify_stored(&r,leaf,expected_size,digest);
}
static int host_publish(void *ctx,const char *part,const char *final) {
    Fixture *f=(Fixture *)ctx;
    char src[256],dst[256];
    path_for(src,sizeof src,f,part);
    path_for(dst,sizeof dst,f,final);
    return f->fail_publish ? -1 : link(src,dst);
}
static int host_unlink(void *ctx,const char *leaf) {
    Fixture *f=(Fixture *)ctx;
    char path[256];
    path_for(path,sizeof path,f,leaf);
    return f->fail_unlink ? -1 : unlink(path);
}
static DsPort port_for(Fixture *f) {
    DsPort p={0};
    p.context=f;
    p.create_exclusive=host_create;
    p.write_all=host_write;
    p.flush=host_flush;
    p.close=host_close;
    p.verify_sha256=host_verify;
    p.publish_exclusive=host_publish;
    p.unlink_part=host_unlink;
    return p;
}
static void assert_file(const char *path,const uint8_t *bytes,size_t length) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW);
    assert(fd>=0);
    uint8_t buffer[128];
    assert(length<sizeof buffer);
    const ssize_t n=read(fd,buffer,sizeof buffer);
    assert(n==(ssize_t)length);
    assert(!memcmp(buffer,bytes,length));
    assert(close(fd)==0);
}
static void cleanup_path(const char *path) {
    assert(unlink(path)==0);
}
static void begin(DsSession *s,const DsPort *port) {
    ds_init(s);
    assert(ds_begin(s,port,CID,"01.09",sizeof PAYLOAD-1u,DIGEST)==DS_OK);
}
int main(void) {
    char root[]="/tmp/ps4-storage-XXXXXX";
    assert(mkdtemp(root));
    Fixture f={0};
    assert(snprintf(f.root,sizeof f.root,"%s",root)>0);
    f.fd=-1;
    DsPort port=port_for(&f);
    DsSession s;
    ds_init(&s);
    assert(ds_begin(&s,&port,"INVALID","01.09",10,DIGEST)==DS_INVALID);
    assert(ds_begin(&s,&port,CID,"../a",10,DIGEST)==DS_INVALID);
    assert(ds_begin(&s,&port,CID,"01.09",0,DIGEST)==DS_INVALID);
    assert(ds_begin(&s,&port,CID,"01.09",10,NULL)==DS_INVALID);
    assert(ds_abort(&s)==DS_BAD_STATE);

    /* Real on-disk write, length tracking and exclusive publish. */
    begin(&s,&port);
    char part[256],final[256];
    path_for(part,sizeof part,&f,s.part_leaf);
    path_for(final,sizeof final,&f,s.final_leaf);
    assert(strstr(s.final_leaf,".bin") && strstr(s.part_leaf,".bin.part"));
    assert(ds_begin(&s,&port,CID,"01.09",1,DIGEST)==DS_BAD_STATE);
    assert(ds_write(&s,PAYLOAD,7)==DS_OK);
    assert(ds_finalize(&s)==DS_SIZE_EXCEEDED);
    assert(ds_write(&s,PAYLOAD+7,sizeof PAYLOAD-1u-7u)==DS_OK);
    assert(ds_write(&s,PAYLOAD,1)==DS_SIZE_EXCEEDED);
    assert(ds_finalize(&s)==DS_OK);
    assert(s.state==DS_PUBLISHED && !s.owns_part && !s.file_open);
    assert(access(part,F_OK)!=0);
    assert_file(final,PAYLOAD,sizeof PAYLOAD-1u);
    assert(ds_abort(&s)==DS_BAD_STATE);
    cleanup_path(final);

    /* Pre-existing .part belongs to nobody in this new session. */
    int prior=open(part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    assert(prior>=0);
    assert(write(prior,"preserve",8)==8);
    assert(close(prior)==0);
    ds_init(&s);
    assert(ds_begin(&s,&port,CID,"01.09",sizeof PAYLOAD-1u,DIGEST)
           ==DS_OPEN_FAILED);
    assert(s.state==DS_EMPTY && !s.owns_part);
    assert(ds_abort(&s)==DS_BAD_STATE);
    assert_file(part,(const uint8_t *)"preserve",8);
    cleanup_path(part);

    /* Existing final file MUST NOT be replaced by a verified .part. */
    prior=open(final,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    assert(prior>=0 && write(prior,"original",8)==8);
    assert(close(prior)==0);
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    assert(ds_finalize(&s)==DS_PUBLISH_FAILED);
    assert(s.state==DS_FAILED && s.owns_part);
    assert_file(final,(const uint8_t *)"original",8);
    assert(ds_abort(&s)==DS_OK);
    assert(access(part,F_OK)!=0);
    assert_file(final,(const uint8_t *)"original",8);
    cleanup_path(final);

    /* Partial I/O failure cannot be accepted as complete. */
    begin(&s,&port);
    f.fail_write=1;
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_WRITE_FAILED);
    f.fail_write=0;
    assert(ds_finalize(&s)==DS_BAD_STATE);
    assert(s.written_size==0u);
    assert(ds_abort(&s)==DS_OK);
    assert(access(part,F_OK)!=0 && access(final,F_OK)!=0);

    /* Cryptographic verification failure keeps .part, never final. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    f.fail_verify=1;
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    f.fail_verify=0;
    assert(s.state==DS_FAILED && access(part,F_OK)==0);
    assert(access(final,F_OK)!=0);
    assert(ds_abort(&s)==DS_OK && access(part,F_OK)!=0);

    /* One-byte mutation after write is rejected even with same length. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    int mutation=open(part,O_WRONLY|O_NOFOLLOW);
    assert(mutation>=0);
    assert(pwrite(mutation,"X",1u,0)==1);
    assert(close(mutation)==0);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(access(final,F_OK)!=0 && access(part,F_OK)==0);
    assert(ds_abort(&s)==DS_OK && access(part,F_OK)!=0);

    /* A truncated stored file cannot pass even if the writer counted all
     * expected bytes. The verifier independently checks the file length. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    assert(truncate(part,7)==0);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(access(final,F_OK)!=0);
    assert(ds_abort(&s)==DS_OK);

    /* Append an extra byte: stored length differs from expected. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    mutation=open(part,O_WRONLY|O_NOFOLLOW);
    assert(mutation>=0);
    assert(pwrite(mutation,"Z",1u,(off_t)(sizeof PAYLOAD-1u))==1);
    assert(close(mutation)==0);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(access(final,F_OK)!=0);
    assert(ds_abort(&s)==DS_OK);

    /* A fresh session with an incorrect expected digest must fail. */
    uint8_t wrong_digest[32]={0};
    ds_init(&s);
    assert(ds_begin(&s,&port,CID,"01.09",sizeof PAYLOAD-1u,
                    wrong_digest)==DS_OK);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(ds_abort(&s)==DS_OK);

    /* Independent verifier read failure is never mistaken for matching. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    f.fail_read=1;
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    f.fail_read=0;
    assert(ds_abort(&s)==DS_OK);

    /* Failed sync is terminal, and the file can be cleaned up. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    f.fail_flush=1;
    assert(ds_finalize(&s)==DS_FLUSH_FAILED);
    f.fail_flush=0;
    assert(s.state==DS_FAILED);
    assert(ds_abort(&s)==DS_OK);
    assert(access(part,F_OK)!=0);

    /* Successful exclusive publish with failed .part unlink still retains
     * the final verified blob; a later cleanup does NOT delete the final. */
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    f.fail_unlink=1;
    assert(ds_finalize(&s)==DS_CLEANUP_FAILED);
    f.fail_unlink=0;
    assert(s.state==DS_PUBLISHED && s.owns_part);
    assert_file(final,PAYLOAD,sizeof PAYLOAD-1u);
    assert(ds_abort(&s)==DS_OK);
    assert(access(part,F_OK)!=0);
    assert_file(final,PAYLOAD,sizeof PAYLOAD-1u);
    cleanup_path(final);

    /* Integrate queue byte counting with only actually accepted writes. */
    DlqQueue q;
    dlq_init(&q);
    assert(dlq_enqueue(&q,"CUSA02343",CID,"01.09",
                       sizeof PAYLOAD-1u)==0);
    assert(dlq_start(&q,0,sizeof PAYLOAD,0,0,NULL)==DLQ_OK);
    begin(&s,&port);
    assert(ds_write(&s,PAYLOAD,sizeof PAYLOAD-1u)==DS_OK);
    assert(dlq_received(&q,0,sizeof PAYLOAD-1u)==DLQ_OK);
    assert(dlq_begin_verification(&q,0)==DLQ_OK);
    assert(ds_finalize(&s)==DS_OK);
    /* Stored-byte SHA-256 is real; PS4 backend/authorization remains off. */
    assert(dlq_finish_verification(&q,0,1)==DLQ_OK);
    assert(dlq_get(&q,0)->state==DLQ_COMPLETED);
    cleanup_path(final);

    assert(rmdir(f.root)==0);
    puts("DOWNLOAD_STORAGE_HOST_OK");
    return 0;
}
