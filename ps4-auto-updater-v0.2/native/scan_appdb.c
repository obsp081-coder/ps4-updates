/*
 * PS4 Auto Updater — native inventory scanner prototype, C99 + SQLite3.
 * READ-ONLY: SELECT queries against app.db; reads param.sfo; writes JSON to an output file only.
 * Built/tested on Linux. NOT yet built or verified on a PS4, nor is this a complete homebrew app.
 * Based on observed PS4 app.db tbl_appbrowse_* schema (ItemzFlow).
 */
#define _POSIX_C_SOURCE 200809L
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define MAX_SFO (4U * 1024U * 1024U)
#define MAX_TITLES 10000
#define MAX_PATH_ 2048

typedef struct {
    char id[32], name[256], app_ver[32], content_id[128], category[32];
    int metadata_found, in_appdb, found_external;
} game_t;
static game_t games[MAX_TITLES];
static size_t count=0;
static uint16_t rd16(const unsigned char *p) {return (uint16_t)(p[0] | ((uint16_t)p[1]<<8));}
static uint32_t rd32(const unsigned char *p) {return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);}
static int inside(size_t pos,size_t len,size_t size){return pos<=size && len<=size-pos;}
static void bounded_copy(char *dst,size_t cap,const char *src) {
    if(!cap)return;
    if(!src)src="";
    size_t len=strlen(src); if(len>=cap)len=cap-1;
    memcpy(dst,src,len);dst[len]=0;
}
static int valid_id(const char *p){
    if(!p || strlen(p)!=9 || strncmp(p,"CUSA",4))return 0;
    for(size_t i=4;i<9;i++)if(!isdigit((unsigned char)p[i]))return 0;
    return 1;
}
static void extract_str(char *dst,size_t cap,const unsigned char *p,size_t len){
    size_t n=0;if(!cap)return;
    while(n<len && n<cap-1 && p[n]){dst[n]=(char)p[n];n++;}
    dst[n]=0;
}
static int parse_sfo(const char *path,game_t *g){
    FILE *fp=fopen(path,"rb"); if(!fp)return 0;
    if(fseek(fp,0,SEEK_END)){fclose(fp);return 0;}
    long l=ftell(fp);if(l<20||l>(long)MAX_SFO){fclose(fp);return 0;}
    rewind(fp);size_t n=(size_t)l;
    unsigned char *buf=(unsigned char*)malloc(n); if(!buf){fclose(fp);return 0;}
    int ok=(fread(buf,1,n,fp)==n);fclose(fp);
    if(!ok||rd32(buf)!=0x46535000){free(buf);return 0;}
    size_t keys=rd32(buf+8),values=rd32(buf+12),entries=rd32(buf+16);
    if(entries>2048||!inside(20,entries*16,n)||keys>=n||values>=n){free(buf);return 0;}
    char sfo_title_id[32]="";
    for(size_t i=0;i<entries;i++){
        const unsigned char *e=buf+20+i*16;
        size_t kp=keys+rd16(e),vp=values+rd32(e+12),sz=rd32(e+4);
        if(!inside(kp,1,n)||!inside(vp,sz,n))continue;
        const char *key=(const char*)buf+kp;
        if(!memchr(key,0,n-kp))continue;
        if(!strcmp(key,"TITLE_ID"))extract_str(sfo_title_id,sizeof(sfo_title_id),buf+vp,sz);
        else if(!strcmp(key,"TITLE"))extract_str(g->name,sizeof(g->name),buf+vp,sz);
        else if(!strcmp(key,"APP_VER"))extract_str(g->app_ver,sizeof(g->app_ver),buf+vp,sz);
        else if(!strcmp(key,"CONTENT_ID"))extract_str(g->content_id,sizeof(g->content_id),buf+vp,sz);
        else if(!strcmp(key,"CATEGORY"))extract_str(g->category,sizeof(g->category),buf+vp,sz);
    }
    free(buf);
    /* Refuse metadata if SFO identifies a different installed title. */
    return !sfo_title_id[0] || !strcmp(sfo_title_id,g->id);
}
static game_t *get_game(const char *id){
    for(size_t i=0;i<count;i++)if(!strcmp(games[i].id,id))return &games[i];
    if(count>=MAX_TITLES)return NULL;
    game_t *g=&games[count++];memset(g,0,sizeof(*g));bounded_copy(g->id,sizeof(g->id),id);return g;
}
static int safe_table(const char *s){
    const char *pfx="tbl_appbrowse_";size_t n=strlen(pfx);
    if(strncmp(s,pfx,n)||!s[n])return 0;
    for(const char *p=s+n;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_')return 0;
    return 1;
}
static int table_col(sqlite3 *db,const char *table,const char *col){
    char sql[256];if(snprintf(sql,sizeof(sql),"PRAGMA table_info(\"%s\")",table)>=(int)sizeof(sql))return 0;
    sqlite3_stmt *st=NULL;int found=0;
    if(sqlite3_prepare_v2(db,sql,-1,&st,NULL)!=SQLITE_OK)return 0;
    while(sqlite3_step(st)==SQLITE_ROW){
        const unsigned char *n=sqlite3_column_text(st,1);
        if(n&&!strcmp((const char*)n,col)){found=1;break;}
    }
    sqlite3_finalize(st);return found;
}
static size_t load_table(sqlite3 *db,const char *table){
    if(!safe_table(table)||!table_col(db,table,"titleId"))return 0;
    int has_name=table_col(db,table,"titleName");
    int has_category=table_col(db,table,"category");
    char *sql=sqlite3_mprintf("SELECT titleId, %s, %s FROM \"%w\"",
             has_name?"titleName":"NULL",has_category?"category":"NULL",table);
    if(!sql)return 0;
    sqlite3_stmt *st=NULL;size_t added=0;
    int prepared=sqlite3_prepare_v2(db,sql,-1,&st,NULL);
    sqlite3_free(sql);
    if(prepared!=SQLITE_OK)return 0;
    while(sqlite3_step(st)==SQLITE_ROW){
        const char *id=(const char*)sqlite3_column_text(st,0);
        const char *name=(const char*)sqlite3_column_text(st,1);
        const char *category=(const char*)sqlite3_column_text(st,2);
        if(!valid_id(id))continue;
        /* Avoid treating additional content entries as full games. */
        if(category && *category && strcmp(category,"gd") && strcmp(category,"gde"))continue;
        game_t *g=get_game(id);if(!g)break;
        if(!g->in_appdb)added++;
        g->in_appdb=1;
        if(name&&!g->name[0])bounded_copy(g->name,sizeof(g->name),name);
        if(category&&!g->category[0])bounded_copy(g->category,sizeof(g->category),category);
    }
    sqlite3_finalize(st);return added;
}
static int load_db(const char *dbpath){
    sqlite3 *db=NULL;int rc=sqlite3_open_v2(dbpath,&db,SQLITE_OPEN_READONLY,NULL);
    if(rc!=SQLITE_OK){fprintf(stderr,"Cannot open app.db read-only: %s\n",db?sqlite3_errmsg(db):"unknown");if(db)sqlite3_close(db);return -1;}
    (void)sqlite3_exec(db,"PRAGMA query_only=ON",NULL,NULL,NULL);
    sqlite3_stmt *st=NULL;int tables=0;
    if(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'tbl_appbrowse_%'",-1,&st,NULL)!=SQLITE_OK){
        sqlite3_close(db);return -1;
    }
    /* SQL prepared statements cannot safely interleave schema inspections on every platform.
       Collect table names first. */
    char names[128][128];size_t nt=0;
    while(sqlite3_step(st)==SQLITE_ROW && nt<128){
        const char *name=(const char*)sqlite3_column_text(st,0);
        if(name&&safe_table(name))bounded_copy(names[nt++],sizeof(names[0]),name);
    }
    sqlite3_finalize(st);
    for(size_t i=0;i<nt;i++)if(load_table(db,names[i]))tables++;
    sqlite3_close(db);
    if(!nt){fprintf(stderr,"No tbl_appbrowse_* tables in app.db; check database schema.\n");return -1;}
    fprintf(stderr,"Read %zu database tables; %zu distinct CUSA IDs.\n",nt,count);
    return tables>=0?0:-1;
}
static void attach_sfo(game_t *g,const char *root){
    char p[MAX_PATH_];int n=snprintf(p,sizeof(p),"%s/%s/param.sfo",root,g->id);
    if(n>0&&(size_t)n<sizeof(p)&&parse_sfo(p,g)){g->metadata_found=1;return;}
    n=snprintf(p,sizeof(p),"%s/external/%s/param.sfo",root,g->id);
    if(n>0&&(size_t)n<sizeof(p)&&parse_sfo(p,g)){g->metadata_found=1;g->found_external=1;}
}
static void json_str(FILE *o,const char *s){
    fputc('"',o);
    for(const unsigned char *p=(const unsigned char*)s;*p;p++){
        if(*p=='"'||*p=='\\'){fputc('\\',o);fputc(*p,o);}
        else if(*p<0x20)fprintf(o,"\\u%04x",*p);
        else fputc(*p,o);
    }
    fputc('"',o);
}
static int cmp_game(const void*a,const void*b){return strcmp(((const game_t*)a)->id,((const game_t*)b)->id);}
static int write_inventory(const char *out){
    FILE *fp=fopen(out,"wb");if(!fp){fprintf(stderr,"Cannot write %s: %s\n",out,strerror(errno));return -1;}
    qsort(games,count,sizeof(game_t),cmp_game);
    fputs("{\n  \"schema\": 2,\n  \"source\": \"ps4-appdb-readonly-scanner-prototype\",\n  \"metadata_only\": false,\n  \"games\": [\n",fp);
    for(size_t i=0;i<count;i++){
        game_t *g=&games[i];
        if(i)fputs(",\n",fp);
        fputs("    {\"cusa\":",fp);json_str(fp,g->id);
        fputs(",\"name\":",fp);json_str(fp,g->name);
        fputs(",\"app_ver\":",fp);json_str(fp,g->app_ver);
        fputs(",\"content_id\":",fp);json_str(fp,g->content_id);
        fputs(",\"category\":",fp);json_str(fp,g->category);
        fputs(",\"listed_in_appdb\":true,\"metadata_available\":",fp);
        fputs(g->metadata_found?"true":"false",fp);
        fputs(",\"installed_confirmed\":",fp);
        /* app.db membership + local on-disk metadata gives confidence but not a guarantee. */
        fputs(g->metadata_found?"true":"false",fp);
        fputs(",\"metadata_location\":",fp);json_str(fp,g->found_external?"external":"internal");
        fputs("}",fp);
    }
    fputs("\n  ]\n}\n",fp);
    int err=ferror(fp);if(fclose(fp))err=1;
    return err?-1:0;
}
int main(int argc,char **argv){
    const char *db="/system_data/priv/mms/app.db";
    const char *meta="/system_data/priv/appmeta";
    const char *out="/user/data/PS4AutoUpdater/inventory.json";
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--appdb") && i+1<argc)db=argv[++i];
        else if(!strcmp(argv[i],"--appmeta") && i+1<argc)meta=argv[++i];
        else if(!strcmp(argv[i],"--out") && i+1<argc)out=argv[++i];
        else {fprintf(stderr,"Usage: %s [--appdb PATH] [--appmeta PATH] [--out PATH]\n",argv[0]);return 2;}
    }
    if(load_db(db))return 1;
    for(size_t i=0;i<count;i++)attach_sfo(&games[i],meta);
    if(write_inventory(out)){fprintf(stderr,"Failed to write inventory.\n");return 1;}
    printf("Exported %zu CUSA records to %s (read-only inventory; no install actions).\n",count,out);
    return 0;
}
