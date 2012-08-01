#include "Dalvik.h"

/* Default hash table entries */
#define TAINT_STATS_OBJECT_TABLE_SIZE 1024 /* number of classes with stats */
#define TAINT_STATS_FIELD_TABLE_SIZE  1024 /* number of fields for each class */
#define TAINT_STATS_USAGE_TABLE_SIZE  128 /* number of different locations setting a field */

typedef struct {
    const char* location;       /* e.g. Ljava/util/HashSet;.size */
    const ClassObject* clazz;
    int         tainted;        /* per location */
    int         total;          /* per location */
} FieldUsageEntry;

typedef struct {
    /* const Field*       field; */
    const char *       name;
    const char *       class;
    /* const char*        fullDescriptor; /\* e.g. Landroid/os/Message;->target *\/ */
    int                tainted;
    int                total;
    HashTable*         usage;
} FieldStatsEntry;

/* typedef struct { */
/*     const ClassObject* clazz; */
/*     HashTable*         fieldStats; */
/* } ObjectStatsEntry; */

/* static int hashcmpFieldUsage(const void* ptr1, const void* ptr2) */
/* { */
/*     FieldUsageEntry* p1 = (FieldUsageEntry*) ptr1; */
/*     FieldUsageEntry* p2 = (FieldUsageEntry*) ptr2; */

/*     return strcmp(p1->location, p2->location); */
/* } */

static int hashcmpFieldStats(const void* ptr1, const void* ptr2)
{
    FieldStatsEntry* p1 = (FieldStatsEntry*) ptr1;
    FieldStatsEntry* p2 = (FieldStatsEntry*) ptr2;

    /* return strcmp(p1->field->name, p2->field->name); */
    if (p1 == NULL || p2 == NULL) {
        LOGE("[STATS] hashcmpFieldStats null entries p1=%p p2=%p", p1, p2);
        return -1;
    }
    /* if (p1->fullDescriptor == NULL || p2->fullDescriptor == NULL) { */
    /*     LOGE("[STATS] hashcmpFieldStats null descriptors p1 desc=%s p2 desc=%s", */
    /*          p1->fullDescriptor, */
    /*          p2->fullDescriptor); */
    /*     return -1; */
    /* } */
    /* return strcmp(p1->fullDescriptor, p2->fullDescriptor); */
    int ret;
    /* ret = strcmp(p1->field->clazz->descriptor, p2->field->clazz->descriptor); */
    /* if (ret == 0) { */
    /*     ret = strcmp(p1->field->name, p2->field->name); */
    /* } */
    LOGD("[STATS] hashcmpFieldStats p1->class=%s p2->class=%s", p1->class, p2->class);
    ret = strcmp(p1->class, p2->class);
    if (ret == 0) {
        LOGD("[STATS] hashcmpFieldStats p1->name=%s p2->name=%s", p1->name, p2->name);
        ret = strcmp(p1->name, p2->name);
        LOGD("[STATS] cmp identical %s->%s", p1->class, p1->name);
    }
    return ret;
}

/* static int hashcmpObjectStats(const void* ptr1, const void* ptr2) */
/* { */
/*     ObjectStatsEntry* p1 = (ObjectStatsEntry*) ptr1; */
/*     ObjectStatsEntry* p2 = (ObjectStatsEntry*) ptr2; */

/*     return strcmp(p1->clazz->descriptor, p2->clazz->descriptor); */
/* } */

/* static void freeFieldUsage(void* entry) */
/* { */
/*     FieldUsageEntry* fu = (FieldUsageEntry*) entry; */
/*     if (fu != NULL && fu->location != NULL) { */
/*         /\* free(fu->location); *\/ */
/*     } */
/* } */

static void freeFieldStats(void* entry)
{
    FieldStatsEntry* fs = (FieldStatsEntry*) entry;
    if (fs != NULL && fs->usage != NULL) {
        dvmHashTableFree(fs->usage);
    }
}

