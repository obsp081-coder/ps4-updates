/* Host POSIX implementation of DsPort; on PS4 this module is disabled.
 * Never replace linkat with a check-then-rename fallback.
 */
#ifndef __ORBIS__
#define _GNU_SOURCE 1
#endif
#include "download_file_backend.h"
#include "download_sha256.h"
#include <stddef.h>
#include <string.h>

#ifdef __ORBIS__
/* PS4 platform requires proof of atomic no-overwrite publication, owned
 * cleanup, and durable flush before a real backend can be enabled. */
int ds_file_backend_attach(DsFileBackend *b,int fd) {
    (void)fd;
    if(b) {
        memset(b,0,sizeof *b);
        b->directory_fd=-1;
        b->writer_fd=-1;
        b->reader_fd=-1;
    }
    return -1;
}
void ds_file_backend_detach(DsFileBackend *b) {
    if(!b)return;
    memset(b,0,sizeof *b);
    b->directory_fd=-1;
    b->writer_fd=-1;
    b->reader_fd=-1;
}
DsPort ds_file_backend_port(DsFileBackend *b) {
    DsPort p={0};
    (void)b;
    return p;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static size_t bounded_len(const char *s,size_t max) {
    size_t n=0;
    if(s)while(n<max && s[n])++n;
    return n;
}
static int safe_leaf(const char *s,int require_part) {
    const size_t n=bounded_len(s,DS_NAME_CAP);
    if(!n || n>=DS_NAME_CAP || s[0]=='.' || strstr(s,".."))
        return 0;
    for(size_t i=0;i<n;++i) {
        const unsigned char c=(unsigned char)s[i];
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||
             (c>='a'&&c<='z')||c=='-'||c=='_'||c=='.'))
            return 0;
    }
    const char *suffix=require_part?".bin.part":".bin";
    const size_t k=strlen(suffix);
    return n>k && !strcmp(s+n-k,suffix);
}
static void backend_reset(DsFileBackend *b) {
    memset(b,0,sizeof *b);
    b->directory_fd=-1;
    b->writer_fd=-1;
    b->reader_fd=-1;
}
static int same_owned(DsFileBackend *b,const char *name) {
    if(!b || !b->owns_partial || strcmp(b->owned_part,name))
        return 0;
    struct stat st;
    if(fstatat(b->directory_fd,name,&st,AT_SYMLINK_NOFOLLOW)!=0 ||
       !S_ISREG(st.st_mode))return 0;
    return (uint64_t)st.st_dev==b->owned_device &&
           (uint64_t)st.st_ino==b->owned_inode;
}
int ds_file_backend_attach(DsFileBackend *b,int dirfd) {
    if(!b)return -1;
    backend_reset(b);
    if(dirfd<0)return -1;
    struct stat st;
    if(fstat(dirfd,&st)!=0 || !S_ISDIR(st.st_mode) ||
       st.st_uid!=geteuid() || (st.st_mode & (S_IWGRP|S_IWOTH))!=0)
        return -1;
    int d=dup(dirfd);
    if(d<0)return -1;
    b->directory_fd=d;
    return 0;
}
static int fs_create(void *ctx,const char *part) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->directory_fd<0 || b->writer_fd>=0 ||
       b->owns_partial || !safe_leaf(part,1))return -1;
    int fd=openat(b->directory_fd,part,
                  O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return -1;
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_nlink!=1 ||
       st.st_uid!=geteuid()) {
        (void)close(fd);
        /* File was created exclusively, but if fstat failed we cannot
         * safely prove ownership for deletion. Leave an orphan, fail
         * closed, and require manual inspection before reuse. */
        return -1;
    }
    b->owned_device=(uint64_t)st.st_dev;
    b->owned_inode=(uint64_t)st.st_ino;
    memcpy(b->owned_part,part,strlen(part)+1u);
    b->owns_partial=1u;
    b->writer_fd=fd;
    return 0;
}
static int fs_write(void *ctx,const uint8_t *data,size_t size) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->writer_fd<0 || !b->owns_partial)return -1;
    size_t done=0;
    while(done<size) {
        const ssize_t n=write(b->writer_fd,data+done,size-done);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return -1;
        done+=(size_t)n;
    }
    return 0;
}
static int fs_sync(void *ctx) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    return b && b->writer_fd>=0 && fsync(b->writer_fd)==0?0:-1;
}
static int fs_close(void *ctx) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->writer_fd<0)return -1;
    int fd=b->writer_fd;
    b->writer_fd=-1;
    return close(fd);
}
static int read_open(void *ctx,const char *name) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->reader_fd>=0 || !same_owned(b,name))return -1;
    int fd=openat(b->directory_fd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return -1;
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) ||
       (uint64_t)st.st_dev!=b->owned_device ||
       (uint64_t)st.st_ino!=b->owned_inode || st.st_nlink!=1) {
        (void)close(fd);
        return -1;
    }
    b->reader_fd=fd;
    return 0;
}
static int read_next(void *ctx,uint8_t *data,size_t cap,size_t *got) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->reader_fd<0 || !data || !got)return -1;
    ssize_t n;
    do {n=read(b->reader_fd,data,cap);} while(n<0 && errno==EINTR);
    if(n<0)return -1;
    *got=(size_t)n;
    return 0;
}
static int read_close(void *ctx) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || b->reader_fd<0)return -1;
    int fd=b->reader_fd;
    b->reader_fd=-1;
    return close(fd);
}
static int fs_verify(void *ctx,const char *part,uint64_t size,
                     const uint8_t expected[32]) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || !same_owned(b,part))return 0;
    DsSha256Reader r={0};
    r.context=b;
    r.open_read=read_open;
    r.read_next=read_next;
    r.close_read=read_close;
    return ds_sha256_verify_stored(&r,part,size,expected);
}
static int fs_publish(void *ctx,const char *part,const char *final) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || !same_owned(b,part) || !safe_leaf(final,0) ||
       b->writer_fd>=0 || b->reader_fd>=0)return -1;
    if(linkat(b->directory_fd,part,b->directory_fd,final,0)!=0)
        return -1; /* Fails with EEXIST, never overwrites final. */
    struct stat st;
    if(fstatat(b->directory_fd,final,&st,AT_SYMLINK_NOFOLLOW)!=0 ||
       !S_ISREG(st.st_mode) ||
       (uint64_t)st.st_dev!=b->owned_device ||
       (uint64_t)st.st_ino!=b->owned_inode)
        return -1;
    /* Sync directory entry. Never claim success if durability fails. */
    return fsync(b->directory_fd)==0 ? 0 : -1;
}
static int fs_unlink(void *ctx,const char *part) {
    DsFileBackend *b=(DsFileBackend *)ctx;
    if(!b || !same_owned(b,part) || !safe_leaf(part,1) ||
       b->writer_fd>=0 || b->reader_fd>=0)return -1;
    if(unlinkat(b->directory_fd,part,0)!=0)return -1;
    b->owns_partial=0u;
    b->owned_part[0]=0;
    return fsync(b->directory_fd)==0?0:-1;
}
DsPort ds_file_backend_port(DsFileBackend *b) {
    DsPort p={0};
    if(!b || b->directory_fd<0)return p;
    p.context=b;
    p.create_exclusive=fs_create;
    p.write_all=fs_write;
    p.flush=fs_sync;
    p.close=fs_close;
    p.verify_sha256=fs_verify;
    p.publish_exclusive=fs_publish;
    p.unlink_part=fs_unlink;
    return p;
}
void ds_file_backend_detach(DsFileBackend *b) {
    if(!b)return;
    if(b->reader_fd>=0)(void)close(b->reader_fd);
    if(b->writer_fd>=0)(void)close(b->writer_fd);
    if(b->directory_fd>=0)(void)close(b->directory_fd);
    backend_reset(b);
}
#endif
