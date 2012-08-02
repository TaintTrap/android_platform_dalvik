#include "Dalvik.h"

/* Default hash table entries */
#define TAINT_STATS_FIELD_TABLE_SIZE  32   /* number of fields for each class */
#define TAINT_STATS_USAGE_TABLE_SIZE  1    /* number of different locations setting a field */

typedef struct {
    const char* location;       /* e.g. Ljava/util/HashSet;.size */
    /* const ClassObject* clazz; */
    int         tainted;        /* per location */
    int         total;          /* per location */
} FieldUsageEntry;

typedef struct {
    /* const Field*       field; */
    const char*        fullDescriptor; /* e.g. Landroid/os/Message;->target */
    int                tainted;
    int                total;
    HashTable*         usage;
} FieldStatsEntry;

static int hashcmpFieldUsage(const void* ptr1, const void* ptr2)
{
    FieldUsageEntry* p1 = (FieldUsageEntry*) ptr1;
    FieldUsageEntry* p2 = (FieldUsageEntry*) ptr2;

    return strcmp(p1->location, p2->location);
}

static int hashcmpFieldStats(const void* ptr1, const void* ptr2)
{
    FieldStatsEntry* p1 = (FieldStatsEntry*) ptr1;
    FieldStatsEntry* p2 = (FieldStatsEntry*) ptr2;

    return strcmp(p1->fullDescriptor, p2->fullDescriptor);
}

static void freeFieldUsage(void* entry)
{
    FieldUsageEntry* fu = (FieldUsageEntry*) entry;
    if (fu != NULL && fu->location != NULL) {
        free((char *)fu->location);
    }
}

static void freeFieldStats(void* entry)
{
    FieldStatsEntry* fs = (FieldStatsEntry*) entry;
    if (fs != NULL) {
        if (fs->usage != NULL) {
            dvmHashTableFree(fs->usage);
        }
        if (fs->fullDescriptor != NULL) {
            free((char *)fs->fullDescriptor);
        }
    }
}

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
    //gDvm.fieldStatsTable     = dvmHashTableCreate(dvmHashSize(TAINT_STATS_FIELD_TABLE_SIZE),
    //                                              freeFieldStats);
    gDvm.fieldStatsTable     = NULL;

    return true;
}

void dvmAddFieldStats(const Field* field, const Method* method, u4 tag)
{
    /* Only track stats if we are a target AND tainted data has been seen */
    if (gDvm.taintTarget == false || gDvm.taintStarted == false) return;

    u4                hash;
    int               len;
    char*             fullDescriptor;
    char*             location;
    FieldStatsEntry   dummyFieldStatsEntry; /* for hash table lookup */
    FieldUsageEntry   dummyFieldUsageEntry; /* for hash table lookup */
    FieldStatsEntry*  realFieldStatsEntry;  /* for hash table storage */
    FieldUsageEntry*  realFieldUsageEntry;  /* for hash table storage */

    if (gDvm.fieldStatsTable == NULL) {
        LOGE("[STATS] Massive error - fieldStatsTable is uninitialized.");
        return;
    }

    dvmHashTableLock(gDvm.fieldStatsTable);

    int clazzDescriptorLen = strlen(field->clazz->descriptor);
    int fieldNameLen = strlen(field->name);

    if (false) {
        LOGD("[STATS] field->clazz->descriptor=%s len=%d field->name=%s len=%d",
             field->clazz->descriptor,
             clazzDescriptorLen,
             field->name,
             fieldNameLen);
    }

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
    fullDescriptor = calloc(len+1, sizeof(char));
    strcpy(fullDescriptor, field->clazz->descriptor);
    strcat(fullDescriptor, "->");
    strcat(fullDescriptor, field->name);
    hash = dvmComputeUtf8Hash(fullDescriptor);
    
    /* LOGD("[STATS] fullDescriptor=%s len=%d hash=%u", fullDescriptor, len, hash);  */

    dummyFieldStatsEntry.fullDescriptor = fullDescriptor;

    /* Check if field entry already exists but don't add */
    realFieldStatsEntry = dvmHashTableLookup(gDvm.fieldStatsTable, hash,
                                             &dummyFieldStatsEntry,
                                             (HashCompareFunc) hashcmpFieldStats,
                                             false);

    /* If entry was not found, create and populate one entry */
    if (realFieldStatsEntry == NULL) {
        realFieldStatsEntry                 = (FieldStatsEntry *) calloc(1, sizeof(FieldStatsEntry));
        realFieldStatsEntry->fullDescriptor = fullDescriptor;
        realFieldStatsEntry->tainted        = 0;
        realFieldStatsEntry->total          = 0;
        realFieldStatsEntry->usage          = dvmHashTableCreate(dvmHashSize(TAINT_STATS_USAGE_TABLE_SIZE),
                                                                 freeFieldUsage);

        dvmHashTableLookup(gDvm.fieldStatsTable, hash,
                           realFieldStatsEntry,
                           (HashCompareFunc) hashcmpFieldStats,
                           true);
    }

    realFieldStatsEntry->total++;
    if (tag != TAINT_CLEAR) {
        realFieldStatsEntry->tainted++;
    }

    if (false) {
        LOGD("[STATS] realFieldStatsEntry field=%s tainted=%d total=%d usage=%p",
             realFieldStatsEntry->fullDescriptor,
             realFieldStatsEntry->tainted,
             realFieldStatsEntry->total,
             realFieldStatsEntry->usage);

    }

    len = strlen(method->clazz->descriptor) + 1 + strlen(method->name);
    location = calloc(len+1, sizeof(char));
    strcpy(location, method->clazz->descriptor);
    strcat(location, ".");
    strcat(location, method->name);
    hash = dvmComputeUtf8Hash(location);

    dummyFieldUsageEntry.location = location;

    /* Lookup usage stats inside field stats */
    realFieldUsageEntry = dvmHashTableLookup(realFieldStatsEntry->usage, hash,
                                             &dummyFieldUsageEntry,
                                             (HashCompareFunc) hashcmpFieldUsage,
                                             false);

    /* If entry was not found, create and populate one entry */
    if (realFieldUsageEntry == NULL) {
        realFieldUsageEntry           = (FieldUsageEntry *) calloc(1, sizeof(FieldUsageEntry));
        realFieldUsageEntry->location = location;
        realFieldUsageEntry->tainted  = 0;
        realFieldUsageEntry->total    = 0;

        dvmHashTableLookup(realFieldStatsEntry->usage, hash,
                           realFieldUsageEntry,
                           (HashCompareFunc) hashcmpFieldUsage,
                           true);
    }

    realFieldUsageEntry->total++;
    if (tag != TAINT_CLEAR) {
        realFieldUsageEntry->tainted++;
    }

    /* if (tag != TAINT_CLEAR) { */
    /*     LOGD("[STATS] Field %s type=%s loc=%s tag=0x%04x tainted=%d total=%d", */
    /*          fullDescriptor, */
    /*          field->signature, */
    /*          location, */
    /*          tag, */
    /*          realFieldStatsEntry->tainted, */
    /*          realFieldStatsEntry->total); */
    /*     LOGD("[STATS] Location %s tainted=%d total=%d", */
    /*          realFieldUsageEntry->location, */
    /*          realFieldUsageEntry->tainted, */
    /*          realFieldUsageEntry->total); */
    /* } */

    dvmHashTableUnlock(gDvm.fieldStatsTable);

    // DEBUG
    /* dvmDumpAllFieldStats(kDumpFieldFullDetail); */
}