/* static void freeObjectStats(void* entry) */
/* { */
/*     ObjectStatsEntry* os = (ObjectStatsEntry*) entry; */
/*     if (os != NULL) { */
/*         dvmHashTableFree(os->fieldStats); */
/*     } */
/* } */


bool dvmTaintInitStats()
{
    gDvm.taintTarget         = false;
    gDvm.taintStarted        = false;
    /* Stats lock */
    dvmInitMutex(&gDvm.statsLock);
    /* Stats counters */
    gDvm.statsTotal          = 0;
    gDvm.statsTainted        = 0;
    gDvm.statsPrevTainted    = 0;
    gDvm.statsPrevTainted    = 0;
    /* Detailed counters */
    gDvm.statsTotalReg       = 0;
    gDvm.statsTaintedReg     = 0;
    gDvm.statsTotalRegWide   = 0;
    gDvm.statsTaintedRegWide = 0;
    gDvm.statsTotalArr       = 0;
    gDvm.statsTaintedArr     = 0;
    gDvm.statsTotalRet       = 0;
    gDvm.statsTaintedRet     = 0;
    /* Object field stats */
    /* gDvm.objectStatsTable     = dvmHashTableCreate(dvmHashSize(TAINT_STATS_OBJECT_TABLE_SIZE), freeObjectStats); */
    gDvm.fieldStatsTable     = dvmHashTableCreate(dvmHashSize(TAINT_STATS_FIELD_TABLE_SIZE), 
                                                  /* freeFieldStats); */
                                                  NULL);

    return true;
}

