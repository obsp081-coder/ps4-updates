/* Host-only cache contract. The data are metadata, never install proof. */
#include "../../ps4/src/ui_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void setup(UiSnapshotGame *g,const char *id,const char *title,
                  const char *version,const char *category) {
    memset(g,0,sizeof *g);
    strcpy(g->cusa,id);
    strcpy(g->title,title);
    strcpy(g->version,version);
    strcpy(g->category,category);
}
static void damage(const char *path,long offset) {
    FILE *f=fopen(path,"r+b");
    assert(f!=NULL);
    assert(fseek(f,offset,SEEK_SET)==0);
    int before=fgetc(f);
    assert(before!=EOF);
    assert(fseek(f,offset,SEEK_SET)==0);
    assert(fputc(before^0x4A,f)!=EOF);
    assert(fclose(f)==0);
}
int main(int argc,char **argv) {
    assert(argc==2);
    const char *path=argv[1];
    UiSnapshotGame games[2],loaded[2];
    unsigned count=777;
    setup(&games[0],"CUSA02343","Game Arabic \xd9\x84\xd8\xb9\xd8\xa8\xd8\xa9","01.05","gd");
    setup(&games[1],"CUSA00002","Another game","01.02","gdo");
    memset(loaded,0x41,sizeof loaded);
    assert(ui_snapshot_load(path,loaded,2,&count)==0);
    assert(count==0);
    assert(ui_snapshot_save(path,games,2)==0);
    assert(ui_snapshot_load(path,loaded,2,&count)==1);
    assert(count==2);
    assert(memcmp(loaded,games,sizeof loaded)==0);

    /* One-byte corruption must fail closed, and not change prior memory. */
    damage(path,16+15);
    count=999;
    assert(ui_snapshot_load(path,loaded,2,&count)==-1);
    assert(count==0);
    assert(memcmp(loaded,games,sizeof loaded)==0);
    assert(ui_snapshot_save(path,games,2)==0);
    FILE *f=fopen(path,"ab");
    assert(f!=NULL && fputc('!',f)!=EOF && fclose(f)==0);
    assert(ui_snapshot_load(path,loaded,2,&count)==-1);
    assert(memcmp(loaded,games,sizeof loaded)==0);

    /* Invalid writes never replace an existing good cache. */
    assert(ui_snapshot_save(path,games,2)==0);
    setup(&games[1],"CUSA02343","Duplicate","01.02","gd");
    assert(ui_snapshot_save(path,games,2)<0);
    assert(ui_snapshot_load(path,loaded,2,&count)==1);
    assert(count==2 && !strcmp(loaded[1].cusa,"CUSA00002"));
    setup(&games[1],"CUSA00002","Patch","01.02","gp");
    assert(ui_snapshot_save(path,games,2)<0);
    setup(&games[1],"CUSA00002","Another game","01.02","gdo");
    assert(ui_snapshot_save(path,games,UI_SNAPSHOT_LIMIT+1u)<0);
    assert(ui_snapshot_load(path,loaded,1,&count)==-1);
    assert(count==0);
    assert(ui_snapshot_load(path,loaded,2,&count)==1);
    assert(count==2);

    /* Only a correctly completed scan may explicitly clear a cache. */
    assert(ui_snapshot_save(path,NULL,0)==0);
    assert(ui_snapshot_load(path,NULL,0,&count)==1);
    assert(count==0);
    f=fopen(path,"wb");
    assert(f!=NULL && fwrite("BAD",1,3,f)==3 && fclose(f)==0);
    assert(ui_snapshot_load(path,loaded,2,&count)==-1);
    assert(count==0);
    remove(path);
    puts("UI_SNAPSHOT_HOST_OK");
    return 0;
}
