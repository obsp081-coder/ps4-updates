/*
 * PS4 updater: metadata scanner prototype (read-only)
 * This is portable C source, NOT an installable PS4 PKG.
 * Reads PARAM.SFO files located under /system_data/priv/appmeta,
 * including the external subdirectory. Metadata may be stale and
 * this is NOT a replacement for the authoritative app.db inventory.
 *
 * Build on Linux for verification: cc -std=c99 -Wall -Wextra -O2 -o scan_appmeta scanner/scan_appmeta.c
 * Usage: ./scan_appmeta --appmeta ./fixtures/appmeta --out inventory.json
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_SFO (4U * 1024U * 1024U)
#define MAX_PATH_ 2048

typedef struct {
    char title_id[32], title[256], app_ver[32], content_id[128], category[32];
} game_t;

static unsigned short rd16(const unsigned char *p) {return (unsigned short)(p[0] | (p[1]<<8));}
static unsigned int rd32(const unsigned char *p) {return (unsigned int)p[0] | ((unsigned int)p[1]<<8) | ((unsigned int)p[2]<<16) | ((unsigned int)p[3]<<24);}
static int within(size_t pos, size_t n, size_t size) {return pos<=size && n<=size-pos;}
static void copy_sfo_str(char *dst, size_t dstcap, const unsigned char *p, size_t n) {
    if (dstcap == 0) return;
    size_t used=0;
    while(used<n && used<dstcap-1 && p[used]) {dst[used]=(char)p[used]; used++;}
    dst[used]=0;
}
static int parse_sfo(const char *path, game_t *g) {
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    if(fseek(f,0,SEEK_END)!=0) {fclose(f);return 0;}
    long flen=ftell(f);
    if(flen<20 || flen>(long)MAX_SFO) {fclose(f);return 0;}
    rewind(f);
    size_t size=(size_t)flen;
    unsigned char *buf=(unsigned char*)malloc(size);
    if(!buf) {fclose(f);return 0;}
    int ok=(fread(buf,1,size,f)==size); fclose(f);
    if(!ok || rd32(buf)!=0x46535000) {free(buf);return 0;}
    unsigned int keys=rd32(buf+8), values=rd32(buf+12), count=rd32(buf+16);
    if(count>2048 || !within(20,(size_t)count*16,size) || keys>=size || values>=size) {free(buf);return 0;}
    memset(g,0,sizeof(*g));
    for(unsigned int i=0;i<count;i++) {
        const unsigned char *e=buf+20+i*16;
        size_t keypos=(size_t)keys+rd16(e);
        size_t valpos=(size_t)values+rd32(e+12);
        size_t len=rd32(e+4);
        if(!within(keypos,1,size) || !within(valpos,len,size)) continue;
        const char *name=(const char*)buf+keypos;
        const char *term=(const char*)memchr(name,0,size-keypos);
        if(!term) continue;
        if(!strcmp(name,"TITLE_ID")) copy_sfo_str(g->title_id,sizeof(g->title_id),buf+valpos,len);
        else if(!strcmp(name,"TITLE")) copy_sfo_str(g->title,sizeof(g->title),buf+valpos,len);
        else if(!strcmp(name,"APP_VER")) copy_sfo_str(g->app_ver,sizeof(g->app_ver),buf+valpos,len);
        else if(!strcmp(name,"CONTENT_ID")) copy_sfo_str(g->content_id,sizeof(g->content_id),buf+valpos,len);
        else if(!strcmp(name,"CATEGORY")) copy_sfo_str(g->category,sizeof(g->category),buf+valpos,len);
    }
    free(buf); return 1;
}
static int valid_cusa(const char *s) {
    if(strlen(s)!=9 || strncmp(s,"CUSA",4)) return 0;
    for(int i=4;i<9;i++) if(!isdigit((unsigned char)s[i])) return 0;
    return 1;
}
static void json_esc(FILE *out,const char *s) {
    fputc('"',out);
    for(const unsigned char *p=(const unsigned char*)s;*p;p++) {
        if(*p=='"' || *p=='\\') {fputc('\\',out);fputc(*p,out);}
        else if(*p<32) fprintf(out,"\\u%04x",*p);
        else fputc(*p,out);
    }
    fputc('"',out);
}
static int count=0;
static char seen[10000][16];
static int is_duplicate(const char *id) {
    for(int i=0;i<count;i++) if(strcmp(seen[i],id)==0) return 1;
    return 0;
}
static void scan_folder(FILE *out,const char *root,const char *location) {
    char path[MAX_PATH_];
    DIR *d=opendir(root); if(!d) return;
    struct dirent *entry;
    while((entry=readdir(d))) {
        const char *id=entry->d_name;
        if(!valid_cusa(id) || count>=10000 || is_duplicate(id)) continue;
        int n=snprintf(path,sizeof(path),"%s/%s/param.sfo",root,id);
        if(n<0 || (size_t)n>=sizeof(path)) continue;
        game_t g;
        if(!parse_sfo(path,&g)) continue;
        if(!valid_cusa(g.title_id) || strcmp(id,g.title_id)!=0) continue;
        if(g.category[0] && strcmp(g.category,"gd") && strcmp(g.category,"gde")) continue;
        if(count) fputs(",\n",out);
        fprintf(out,"    {\"cusa\":");json_esc(out,g.title_id);
        fputs(",\"name\":",out);json_esc(out,g.title);
        fputs(",\"app_ver\":",out);json_esc(out,g.app_ver);
        fputs(",\"content_id\":",out);json_esc(out,g.content_id);
        fputs(",\"category\":",out);json_esc(out,g.category);
        fputs(",\"metadata_location\":",out);json_esc(out,location);
        fputs(",\"installed_confirmed\":false}",out);
        snprintf(seen[count],sizeof(seen[count]),"%s",id);count++;
    }
    closedir(d);
}
int main(int argc,char **argv) {
    const char *root="/system_data/priv/appmeta", *outfile="inventory.json";
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--appmeta") && i+1<argc) root=argv[++i];
        else if(!strcmp(argv[i],"--out") && i+1<argc) outfile=argv[++i];
        else {fprintf(stderr,"Usage: %s [--appmeta PATH] [--out FILE]\n",argv[0]);return 2;}
    }
    FILE *out=fopen(outfile,"wb");
    if(!out) {fprintf(stderr,"Cannot write %s: %s\n",outfile,strerror(errno));return 1;}
    fputs("{\n  \"schema\": 1,\n  \"source\": \"ps4-appmeta-prototype\",\n  \"metadata_only\": true,\n  \"games\": [\n",out);
    scan_folder(out,root,"internal");
    char external[MAX_PATH_];
    int n=snprintf(external,sizeof(external),"%s/external",root);
    if(n>0 && (size_t)n<sizeof(external)) scan_folder(out,external,"external");
    fputs("\n  ]\n}\n",out); fclose(out);
    printf("Found %d game metadata records. This is not a confirmed installed-app inventory.\n",count);
    return 0;
}