static int dumpFieldUsage(void* ventry, void* varg)
{
    const FieldUsageEntry* fu = (const FieldUsageEntry*) ventry;
    /* int flags = (int) varg; */

    if (fu->location == NULL) {
        LOGI("[STATS] dumpFieldUsage ignoring request to dump null usage entry");
        return 0;
    }
    //if (fu->tainted) {          /* dump only if tainted data exists */
        LOGI("[STATS] dumpFieldUsage location=%s tainted=%d total=%d",
             fu->location,
             fu->tainted,
             fu->total);
    //}

    return 0;
}

static int dumpFieldStats(void* ventry, void* varg)
{
    const FieldStatsEntry* fs = (const FieldStatsEntry*) ventry;
    int flags = (int) varg;

    if (fs->fullDescriptor == NULL) {
        LOGI("[STATS] dumpFieldStats ignoring request to dump null class");
        return 0;
    }
    
    //if (fs->tainted) {          /* dump only if tainted data exists */
    LOGI("[STATS] dumpFieldStats field=%s tainted=%d total=%d",
         fs->fullDescriptor,
         fs->tainted,
         fs->total);

    /* Print hash table stats */
    LOGD("[STATS] dumpFieldStats usage: entries=%d memory=%d",
         dvmHashTableNumEntries(fs->usage),
         dvmHashTableMemUsage(fs->usage));

    if ((flags & kDumpFieldFullDetail) != 0) {
        dvmHashForeach(fs->usage, dumpFieldUsage, 0);
    }
    //}

    return 0;
}

void dvmDumpAllFieldStats(int flags)
{
    if (gDvm.taintTarget && gDvm.taintStarted) {
        dvmHashTableLock(gDvm.fieldStatsTable);

        /* Print hash table stats */
        LOGI("[STATS] Usage: entries=%d memory=%d",
             dvmHashTableNumEntries(gDvm.fieldStatsTable),
             dvmHashTableMemUsage(gDvm.fieldStatsTable));


        dvmHashForeach(gDvm.fieldStatsTable, dumpFieldStats, (void *)flags);
        dvmHashTableUnlock(gDvm.fieldStatsTable);
    }
}

void dvmTaintStatsShutdown()
{
    dvmHashTableFree(gDvm.fieldStatsTable);
}
