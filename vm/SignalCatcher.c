/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is a thread that catches signals and does something useful.  For
 * example, when a SIGQUIT (Ctrl-\) arrives, suspend the VM and dump the
 * status of all threads.
 */
#include "Dalvik.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#include <cutils/open_memstream.h>

static void* signalCatcherThreadStart(void* arg);

/*
 * Crank up the signal catcher thread.
 *
 * Returns immediately.
 */
bool dvmSignalCatcherStartup(void)
{
    gDvm.haltSignalCatcher = false;

    if (!dvmCreateInternalThread(&gDvm.signalCatcherHandle,
                "Signal Catcher", signalCatcherThreadStart, NULL))
        return false;

    return true;
}

/*
 * Shut down the signal catcher thread if it was started.
 *
 * Since we know the thread is just sitting around waiting for signals
 * to arrive, send it one.
 */
void dvmSignalCatcherShutdown(void)
{
    gDvm.haltSignalCatcher = true;
    if (gDvm.signalCatcherHandle == 0)      // not started yet
        return;

    pthread_kill(gDvm.signalCatcherHandle, SIGQUIT);

    pthread_join(gDvm.signalCatcherHandle, NULL);
    LOGV("signal catcher has shut down\n");
}


/*
 * Print the name of the current process, if we can get it.
 */
static void printProcessName(const DebugOutputTarget* target)
{
    int fd = -1;

    fd = open("/proc/self/cmdline", O_RDONLY, 0);
    if (fd < 0)
        goto bail;

    char tmpBuf[256];
    ssize_t actual;

    actual = read(fd, tmpBuf, sizeof(tmpBuf)-1);
    if (actual <= 0)
        goto bail;

    tmpBuf[actual] = '\0';
    dvmPrintDebugMessage(target, "Cmd line: %s\n", tmpBuf);

bail:
    if (fd >= 0)
        close(fd);
}

/*
 * Dump the stack traces for all threads to the supplied file, putting
 * a timestamp header on it.
 */
