/*
 * Main interpreter loop.
 *
 * This was written with an ARM implementation in mind.
 */
bool INTERP_FUNC_NAME(Thread* self, InterpState* interpState)
{
#if defined(EASY_GDB)
    StackSaveArea* debugSaveArea = SAVEAREA_FROM_FP(self->curFrame);
#endif
#if INTERP_TYPE == INTERP_DBG
    bool debugIsMethodEntry = false;
    debugIsMethodEntry = interpState->debugIsMethodEntry;
#endif
#if defined(WITH_TRACKREF_CHECKS)
    int debugTrackedRefStart = interpState->debugTrackedRefStart;
#endif
    DvmDex* methodClassDex;     // curMethod->clazz->pDvmDex
    JValue retval;
#ifdef WITH_TAINT_TRACKING
    Taint rtaint;
#endif

    /* core state */
    const Method* curMethod;    // method we're interpreting
    // VALI removed const in order to make changes
    /* Method* curMethod;          // method we're interpreting */

    const u2* pc;               // program counter
    u4* fp;                     // frame pointer
    u2 inst;                    // current instruction
    /* instruction decoding */
    u2 ref;                     // 16-bit quantity fetched directly
    u2 vsrc1, vsrc2, vdst;      // usually used for register indexes
    /* method call setup */
    const Method* methodToCall;
    bool methodCallRange;

#if defined(THREADED_INTERP)
    /* static computed goto table */
    DEFINE_GOTO_TABLE(handlerTable);
#endif

#if defined(WITH_JIT)
#if 0
    LOGD("*DebugInterp - entrypoint is %d, tgt is 0x%x, %s\n",
         interpState->entryPoint,
         interpState->pc,
         interpState->method->name);
#endif
#if INTERP_TYPE == INTERP_DBG
    const ClassObject* callsiteClass = NULL;

#if defined(WITH_SELF_VERIFICATION)
    if (interpState->jitState != kJitSelfVerification) {
        interpState->self->shadowSpace->jitExitState = kSVSIdle;
    }
#endif

    /* Check to see if we've got a trace selection request. */
    if (
         /*
          * Only perform dvmJitCheckTraceRequest if the entry point is
          * EntryInstr and the jit state is either kJitTSelectRequest or
          * kJitTSelectRequestHot. If debugger/profiler happens to be attached,
          * dvmJitCheckTraceRequest will change the jitState to kJitDone but
          * but stay in the dbg interpreter.
          */
         (interpState->entryPoint == kInterpEntryInstr) &&
         (interpState->jitState == kJitTSelectRequest ||
          interpState->jitState == kJitTSelectRequestHot) &&
         dvmJitCheckTraceRequest(self, interpState)) {
        interpState->nextMode = INTERP_STD;
        //LOGD("Invalid trace request, exiting\n");
        return true;
    }
#endif /* INTERP_TYPE == INTERP_DBG */
#endif /* WITH_JIT */

    /* copy state in */
    curMethod = interpState->method;
    pc = interpState->pc;
    fp = interpState->fp;
    retval = interpState->retval;   /* only need for kInterpEntryReturn? */
#ifdef WITH_TAINT_TRACKING
    rtaint = interpState->rtaint;
#endif

    methodClassDex = curMethod->clazz->pDvmDex;

#ifdef WITH_IMPLICIT_TRACKING
    if (gDvm.taintTarget) {
        LOGVV("TaintLog: dvmInterpretStd gDvm.taintTarget = %d, \
systemTid=%d sysid=%d threadid=%d: entry(%s) %s.%s pc=0x%x fp=%p ep=%d\n",
             gDvm.taintTarget,
             self->systemTid,
             dvmGetSysThreadId(),
             self->threadId, (interpState->nextMode == INTERP_STD) ? "STD" : "DBG",
             curMethod->clazz->descriptor, curMethod->name, pc - curMethod->insns,
             fp, interpState->entryPoint);
    }
#endif /* WITH_IMPLICIT_TRACKING */

    /*
     * DEBUG: scramble this to ensure we're not relying on it.
     */
    methodToCall = (const Method*) -1;

#if INTERP_TYPE == INTERP_DBG
    if (debugIsMethodEntry) {
        ILOGD("|-- Now interpreting %s.%s", curMethod->clazz->descriptor,
                curMethod->name);
        DUMP_REGS(curMethod, interpState->fp, false);
    }
#endif

#ifdef WITH_IMPLICIT_TRACKING
    /* Global per interpreter (across interp calls) */
    bool implicitTaintMode;
    u4   implicitTaintTag;
    /* These are initialized here but moved to/from StackSaveArea later on invoke/returns */
    bool implicitStartingFrame;/* if we started tainting in the current method */
    u4   implicitBranchPdom; // ipd of last if-marker (used in subsequeny IF handler)
    u2   prevInst; // opcode of prev instruction, used for IF to see if it had a if-marker
    /* Actually initialize the values above */
    IMPLICIT_STOP_TAINTING("dvmInterpStd");
#endif /* WITH_IMPLICIT_TRACKING */

    switch (interpState->entryPoint) {
    case kInterpEntryInstr:
        /* just fall through to instruction loop or threaded kickstart */
        break;
    case kInterpEntryReturn:
        CHECK_JIT_VOID();
        goto returnFromMethod;
    case kInterpEntryThrow:
        goto exceptionThrown;
    default:
        dvmAbort();
    }

    /* LOGV("ASSERT VALI WAS HERE\n"); */
/* #ifdef WITH_IMPLICIT_TRACKING */
/*     TLOGV("[IFLOW] curMethod             = %s.%s", curMethod->clazz->descriptor, curMethod->name); */
/*     TLOGV("[IFLOW] implicitTaintMode     = %s", BOOL(implicitTaintMode)); */
/*     TLOGV("[IFLOW] implicitTaintTag      = %04x", implicitTaintTag); */
/*     TLOGV("[IFLOW] implicitStartingFrame = %s", BOOL(implicitStartingFrame)); */
/*     TLOGV("[IFLOW] implicitBranchPdom    = %04x", implicitBranchPdom); */
/* #endif */

#ifdef THREADED_INTERP
    FINISH(0);                  /* fetch and execute first instruction */
#else
    while (1) {
        CHECK_DEBUG_AND_PROF(); /* service debugger and profiling */
        CHECK_TRACKED_REFS();   /* check local reference tracking */

        /* fetch the next 16 bits from the instruction stream */
        inst = FETCH(0);

        switch (INST_INST(inst)) {
#endif

/*--- start of opcodes ---*/
