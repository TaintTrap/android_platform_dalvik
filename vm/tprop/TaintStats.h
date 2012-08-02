/* Note, this file should be included near the end of Dalvik.h */

#ifndef _DALVIK_TPROP_TAINT_STATS
#define _DALVIK_TPROP_TAINT_STATS

/* Dump flags */
#define kDumpFieldFullDetail 1

/* Called from dvmStartup */
bool dvmTaintInitStats();

/* Build stats for each iput/sput operation */
void dvmAddFieldStats(const Field* field, const Method* method, u4 tag);

/* Dumps all stats to logcat */
void dvmDumpAllFieldStats(int flags);

/* Shutdown called from dvmShutdown() */
void dvmTaintStatsShutdown();

#endif