void dvmAddFieldStats(const Field* field, const Method* method, u4 tag)
{
    /* Only track stats if we are a target AND tainted data has been seen */
    if (gDvm.taintTarget == false || gDvm.taintStarted == false) return;

    u4                hash;
    int               len;
    char*             fullDescriptor;
    /* char*             location; */
    /* ObjectStatsEntry* objectStatsIn; */
    /* ObjectStatsEntry* objectStatsOut; */
    FieldStatsEntry   dummyFieldStatsEntry; /* for hash table lookup */
    FieldStatsEntry*  realFieldStatsEntry;    /* for hash table storage */
    /* FieldUsageEntry  fieldUsageIn; */
    /* FieldUsageEntry*  fieldUsageOut; */

    if (gDvm.fieldStatsTable == NULL) {
        LOGE("[STATS] Massive error - fieldStatsTable is uninitialized.");
        return;
    }

    dvmHashTableLock(gDvm.fieldStatsTable);

    int clazzDescriptorLen = strlen(field->clazz->descriptor);
    int fieldNameLen = strlen(field->name);

    LOGD("[STATS] field->clazz->descriptor=%s len=%d field->name=%s len=%d", 
         field->clazz->descriptor,
         clazzDescriptorLen, 
         field->name,
         fieldNameLen);

    if (clazzDescriptorLen < 1) {
        LOGE("[STATS] Error invalid len for field->clazz->descriptor=%d", 
             clazzDescriptorLen);
        return;
    }

    if (field->clazz->descriptor[clazzDescriptorLen] != '\0') {
        LOGE("[STATS] Error string not null terminated field->clazz->descriptor=%s", 
             field->clazz->descriptor);
        return;
    }

    if (fieldNameLen < 1) {
        LOGE("[STATS] Error invalid len for field->name=%d", fieldNameLen);
        return;
    }

    if (field->name[fieldNameLen] != '\0') {
        LOGE("[STATS] Error string not null terminated field->name=%s", field->name);
        return;
    }

    len = strlen(field->clazz->descriptor) + 2 + strlen(field->name);
    /* /\* fullDescriptor = calloc(len+1, sizeof(char)); *\/ */
    fullDescriptor = malloc(len+1);
    strcpy(fullDescriptor, field->clazz->descriptor);
    strcat(fullDescriptor, "->");
    strcat(fullDescriptor, field->name);
    hash = dvmComputeUtf8Hash(fullDescriptor);
    free(fullDescriptor);
    
    /* if (fullDescriptor == NULL) { */
    /*     LOGE("[STATS] Error invalid descriptor! descriptor=%s",fullDescriptor); */
    /*     return; */
    /* } */

    /* LOGD("[STATS] fullDescriptor=%s len=%d hash=%u", fullDescriptor, len, hash);  */

    /* dummyFieldStatsEntry = calloc(1, sizeof(FieldStatsEntry)); */
    /* dummyFieldStatsEntry.field = field; */
    /* dummyFieldStatsEntry.fullDescriptor = strdup(fullDescriptor); */
    /* dummyFieldStatsEntry.fullDescriptor = fullDescriptor; */
    dummyFieldStatsEntry.name  = strdup(field->name);
    dummyFieldStatsEntry.class = strdup(field->clazz->descriptor);

    /* Print hash table stats */
    LOGI("[STATS] Usage: entries=%d memory=%d", 
         dvmHashTableNumEntries(gDvm.fieldStatsTable),
         dvmHashTableMemUsage(gDvm.fieldStatsTable));

    /* Check if field entry already exists but don't add */
    realFieldStatsEntry = dvmHashTableLookup(gDvm.fieldStatsTable, hash,
                                             &dummyFieldStatsEntry,
                                             (HashCompareFunc) hashcmpFieldStats,
                                             false);

    /* If entry was not found, create and populate one entry */
    if (realFieldStatsEntry == NULL) {
        realFieldStatsEntry          = (FieldStatsEntry *) calloc(1, sizeof(FieldStatsEntry));
        realFieldStatsEntry->name    = strdup(field->name);
        realFieldStatsEntry->class   = strdup(field->clazz->descriptor);
        realFieldStatsEntry->tainted = 0;
        realFieldStatsEntry->total   = 0;
        realFieldStatsEntry->usage   = NULL;
        
        dvmHashTableLookup(gDvm.fieldStatsTable, hash,
                           realFieldStatsEntry,
                           (HashCompareFunc) hashcmpFieldStats,
                           true);
    }

    /* Print hash table stats */
    LOGI("[STATS] Usage: entries=%d memory=%d",
         dvmHashTableNumEntries(gDvm.fieldStatsTable),
         dvmHashTableMemUsage(gDvm.fieldStatsTable));

    /* if (realFieldStatsEntry->fullDescriptor == NULL) { */
    /*     LOGE("[STATS] Error invalid descriptor after lookup! descriptor=%s", realFieldStatsEntry->fullDescriptor); */
    /*     return; */
    /* } */

    /* if (strcmp(dummyFieldStatsEntry.fullDescriptor, realFieldStatsEntry->fullDescriptor) != 0) { */
    /*     LOGE("[STATS] Error fieldDescriptor mismatch! in=%s out=%s", */
    /*          dummyFieldStatsEntry.fullDescriptor, */
    /*          realFieldStatsEntry->fullDescriptor); */
    /* } */

    /* free(dummyFieldStatsEntry); */
    /* realFieldStatsEntry->fullDescriptor = fullDescriptor; */

    realFieldStatsEntry->total++;
    if (tag != TAINT_CLEAR) {
        realFieldStatsEntry->tainted++;
    }

    /* if (realFieldStatsEntry->usage == NULL) { */
    /*     realFieldStatsEntry->usage = dvmHashTableCreate(dvmHashSize(TAINT_STATS_USAGE_TABLE_SIZE), freeFieldUsage); */
    /* } */

    /* LOGD("[STATS] realFieldStatsEntry field=%s tainted=%d total=%d usage=%p",  */
    /*      realFieldStatsEntry->field->name,  */
    /*      realFieldStatsEntry->tainted, */
    /*      realFieldStatsEntry->total, */
    /*      realFieldStatsEntry->usage); */

    /* len = strlen(method->clazz->descriptor) + 1 + strlen(method->name); */
    /* location = calloc(len+1, sizeof(char)); */
    /* strcpy(location, method->clazz->descriptor); */
    /* strcat(location, "."); */
    /* strcat(location, method->name); */
    /* hash = dvmComputeUtf8Hash(location); */

    /* fieldUsageIn = calloc(1, sizeof(FieldUsageEntry)); */
    /* fieldUsageIn.location = location; */
    /* fieldUsageIn.clazz = method; */
    /* fieldUsageIn.tainted = 0; */
    /* fieldUsageIn.total = 0; */

    /* Lookup usage stats inside field stats */
    /* fieldUsageOut = dvmHashTableLookup(realFieldStatsEntry->usage, hash, &fieldUsageIn, hashcmpFieldUsage, true); */
    
    /* free(fieldUsageIn); */

    /* if (tag != TAINT_CLEAR) { */
    /*     fieldUsageOut->tainted++; */
    /* } */
    /* fieldUsageOut->total++; */

    /* if (tag != TAINT_CLEAR) { */
    /* LOGD("[STATS] Field %s->%s type=%s loc=%s tag=0x%04x tainted=%d total=%d", */
    LOGD("[STATS] Field %s->%s type=%s tag=0x%04x tainted=%d total=%d",
         field->clazz->descriptor,
         field->name,
         field->signature,
         /* location, */
         tag,
         realFieldStatsEntry->tainted,
         realFieldStatsEntry->total);
    /* } */

    dvmHashTableUnlock(gDvm.fieldStatsTable);

    // DEBUG
    /* dvmDumpAllFieldStats(kDumpFieldFullDetail); */
}