static void logThreadStacks(FILE* fp)
{
    DebugOutputTarget target;

    dvmCreateFileOutputTarget(&target, fp);

    pid_t pid = getpid();
    time_t now = time(NULL);
    struct tm* ptm;
#ifdef HAVE_LOCALTIME_R
    struct tm tmbuf;
    ptm = localtime_r(&now, &tmbuf);
#else
    ptm = localtime(&now);
#endif
    dvmPrintDebugMessage(&target,
        "\n\n----- pid %d at %04d-%02d-%02d %02d:%02d:%02d -----\n",
        pid, ptm->tm_year + 1900, ptm->tm_mon+1, ptm->tm_mday,
        ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    printProcessName(&target);
    dvmPrintDebugMessage(&target, "\n");
    dvmDumpAllThreadsEx(&target, true);
    fprintf(fp, "----- end %d -----\n", pid);
}


/*
 * Respond to a SIGQUIT by dumping the thread stacks.  Optionally dump
 * a few other things while we're at it.
 *
 * Thread stacks can either go to the log or to a file designated for holding
 * ANR traces.  If we're writing to a file, we want to do it in one shot,
 * so we can use a single O_APPEND write instead of contending for exclusive
 * access with flock().  There may be an advantage in resuming the VM
 * before doing the file write, so we don't stall the VM if disk I/O is
 * bottlenecked.
 *
 * If JIT tuning is compiled in, dump compiler stats as well.
 */
static void handleSigQuit(void)
{
    char* traceBuf = NULL;
    size_t traceLen;

    dvmSuspendAllThreads(SUSPEND_FOR_STACK_DUMP);

    dvmDumpLoaderStats("sig");

    if (gDvm.stackTraceFile == NULL) {
        /* just dump to log */
        DebugOutputTarget target;
        dvmCreateLogOutputTarget(&target, ANDROID_LOG_INFO, LOG_TAG);
        dvmDumpAllThreadsEx(&target, true);
    } else {
        /* write to memory buffer */
        FILE* memfp = open_memstream(&traceBuf, &traceLen);
        if (memfp == NULL) {
            LOGE("Unable to create memstream for stack traces\n");
            traceBuf = NULL;        /* make sure it didn't touch this */
            /* continue on */
        } else {
            logThreadStacks(memfp);
            fclose(memfp);
        }
    }

#if defined(WITH_JIT) && defined(WITH_JIT_TUNING)
    dvmCompilerDumpStats();
#endif

    if (false) {
        dvmLockMutex(&gDvm.jniGlobalRefLock);
        dvmDumpReferenceTable(&gDvm.jniGlobalRefTable, "JNI global");
        dvmUnlockMutex(&gDvm.jniGlobalRefLock);
    }
    if (false) dvmDumpTrackedAllocations(true);

    dvmResumeAllThreads(SUSPEND_FOR_STACK_DUMP);

    if (traceBuf != NULL) {
        /*
         * We don't know how long it will take to do the disk I/O, so put us
         * into VMWAIT for the duration.
         */
        int oldStatus = dvmChangeStatus(dvmThreadSelf(), THREAD_VMWAIT);

        /*
         * Open the stack trace output file, creating it if necessary.  It
         * needs to be world-writable so other processes can write to it.
         */
        int fd = open(gDvm.stackTraceFile, O_WRONLY | O_APPEND | O_CREAT, 0666);
        if (fd < 0) {
            LOGE("Unable to open stack trace file '%s': %s\n",
                gDvm.stackTraceFile, strerror(errno));
        } else {
            ssize_t actual = write(fd, traceBuf, traceLen);
            if (actual != (ssize_t) traceLen) {
                LOGE("Failed to write stack traces to %s (%d of %zd): %s\n",
                    gDvm.stackTraceFile, (int) actual, traceLen,
                    strerror(errno));
            } else {
                LOGI("Wrote stack traces to '%s'\n", gDvm.stackTraceFile);
            }
            close(fd);
        }

        free(traceBuf);
        dvmChangeStatus(dvmThreadSelf(), oldStatus);
    }
}

/*
 * Respond to a SIGUSR1 by forcing a GC.
 */
static void handleSigUsr1(void)
{
    LOGI("SIGUSR1 forcing GC (no HPROF)\n");
    if (gDvm.forceGC == true)
        LOGE("forceGC was already set! Concurrency bug?");
    gDvm.forceGC = true;
    dvmCollectGarbage(false);
    gDvm.forceGC = false;
}

#if defined(WITH_JIT) && defined(WITH_JIT_TUNING)
/*
 * Respond to a SIGUSR2 by dumping some JIT stats and possibly resetting
 * the code cache.
 */
static void handleSigUsr2(void)
{
    static int codeCacheResetCount = 0;
    if ((--codeCacheResetCount & 7) == 0) {
        gDvmJit.codeCacheFull = true;
    } else {
        dvmCompilerDumpStats();
        /* Stress-test unchain all */
        dvmJitUnchainAll();
        LOGD("Send %d more signals to rest the code cache",
             codeCacheResetCount & 7);
    }
}
#endif

#ifdef WITH_TAINT_TRACKING

static inline void printTaintStats()
{
    LOGI("STATS [Interval] [Tainted / Total Tainted / Ops / Total Ops] # %d %d %d %d\n",
         gDvm.statsTainted - gDvm.statsPrevTainted,
         gDvm.statsTainted,
         gDvm.statsTotal - gDvm.statsPrevTotal,
         gDvm.statsTotal);
    gDvm.statsPrevTainted = gDvm.statsTainted;
    gDvm.statsPrevTotal   = gDvm.statsTotal;
}

static void handleSigTaintStats(void)
{
    dvmSuspendAllThreads(SUSPEND_FOR_DEBUG_EVENT);

    /* LOGD("[IFLOW] Entry tainted = %d total = %d", gDvm.statsTainted, gDvm.statsTotal); */

    Thread* thread;
    /* LOGD("[IFLOW] Aggregating %d threads...", gDvm.nonDaemonThreadCount); */
    for (thread = gDvm.threadList; thread != NULL; thread = thread->next) {
        /* Aggregate thread counters */
        gDvm.statsTainted   += thread->statsTainted;
        gDvm.statsTotal     += thread->statsTotal;

        /* LOGD("[IFLOW] [tid %d] tainted = %d total = %d threadTainted = %d threadTotal = %d", */
        /*      thread->threadId, */
        /*      gDvm.statsTainted, gDvm.statsTotal, */
        /*      thread->statsTainted, thread->statsTotal); */

        /* Reset thread counters */
        thread->statsTainted = 0;
        thread->statsTotal   = 0;
    }
    /* Skip stats until first tainted data shows up */
    /*
    if (gDvm.statsTainted == 0) {
        gDvm.statsTotal = 0;
    } else {
        printTaintStats();
    }
    */
    printTaintStats();

    dvmResumeAllThreads(SUSPEND_FOR_DEBUG_EVENT);
}
#endif

/*
 * Sleep in sigwait() until a signal arrives.
 */
static void* signalCatcherThreadStart(void* arg)
{
    Thread* self = dvmThreadSelf();
    sigset_t mask;
    int cc;

    UNUSED_PARAMETER(arg);

    LOGV("Signal catcher thread started (threadid=%d)\n", self->threadId);

    /* set up mask with signals we want to handle */
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGUSR1);
#if defined(WITH_JIT) && defined(WITH_JIT_TUNING)
    sigaddset(&mask, SIGUSR2);
#endif
#ifdef WITH_TAINT_TRACKING
    sigaddset(&mask, SIGALRM);
#endif

    while (true) {
        int rcvd;

        dvmChangeStatus(self, THREAD_VMWAIT);

        /*
         * Signals for sigwait() must be blocked but not ignored.  We
         * block signals like SIGQUIT for all threads, so the condition
         * is met.  When the signal hits, we wake up, without any signal
         * handlers being invoked.
         *
         * When running under GDB we occasionally return from sigwait()
         * with EINTR (e.g. when other threads exit).
         */
loop:
        cc = sigwait(&mask, &rcvd);
        if (cc != 0) {
            if (cc == EINTR) {
                //LOGV("sigwait: EINTR\n");
                goto loop;
            }
            assert(!"bad result from sigwait");
        }

        if (!gDvm.haltSignalCatcher) {
            LOGI("threadid=%d: reacting to signal %d\n",
                dvmThreadSelf()->threadId, rcvd);
        }

        /* set our status to RUNNING, self-suspending if GC in progress */
        dvmChangeStatus(self, THREAD_RUNNING);

        if (gDvm.haltSignalCatcher)
            break;

        switch (rcvd) {
        case SIGQUIT:
            handleSigQuit();
            break;
        case SIGUSR1:
            handleSigUsr1();
            break;
#if defined(WITH_JIT) && defined(WITH_JIT_TUNING)
        case SIGUSR2:
            handleSigUsr2();
            break;
#endif
#ifdef WITH_TAINT_TRACKING
        case SIGALRM:
            handleSigTaintStats();
            // VIP: leverage GC to gather stats on tainted bytes and ranges
            handleSigUsr1();
            break;
#endif
        default:
            LOGE("unexpected signal %d\n", rcvd);
            break;
        }
    }

    return NULL;
}
