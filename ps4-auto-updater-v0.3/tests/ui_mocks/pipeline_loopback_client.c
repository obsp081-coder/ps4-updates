/* HOST ONLY. A real POSIX TCP loopback client feeds network bytes into
 * the same C Download Pipeline linked into the experimental PS4 build.
 * No DNS, no TLS and no Sony update servers. The test's tls_authenticated
 * flag is a SYNTHETIC fixture assertion, never a production TLS claim.
 */
#define _GNU_SOURCE 1
#include "../../ps4/src/download_pipeline.h"
#include "../../ps4/src/download_file_backend.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const char *CUSA="CUSA02343";
static const char *CID="IV0000-CUSA02343_00-SCANNER000000000";

static int from_hex(const char *hex,uint8_t out[32]) {
    if(!hex || strlen(hex)!=64u)return 0;
    for(unsigned i=0u;i<32u;++i) {
        unsigned parts[2];
        for(unsigned j=0;j<2u;++j) {
            const char c=hex[i*2u+j];
            if(c>='0' && c<='9')parts[j]=(unsigned)(c-'0');
            else if(c>='a' && c<='f')parts[j]=(unsigned)(c-'a'+10);
            else return 0;
        }
        out[i]=(uint8_t)(parts[0]*16u+parts[1]);
    }
    return 1;
}
static int parse_number(const char *s,uint64_t *out) {
    char *end=NULL;
    errno=0;
    if(!s || !*s || *s=='-')return 0;
    const unsigned long long result=strtoull(s,&end,10);
    if(errno || !end || *end)return 0;
    *out=(uint64_t)result;
    return 1;
}
static int send_all(int fd,const char *bytes,size_t length) {
    size_t done=0;
    while(done<length) {
        const ssize_t n=send(fd,bytes+done,length-done,0);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        done+=(size_t)n;
    }
    return 1;
}
static ssize_t terminator(const char *s,size_t n) {
    if(n<4u)return -1;
    for(size_t i=3u;i<n;++i)
        if(s[i-3u]=='\r' && s[i-2u]=='\n' &&
           s[i-1u]=='\r' && s[i]=='\n')return (ssize_t)(i+1u);
    return -1;
}
int main(int argc,char **argv) {
    if(argc!=8) {
        fprintf(stderr,"usage: client <port> <dir> <endpoint> <sha256> <bytes> <cancel|run> <tls-test|untrusted>\n");
        return 2;
    }
    uint64_t port=0,expected=0;
    if(!parse_number(argv[1],&port) || port<1u || port>65535u ||
       !parse_number(argv[5],&expected) || !expected ||
       expected>8u*1024u*1024u || argv[3][0]!='/' ||
       strchr(argv[3],'\r') || strchr(argv[3],'\n'))return 2;
    const int cancel=!strcmp(argv[6],"cancel");
    const int trust=!strcmp(argv[7],"tls-test");
    if((!cancel && strcmp(argv[6],"run")) ||
       (!trust && strcmp(argv[7],"untrusted")))return 2;
    uint8_t digest[32];
    if(!from_hex(argv[4],digest))return 2;

    DlqQueue queue;
    dlq_init(&queue);
    if(dlq_enqueue(&queue,CUSA,CID,"01.09",expected)!=0)return 3;
    DsFileBackend backend;
    int dir=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(dir<0)return 3;
    if(ds_file_backend_attach(&backend,dir)!=0) {
        (void)close(dir);
        return 3;
    }
    (void)close(dir);
    DsPort storage=ds_file_backend_port(&backend);
    DlPipeline pipe;
    if(dlp_init(&pipe,&queue,0,&storage,digest)!=DLP_OK) {
        ds_file_backend_detach(&backend);return 3;
    }
    struct statvfs space;
    if(fstatvfs(backend.directory_fd,&space)!=0) {
        ds_file_backend_detach(&backend);return 3;
    }
    const uint64_t available=
        (uint64_t)space.f_bavail>UINT64_MAX/(uint64_t)space.f_frsize ?
        UINT64_MAX : (uint64_t)space.f_bavail*(uint64_t)space.f_frsize;
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if(sock<0) {
        ds_file_backend_detach(&backend);return 3;
    }
    struct sockaddr_in addr;
    memset(&addr,0,sizeof addr);
    addr.sin_family=AF_INET;
    addr.sin_port=htons((uint16_t)port);
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(sock,(const struct sockaddr *)&addr,sizeof addr)!=0) {
        (void)close(sock);ds_file_backend_detach(&backend);return 3;
    }
    char request[256];
    const int n=snprintf(request,sizeof request,
            "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
            argv[3]);
    if(n<=0 || (size_t)n>=sizeof request ||
       !send_all(sock,request,(size_t)n)) {
        (void)close(sock);ds_file_backend_detach(&backend);return 3;
    }
    char header[DL_HTTP_HEADER_MAX_BYTES+1u];
    size_t buffered=0;
    int headers_done=0,failed=0,cancelled=0;
    char chunk[4096];
    for(;;) {
        const ssize_t got=recv(sock,chunk,sizeof chunk,0);
        if(got<0 && errno==EINTR)continue;
        if(got<0) {failed=1;break;}
        if(got==0)break;
        const uint8_t *body=(const uint8_t *)chunk;
        size_t body_count=(size_t)got;
        if(!headers_done) {
            if(buffered+body_count>DL_HTTP_HEADER_MAX_BYTES) {
                failed=1;break;
            }
            memcpy(header+buffered,chunk,body_count);
            buffered+=body_count;
            const ssize_t boundary=terminator(header,buffered);
            if(boundary<0)continue;
            headers_done=1;
            if(dlp_begin(&pipe,header,(size_t)boundary,trust,available,
                         0u,4096u,NULL)!=DLP_OK) {
                failed=1;break;
            }
            body=(const uint8_t *)header+(size_t)boundary;
            body_count=buffered-(size_t)boundary;
        }
        if(body_count) {
            if(dlp_feed(&pipe,body,body_count)!=DLP_OK) {
                failed=1;break;
            }
            if(cancel) {
                if(dlp_cancel(&pipe)!=DLP_CANCELLED)failed=1;
                cancelled=1;
                break;
            }
        }
    }
    (void)close(sock);
    if(!failed && !cancelled) {
        if(!headers_done || dlp_finish(&pipe)!=DLP_OK)failed=1;
    }
    if(failed && pipe.started)(void)dlp_cancel(&pipe);
    const DlqJob *job=dlq_get(&queue,0);
    const int accepted=!failed && !cancelled && job &&
                       job->state==DLQ_COMPLETED &&
                       job->received_bytes==expected;
    const uint64_t progress=job?job->received_bytes:0u;
    ds_file_backend_detach(&backend);
    if(accepted) {
        printf("PIPELINE_LOOPBACK_OK bytes=%" PRIu64 "\n",progress);
        return 0;
    }
    if(cancelled) {
        printf("PIPELINE_LOOPBACK_CANCELLED bytes=%" PRIu64 "\n",progress);
        return 4;
    }
    printf("PIPELINE_LOOPBACK_REJECTED bytes=%" PRIu64 "\n",progress);
    return 5;
}