/* static int dumpFieldUsage(void* ventry, void* varg) */
/* { */
/*     const FieldUsageEntry* fu = (const FieldUsageEntry*) ventry; */
/*     /\* int flags = (int) varg; *\/ */

/*     if (fu->location == NULL) { */
/*         LOGI("[STATS] dumpFieldUsage ignoring request to dump null usage entry"); */
/*         return 0; */
/*     } */
/*     if (fu->tainted) {          /\* dump only if tainted data exists *\/ */
/*         LOGI("[STATS] dumpFieldUsage location=%s tainted=%d total=%d", */
/*              fu->location, */
/*              fu->tainted, */
/*              fu->total); */
/*     } */

/*     return 0; */
/* } */

/* static int dumpFieldStats(void* ventry, void* varg) */
/* { */
/*     const FieldStatsEntry* fs = (const FieldStatsEntry*) ventry; */
/*     int flags = (int) varg; */

/*     /\* if (fs->fullDescriptor == NULL) { *\/ */
/*     /\*     LOGI("[STATS] dumpFieldStats ignoring request to dump null class"); *\/ */
/*     /\*     return 0; *\/ */
/*     /\* } *\/ */
    
/*     /\* if (fs->fullDescriptor[0] == 'L' && fs->tainted) { /\\* dump only if tainted data exists *\\/ *\/ */
/*     /\*     LOGI("[STATS] dumpFieldStats field=%s tainted=%d total=%d",  *\/ */
/*     /\*          fs->fullDescriptor, *\/ */
/*     /\*          fs->tainted, *\/ */
/*     /\*          fs->total); *\/ */
/*     /\*     if ((flags & kDumpFieldFullDetail) != 0) { *\/ */
/*     /\*         dvmHashForeach(fs->usage, dumpFieldUsage, 0); *\/ */
/*     /\*     } *\/ */
/*     /\* } *\/ */

/*     return 0; */
/* } */

/* void dvmDumpAllFieldStats(int flags) */
/* { */
/*     if (gDvm.taintTarget && gDvm.taintStarted) { */
/*         dvmHashTableLock(gDvm.fieldStatsTable); */
/*         dvmHashForeach(gDvm.fieldStatsTable, dumpFieldStats, (void *)flags); */
/*         dvmHashTableUnlock(gDvm.fieldStatsTable); */
/*     } */
/* } */

/* UNUSED yet */
/* void dvmTaintStatsShutdown() */
/* { */
/*     dvmHashTableFree(gDvm.fieldStatsTable); */
/* } */
