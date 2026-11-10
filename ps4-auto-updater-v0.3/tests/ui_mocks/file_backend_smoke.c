/* Stage 5: real isolated POSIX file-backend integration on Host only.
 * This test never uses PS4 APIs, user games, FTP or update PKG files.
 */
#define _GNU_SOURCE 1
#include "../../ps4/src/download_file_backend.h"
#include "../../ps4/src/download_sha256.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *CID="IV0000-CUSA02343_00-SCANNER000000000";
static const uint8_t payload[]="only-my-own-host-test-fixture-2026";
static uint8_t digest[32];

static void make_digest(void) {
    DsSha256 hash;
    ds_sha256_init(&hash);
    assert(ds_sha256_update(&hash,payload,sizeof payload-1u));
    assert(ds_sha256_final(&hash,digest));
}
static void begin(DsSession *s,const DsPort *p) {
    ds_init(s);
    assert(ds_begin(s,p,CID,"01.08",sizeof payload-1u,digest)==DS_OK);
}
static void read_exact(int fd,const char *leaf,const uint8_t *data,
                       size_t size) {
    int in=openat(fd,leaf,O_RDONLY|O_NOFOLLOW);
    assert(in>=0);
    uint8_t got[120]={0};
    assert(size<sizeof got);
    assert(read(in,got,sizeof got)==(ssize_t)size);
    assert(memcmp(got,data,size)==0);
    assert(close(in)==0);
}
int main(void) {
    make_digest();
    DsFileBackend b;
    assert(ds_file_backend_attach(&b,-1)==-1);
    DsPort unavailable=ds_file_backend_port(&b);
    assert(!unavailable.create_exclusive);

    char directory[]="/tmp/ps4-backend-XXXXXX";
    assert(mkdtemp(directory));
    int directory_fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    assert(directory_fd>=0);
    assert(ds_file_backend_attach(&b,directory_fd)==0);
    /* Adapter owns its DUP; closing caller fd cannot break it. */
    assert(close(directory_fd)==0);
    DsPort p=ds_file_backend_port(&b);
    assert(p.create_exclusive && p.verify_sha256 && p.publish_exclusive);
    DsSession s;
    begin(&s,&p);
    assert(ds_write(&s,payload,12)==DS_OK);
    assert(ds_write(&s,payload+12,sizeof payload-1u-12u)==DS_OK);
    assert(ds_finalize(&s)==DS_OK);
    assert(s.state==DS_PUBLISHED);
    read_exact(b.directory_fd,s.final_leaf,payload,sizeof payload-1u);
    assert(faccessat(b.directory_fd,s.part_leaf,F_OK,AT_SYMLINK_NOFOLLOW)!=0);

    /* Safe publication refuses replacing a previous final file. */
    begin(&s,&p);
    assert(ds_write(&s,payload,sizeof payload-1u)==DS_OK);
    assert(ds_finalize(&s)==DS_PUBLISH_FAILED);
    assert(ds_abort(&s)==DS_OK);
    read_exact(b.directory_fd,s.final_leaf,payload,sizeof payload-1u);
    assert(unlinkat(b.directory_fd,s.final_leaf,0)==0);

    /* Pre-existing .part is never adopted or deleted. */
    const char *part="IV0000-CUSA02343_00-SCANNER000000000-01.08.bin.part";
    int old=openat(b.directory_fd,part,O_WRONLY|O_CREAT|O_EXCL,0600);
    assert(old>=0);
    assert(write(old,"keep",4)==4);
    assert(close(old)==0);
    ds_init(&s);
    assert(ds_begin(&s,&p,CID,"01.08",sizeof payload-1u,digest)
           ==DS_OPEN_FAILED);
    read_exact(b.directory_fd,part,(const uint8_t *)"keep",4);
    assert(unlinkat(b.directory_fd,part,0)==0);

    /* O_EXCL|O_NOFOLLOW refuses a symlink .part as well. */
    old=openat(b.directory_fd,"innocent.bin",O_WRONLY|O_CREAT|O_EXCL,0600);
    assert(old>=0);
    assert(write(old,"SAFE",4)==4);
    assert(close(old)==0);
    assert(symlinkat("innocent.bin",b.directory_fd,part)==0);
    ds_init(&s);
    assert(ds_begin(&s,&p,CID,"01.08",sizeof payload-1u,digest)
           ==DS_OPEN_FAILED);
    read_exact(b.directory_fd,"innocent.bin",(const uint8_t *)"SAFE",4);
    assert(unlinkat(b.directory_fd,part,0)==0);
    assert(unlinkat(b.directory_fd,"innocent.bin",0)==0);

    /* A forged link replacing our owned .part is not verified/deleted. */
    begin(&s,&p);
    assert(ds_write(&s,payload,sizeof payload-1u)==DS_OK);
    assert(renameat(b.directory_fd,s.part_leaf,b.directory_fd,
                    "orphan-owned.bin")==0);
    old=openat(b.directory_fd,s.part_leaf,
               O_WRONLY|O_CREAT|O_EXCL,0600);
    assert(old>=0);
    assert(write(old,"ATTACKER",8)==8);
    assert(close(old)==0);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(ds_abort(&s)==DS_CLEANUP_FAILED);
    read_exact(b.directory_fd,s.part_leaf,(const uint8_t *)"ATTACKER",8);
    assert(unlinkat(b.directory_fd,s.part_leaf,0)==0);
    assert(unlinkat(b.directory_fd,"orphan-owned.bin",0)==0);

    /* Reattach a clean backend to reset its ownership state. */
    ds_file_backend_detach(&b);
    directory_fd=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    assert(directory_fd>=0);
    assert(ds_file_backend_attach(&b,directory_fd)==0);
    assert(close(directory_fd)==0);
    p=ds_file_backend_port(&b);
    begin(&s,&p);
    assert(ds_write(&s,payload,sizeof payload-1u)==DS_OK);
    /* Content changed after ds_write must fail stored-file SHA-256. */
    old=openat(b.directory_fd,s.part_leaf,O_WRONLY|O_NOFOLLOW);
    assert(old>=0);
    assert(pwrite(old,"X",1,0)==1);
    assert(close(old)==0);
    assert(ds_finalize(&s)==DS_VERIFY_FAILED);
    assert(ds_abort(&s)==DS_OK);
    assert(faccessat(b.directory_fd,s.part_leaf,F_OK,AT_SYMLINK_NOFOLLOW)!=0);

    ds_file_backend_detach(&b);
    assert(rmdir(directory)==0);
    puts("DOWNLOAD_FILE_BACKEND_HOST_OK");
    return 0;
}
