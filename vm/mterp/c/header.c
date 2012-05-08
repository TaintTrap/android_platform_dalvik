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

// VALI
//#define LOG_NDEBUG 0
//#define IMPLICIT_DEBUG
//#define WITH_IMPLICIT_TRACKING

/* common includes */
#include "Dalvik.h"
#include "interp/InterpDefs.h"
#include "mterp/Mterp.h"
#include <math.h>                   // needed for fmod, fmodf
#include "mterp/common/FindInterface.h"

/*
 * Configuration defines.  These affect the C implementations, i.e. the
 * portable interpreter(s) and C stubs.
 *
 * Some defines are controlled by the Makefile, e.g.:
 *   WITH_INSTR_CHECKS
 *   WITH_TRACKREF_CHECKS
 *   EASY_GDB
 *   NDEBUG
 *
 * If THREADED_INTERP is not defined, we use a classic "while true / switch"
 * interpreter.  If it is defined, then the tail end of each instruction
 * handler fetches the next instruction and jumps directly to the handler.
 * This increases the size of the "Std" interpreter by about 10%, but
 * provides a speedup of about the same magnitude.
 *
 * There's a "hybrid" approach that uses a goto table instead of a switch
 * statement, avoiding the "is the opcode in range" tests required for switch.
 * The performance is close to the threaded version, and without the 10%
 * size increase, but the benchmark results are off enough that it's not
 * worth adding as a third option.
 */
#define THREADED_INTERP             /* threaded vs. while-loop interpreter */

#ifdef WITH_INSTR_CHECKS            /* instruction-level paranoia (slow!) */
# define CHECK_BRANCH_OFFSETS
# define CHECK_REGISTER_INDICES
#endif

/*
 * ARM EABI requires 64-bit alignment for access to 64-bit data types.  We
 * can't just use pointers to copy 64-bit values out of our interpreted
 * register set, because gcc will generate ldrd/strd.
 *
 * The __UNION version copies data in and out of a union.  The __MEMCPY
 * version uses a memcpy() call to do the transfer; gcc is smart enough to
 * not actually call memcpy().  The __UNION version is very bad on ARM;
 * it only uses one more instruction than __MEMCPY, but for some reason
 * gcc thinks it needs separate storage for every instance of the union.
 * On top of that, it feels the need to zero them out at the start of the
 * method.  Net result is we zero out ~700 bytes of stack space at the top
 * of the interpreter using ARM STM instructions.
 */
#if defined(__ARM_EABI__)
//# define NO_UNALIGN_64__UNION
# define NO_UNALIGN_64__MEMCPY
#endif

//#define LOG_INSTR                   /* verbose debugging */
/* set and adjust ANDROID_LOG_TAGS='*:i jdwp:i dalvikvm:i dalvikvmi:i' */

/*
 * Keep a tally of accesses to fields.  Currently only works if full DEX
 * optimization is disabled.
 */
#ifdef PROFILE_FIELD_ACCESS
# define UPDATE_FIELD_GET(_field) { (_field)->gets++; }
# define UPDATE_FIELD_PUT(_field) { (_field)->puts++; }
#else
# define UPDATE_FIELD_GET(_field) ((void)0)
# define UPDATE_FIELD_PUT(_field) ((void)0)
#endif

/*
 * Export another copy of the PC on every instruction; this is largely
 * redundant with EXPORT_PC and the debugger code.  This value can be
 * compared against what we have stored on the stack with EXPORT_PC to
 * help ensure that we aren't missing any export calls.
 */
#if WITH_EXTRA_GC_CHECKS > 1
# define EXPORT_EXTRA_PC() (self->currentPc2 = pc)
#else
# define EXPORT_EXTRA_PC()
#endif

/*
 * Adjust the program counter.  "_offset" is a signed int, in 16-bit units.
 *
 * Assumes the existence of "const u2* pc" and "const u2* curMethod->insns".
 *
 * We don't advance the program counter until we finish an instruction or
 * branch, because we do want to have to unroll the PC if there's an
 * exception.
 */
#ifdef CHECK_BRANCH_OFFSETS
# define ADJUST_PC(_offset) do {                                            \
        int myoff = _offset;        /* deref only once */                   \
        if (pc + myoff < curMethod->insns ||                                \
            pc + myoff >= curMethod->insns + dvmGetMethodInsnsSize(curMethod)) \
        {                                                                   \
            char* desc;                                                     \
            desc = dexProtoCopyMethodDescriptor(&curMethod->prototype);     \
            LOGE("Invalid branch %d at 0x%04x in %s.%s %s\n",               \
                myoff, (int) (pc - curMethod->insns),                       \
                curMethod->clazz->descriptor, curMethod->name, desc);       \
            free(desc);                                                     \
            dvmAbort();                                                     \
        }                                                                   \
        pc += myoff;                                                        \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#else
# define ADJUST_PC(_offset) do {                                            \
        pc += _offset;                                                      \
        EXPORT_EXTRA_PC();                                                  \
    } while (false)
#endif

/*
 * If enabled, log instructions as we execute them.
 */
#undef LOG_INSTR
#ifdef LOG_INSTR
# define ILOGD(...) ILOG(LOG_DEBUG, __VA_ARGS__)
# define ILOGV(...) ILOG(LOG_VERBOSE, __VA_ARGS__)
# define ILOG(_level, ...) do {                                             \
        char debugStrBuf[128];                                              \
        snprintf(debugStrBuf, sizeof(debugStrBuf), __VA_ARGS__);            \
        if (curMethod != NULL)                                                 \
            LOG(_level, LOG_TAG"i", "%-2d|%04x%s\n",                        \
                self->threadId, (int)(pc - curMethod->insns), debugStrBuf); \
        else                                                                \
            LOG(_level, LOG_TAG"i", "%-2d|####%s\n",                        \
                self->threadId, debugStrBuf);                               \
    } while(false)
void dvmDumpRegs(const Method* method, const u4* framePtr, bool inOnly);
# define DUMP_REGS(_meth, _frame, _inOnly) dvmDumpRegs(_meth, _frame, _inOnly)
static const char kSpacing[] = "            ";
#else
# define ILOGD(...) ((void)0)
# define ILOGV(...) ((void)0)
# define DUMP_REGS(_meth, _frame, _inOnly) ((void)0)
#endif

/*
 * If enabled, log taint propagation
 */
#ifdef WITH_TAINT_TRACKING
# define TLOGD(...) TLOG(LOG_DEBUG, __VA_ARGS__)
# define TLOGV(...) TLOG(LOG_VERBOSE, __VA_ARGS__)
# define TLOGW(...) TLOG(LOG_WARN, __VA_ARGS__)
# define TLOGE(...) TLOG(LOG_ERROR, __VA_ARGS__)
# define TLOG(_level, ...) do {                                             \
        char debugStrBuf[128];                                              \
        snprintf(debugStrBuf, sizeof(debugStrBuf), __VA_ARGS__);            \
        if (curMethod != NULL)                                              \
            LOG(_level, LOG_TAG"t", "%-2d|%04x|%s.%s:%s\n",                    \
                self->threadId, (int)(pc - curMethod->insns), curMethod->clazz->descriptor, curMethod->name, debugStrBuf); \
        else                                                                \
            LOG(_level, LOG_TAG"t", "%-2d|####%s\n",                        \
                self->threadId, debugStrBuf);                               \
    } while(false)
/* Helper function for printing boolean values */
# define BOOL(_val) ((_val) ? "true" : "false")
#else
# define TLOGD(...) ((void)0)
# define TLOGV(...) ((void)0)
# define TLOGW(...) ((void)0)
# define TLOGE(...) ((void)0)
# define BOOL(_val) ((void)0)
#endif

/* get a long from an array of u4 */
static inline s8 getLongFromArray(const u4* ptr, int idx)
{
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.ll;
#elif defined(NO_UNALIGN_64__MEMCPY)
    s8 val;
    memcpy(&val, &ptr[idx], 8);
    return val;
#else
    return *((s8*) &ptr[idx]);
#endif
}

#ifdef WITH_TAINT_TRACKING
/* get a long from an array of u4 */
static inline s8 getLongFromArrayTaint(const u4* ptr, int idx)
{
    /* Need to use the "union" version for taint tracking */
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[2];
    return conv.ll;
}
#endif

/* store a long into an array of u4 */
static inline void putLongToArray(u4* ptr, int idx, s8 val)
{
#if defined(NO_UNALIGN_64__UNION)
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.ll = val;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#elif defined(NO_UNALIGN_64__MEMCPY)
    memcpy(&ptr[idx], &val, 8);
#else
    *((s8*) &ptr[idx]) = val;
#endif
}

#ifdef WITH_TAINT_TRACKING
/* store a long into an array of u4 */
static inline void putLongToArrayTaint(u4* ptr, int idx, s8 val)
{
    /* Need to use the "union" version for taint tracking */
    union { s8 ll; u4 parts[2]; } conv;

    ptr += idx;
    conv.ll = val;
    ptr[0] = conv.parts[0];
    ptr[2] = conv.parts[1];
}
#endif

/* get a double from an array of u4 */
static inline double getDoubleFromArray(const u4* ptr, int idx)
{
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[1];
    return conv.d;
#elif defined(NO_UNALIGN_64__MEMCPY)
    double dval;
    memcpy(&dval, &ptr[idx], 8);
    return dval;
#else
    return *((double*) &ptr[idx]);
#endif
}

#ifdef WITH_TAINT_TRACKING
/* get a double from an array of u4 */
static inline double getDoubleFromArrayTaint(const u4* ptr, int idx)
{
    /* Need to use the "union" version for taint tracking */
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.parts[0] = ptr[0];
    conv.parts[1] = ptr[2];
    return conv.d;
}
#endif

/* store a double into an array of u4 */
static inline void putDoubleToArray(u4* ptr, int idx, double dval)
{
#if defined(NO_UNALIGN_64__UNION)
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.d = dval;
    ptr[0] = conv.parts[0];
    ptr[1] = conv.parts[1];
#elif defined(NO_UNALIGN_64__MEMCPY)
    memcpy(&ptr[idx], &dval, 8);
#else
    *((double*) &ptr[idx]) = dval;
#endif
}

#ifdef WITH_TAINT_TRACKING
/* store a double into an array of u4 */
static inline void putDoubleToArrayTaint(u4* ptr, int idx, double dval)
{
    /* Need to use the "union" version for taint tracking */
    union { double d; u4 parts[2]; } conv;

    ptr += idx;
    conv.d = dval;
    ptr[0] = conv.parts[0];
    ptr[2] = conv.parts[1];
}
#endif

#ifdef WITH_TAINT_TRACKING
static inline s8 getRegister(const u4* fp, int idx)
{
#ifdef IMPLICIT_DEBUG
    printf("+++ GET_REGISTER v%-2d : fp[idx<<1] : 0x%04x\n", idx, fp[idx]);
#endif
    return fp[idx];
}

static inline s8 getRegisterTaint(const u4* fp, int idx)
{
    if (fp[idx] != TAINT_CLEAR) {
        printf("+++ GET_REGISTER_TAINT v%-2d : fp[(idx<<1)+1] : 0x%04x\n", idx, fp[idx]);
    }
    return fp[idx];
}
#endif

#ifdef WITH_IMPLICIT_TRACKING
static inline void setImplicitTaintMode(bool *taintMode, bool mode) {
  *taintMode = mode;
}
#endif

/*
 * If enabled, validate the register number on every access.  Otherwise,
 * just do an array access.
 *
 * Assumes the existence of "u4* fp".
 *
 * "_idx" may be referenced more than once.
 */
#ifdef WITH_TAINT_TRACKING
/* -- Begin Taint Tracking version ------------------------------- */
/* Taint tags are interleaved between registers. All indexes must
 * be multiplied by 2 (i.e., left bit shift by 1) */
#ifdef CHECK_REGISTER_INDICES
# define GET_REGISTER(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)<<1]) : (assert(!"bad reg"),1969) )
# define SET_REGISTER(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)<<1] = (u4)(_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object *)GET_REGISTER(_idx))
# define SET_REGISTER_AS_OBJECT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_INT(_idx) ((s4) GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getLongFromArrayTaint(fp, ((_idx)<<1)) : (assert(!"bad reg"),1969) )
# define SET_REGISTER_WIDE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        putLongToArrayTaint(fp, ((_idx)<<1), (_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_FLOAT(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)<<1])) : (assert(!"bad reg"),1969.0f) )
# define SET_REGISTER_FLOAT(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)<<1]) = (_val)) : (assert(!"bad reg"),1969.0f) )
# define GET_REGISTER_DOUBLE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getDoubleFromArrayTaint(fp, ((_idx)<<1)) : (assert(!"bad reg"),1969.0) )
# define SET_REGISTER_DOUBLE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        putDoubleToArrayTaint(fp, ((_idx)<<1), (_val)) : (assert(!"bad reg"),1969.0) )
#else
# define GET_REGISTER(_idx)                 (fp[(_idx)<<1])
/* # define GET_REGISTER(_idx)                 (getRegister(fp, ((_idx)<<1))) */
# define SET_REGISTER(_idx, _val)           (fp[(_idx)<<1] = (_val))
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object*) fp[(_idx)<<1])
# define SET_REGISTER_AS_OBJECT(_idx, _val) (fp[(_idx)<<1] = (u4)(_val))
# define GET_REGISTER_INT(_idx)             ((s4)GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val)       SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx)            getLongFromArrayTaint(fp, ((_idx)<<1))
# define SET_REGISTER_WIDE(_idx, _val)      putLongToArrayTaint(fp, ((_idx)<<1), (_val))
# define GET_REGISTER_FLOAT(_idx)           (*((float*) &fp[(_idx)<<1]))
# define SET_REGISTER_FLOAT(_idx, _val)     (*((float*) &fp[(_idx)<<1]) = (_val))
# define GET_REGISTER_DOUBLE(_idx)          getDoubleFromArrayTaint(fp, ((_idx)<<1))
# define SET_REGISTER_DOUBLE(_idx, _val)    putDoubleToArrayTaint(fp, ((_idx)<<1), (_val))
#endif
/* -- End Taint Tracking version ---------------------------------- */
#else /* no taint tracking */
#ifdef CHECK_REGISTER_INDICES
# define GET_REGISTER(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)]) : (assert(!"bad reg"),1969) )
# define SET_REGISTER(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (fp[(_idx)] = (u4)(_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object *)GET_REGISTER(_idx))
# define SET_REGISTER_AS_OBJECT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_INT(_idx) ((s4) GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val) SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getLongFromArray(fp, (_idx)) : (assert(!"bad reg"),1969) )
# define SET_REGISTER_WIDE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        putLongToArray(fp, (_idx), (_val)) : (assert(!"bad reg"),1969) )
# define GET_REGISTER_FLOAT(_idx) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)])) : (assert(!"bad reg"),1969.0f) )
# define SET_REGISTER_FLOAT(_idx, _val) \
    ( (_idx) < curMethod->registersSize ? \
        (*((float*) &fp[(_idx)]) = (_val)) : (assert(!"bad reg"),1969.0f) )
# define GET_REGISTER_DOUBLE(_idx) \
    ( (_idx) < curMethod->registersSize-1 ? \
        getDoubleFromArray(fp, (_idx)) : (assert(!"bad reg"),1969.0) )
# define SET_REGISTER_DOUBLE(_idx, _val) \
    ( (_idx) < curMethod->registersSize-1 ? \
        putDoubleToArray(fp, (_idx), (_val)) : (assert(!"bad reg"),1969.0) )
#else
# define GET_REGISTER(_idx)                 (fp[(_idx)])
# define SET_REGISTER(_idx, _val)           (fp[(_idx)] = (_val))
# define GET_REGISTER_AS_OBJECT(_idx)       ((Object*) fp[(_idx)])
# define SET_REGISTER_AS_OBJECT(_idx, _val) (fp[(_idx)] = (u4)(_val))
# define GET_REGISTER_INT(_idx)             ((s4)GET_REGISTER(_idx))
# define SET_REGISTER_INT(_idx, _val)       SET_REGISTER(_idx, (s4)_val)
# define GET_REGISTER_WIDE(_idx)            getLongFromArray(fp, (_idx))
# define SET_REGISTER_WIDE(_idx, _val)      putLongToArray(fp, (_idx), (_val))
# define GET_REGISTER_FLOAT(_idx)           (*((float*) &fp[(_idx)]))
# define SET_REGISTER_FLOAT(_idx, _val)     (*((float*) &fp[(_idx)]) = (_val))
# define GET_REGISTER_DOUBLE(_idx)          getDoubleFromArray(fp, (_idx))
# define SET_REGISTER_DOUBLE(_idx, _val)    putDoubleToArray(fp, (_idx), (_val))
#endif
#endif /* end no taint tracking */

#ifdef WITH_TAINT_TRACKING
/* Core get and set macros */
# define GET_REGISTER_TAINT(_idx)	     (fp[((_idx)<<1)+1])
#ifdef WITH_IMPLICIT_TRACKING
# define SET_REGISTER_TAINT(_idx, _val)	     (fp[((_idx)<<1)+1] = (u4)(_val | implicitTaintTag))
# define SET_REGISTER_TAINT_WIDE(_idx, _val) (fp[((_idx)<<1)+1] = \
	                                      fp[((_idx)<<1)+3] = (u4)(_val | implicitTaintTag))
# define SET_ARRAY_TAINT(_arr, _val)	      ((_arr)->taint.tag = (u4)(_val | implicitTaintTag))
# define SET_RETURN_TAINT(_val)		      (rtaint.tag = (u4)(_val | implicitTaintTag))
#else
# define SET_REGISTER_TAINT(_idx, _val)	     (fp[((_idx)<<1)+1] = (u4)(_val))
# define SET_REGISTER_TAINT_WIDE(_idx, _val) (fp[((_idx)<<1)+1] = \
	                                      fp[((_idx)<<1)+3] = (u4)(_val))
# define SET_ARRAY_TAINT(_arr, _val)	      ((_arr)->taint.tag = (u4)(_val))
# define SET_RETURN_TAINT(_val)		      (rtaint.tag = (u4)(_val))
#endif  /* WITH_IMPLICIT_TRACKING */
# define GET_REGISTER_TAINT_WIDE(_idx)       (fp[((_idx)<<1)+1])
/* Alternate interfaces to help dereference register width */
# define GET_REGISTER_TAINT_INT(_idx)	          GET_REGISTER_TAINT(_idx)
# define SET_REGISTER_TAINT_INT(_idx, _val)       SET_REGISTER_TAINT(_idx, _val)
# define GET_REGISTER_TAINT_FLOAT(_idx)	          GET_REGISTER_TAINT(_idx)
# define SET_REGISTER_TAINT_FLOAT(_idx, _val)     SET_REGISTER_TAINT(_idx, _val)
# define GET_REGISTER_TAINT_DOUBLE(_idx)          GET_REGISTER_TAINT_WIDE(_idx)
# define SET_REGISTER_TAINT_DOUBLE(_idx, _val)    SET_REGISTER_TAINT_WIDE(_idx, _val)
# define GET_REGISTER_TAINT_AS_OBJECT(_idx)       GET_REGISTER_TAINT(_idx)
# define SET_REGISTER_TAINT_AS_OBJECT(_idx, _val) SET_REGISTER_TAINT(_idx, _val)

/* Object Taint interface */
# define GET_ARRAY_TAINT(_arr)		      ((_arr)->taint.tag)

/* Return value taint (assumes rtaint variable is in scope */
# define GET_RETURN_TAINT()		      (rtaint.tag)
#else
# define GET_REGISTER_TAINT(_idx)		    ((void)0)
# define SET_REGISTER_TAINT(_idx, _val)		    ((void)0)
# define GET_REGISTER_TAINT_WIDE(_idx)		    ((void)0)
# define SET_REGISTER_TAINT_WIDE(_idx, _val)	    ((void)0)
# define GET_REGISTER_TAINT_INT(_idx)		    ((void)0)
# define SET_REGISTER_TAINT_INT(_idx, _val)	    ((void)0)
# define GET_REGISTER_TAINT_DOUBLE(_idx)	    ((void)0)
# define SET_REGISTER_TAINT_DOUBLE(_idx, _val)	    ((void)0)
# define GET_REGISTER_TAINT_AS_OBJECT(_idx)	    ((void)0)
# define SET_REGISTER_TAINT_AS_OBJECT(_idx, _val)   ((void)0)
# define GET_ARRAY_TAINT(_field)                    ((void)0)
# define SET_ARRAY_TAINT(_field, _val)              ((void)0)
# define GET_RETURN_TAINT()			    ((void)0)
# define SET_RETURN_TAINT(_val)			    ((void)0)
#endif

#ifdef WITH_IMPLICIT_TRACKING
# define IMPLICIT_BRANCH_TAINT(_val) if ((!implicitTaintMode) && ((u4)(_val) != TAINT_CLEAR)) { \
    if (prevInst == OP_IF_MARKER) {                                     \
      IMPLICIT_START_TAINTING(_val);                                    \
      TLOGV("[STATE] IF-branch implicitTaintMode = %s implicitTaintTag = %04x implicitStartingFrame = %s implicitBranchPdom = %04x", \
            BOOL(implicitTaintMode),                                    \
            implicitTaintTag,                                           \
            BOOL(implicitStartingFrame),                                \
            implicitBranchPdom                                          \
            );                                                          \
    } else {                                                            \
      TLOGE("[ERROR] IF-branch with Taint Tag = %04x and missing if-marker. Library not smalified?", (u4)(_val)); \
    }                                                                   \
  }
# define IMPLICIT_STOP_TAINTING(_reason)                                \
  setImplicitTaintMode(&implicitTaintMode, false);                      \
  implicitTaintTag      = TAINT_CLEAR;                                  \
  implicitStartingFrame = false;                                        \
  implicitBranchPdom    = 0;                                            \
  prevInst              = OP_NOP;
# define IMPLICIT_START_TAINTING(_val)                                  \
  setImplicitTaintMode(&implicitTaintMode, true);                       \
  implicitTaintTag = (u4)(_val);                                        \
  implicitStartingFrame = true;
#else
# define IMPLICIT_BRANCH_TAINT() ((void)0)
# define IMPLICIT_STOP_TAINTING(_reason) ((void)0)
#endif /* WITH_IMPLICIT_TRACKING */

/* # define GET_REGISTER_TAINT(_idx)      (getRegisterTaint(fp, ((_idx)<<1)+1)) */
/* # define GET_REGISTER_TAINT(_idx)      (fp[((_idx)<<1)+1]);             \ */
/*                                         TLOGV("+++ GET_REGISTER v%-2d : fp[(idx<<1)+1] : 0x%08x\n", (_idx), fp[(_idx)<<1]); ) */
/* # define SET_REGISTER_TAINT(_idx, _val)	     fp[((_idx)<<1)+1] = (u4)(_val); \ */
/*                                               TLOGV("[IFLOW-DEBUG] implicitTaintMode = %s", BOOL(implicitTaintMode)); */
/* # define SET_REGISTER_TAINT(_idx, _val)      (fp[((_idx)<<1)+1] = (u4)(_val | implicitTaintTag)); \ */
/*     TLOGV(" +++ SET_REGISTER_TAINT v%-2d : fp[(%d<<1)+1] = 0x%04x implicit = 0x%04x", (_idx), (_idx), (u4)(_val | implicitTaintTag), implicitTaintTag); */
/* # define SET_REGISTER_TAINT_WIDE(_idx, _val) (fp[((_idx)<<1)+1] = \ */
/*                                               fp[((_idx)<<1)+3] = (u4)(_val | implicitTaintTag)); \ */
/*     TLOGV(" +++ SET_REGISTER_TAINT_WIDE v%-2d : fp[(%d<<1)+1] = fp[(%d<<1)+3] = 0x%04x implcit = 0x%04x", (_idx), (_idx), (_idx), (u4)(_val | implicitTaintTag), implicitTaintTag); */
/* # define SET_ARRAY_TAINT(_arr, _val)        ((_arr)->taint.tag = (u4)(_val | implicitTaintTag)); \ */
/*     TLOGV(" +++ SET_ARRAY_TAINT val : 0x%04x implicit : 0x%04x", (u4)(_val | implicitTaintTag), implicitTaintTag); */
/* # define SET_RETURN_TAINT(_val)         (rtaint.tag = (u4)(_val));  \ */
/*     TLOGV(" +++ SET_RETURN_TAINT val : 0x%04x", (u4)(_val)); */

/*
 * Get 16 bits from the specified offset of the program counter.  We always
 * want to load 16 bits at a time from the instruction stream -- it's more
 * efficient than 8 and won't have the alignment problems that 32 might.
 *
 * Assumes existence of "const u2* pc".
 */
#define FETCH(_offset)     (pc[(_offset)])

/*
 * Extract instruction byte from 16-bit fetch (_inst is a u2).
 */
#define INST_INST(_inst)    ((_inst) & 0xff)

/*
 * Replace the opcode (used when handling breakpoints).  _opcode is a u1.
 */
#define INST_REPLACE_OP(_inst, _opcode) (((_inst) & 0xff00) | _opcode)

/*
 * Extract the "vA, vB" 4-bit registers from the instruction word (_inst is u2).
 */
#define INST_A(_inst)       (((_inst) >> 8) & 0x0f)
#define INST_B(_inst)       ((_inst) >> 12)

/*
 * Get the 8-bit "vAA" 8-bit register index from the instruction word.
 * (_inst is u2)
 */
#define INST_AA(_inst)      ((_inst) >> 8)

/*
 * The current PC must be available to Throwable constructors, e.g.
 * those created by dvmThrowException(), so that the exception stack
 * trace can be generated correctly.  If we don't do this, the offset
 * within the current method won't be shown correctly.  See the notes
 * in Exception.c.
 *
 * This is also used to determine the address for precise GC.
 *
 * Assumes existence of "u4* fp" and "const u2* pc".
 */
#define EXPORT_PC()         (SAVEAREA_FROM_FP(fp)->xtra.currentPc = pc)

/*
 * Determine if we need to switch to a different interpreter.  "_current"
 * is either INTERP_STD or INTERP_DBG.  It should be fixed for a given
 * interpreter generation file, which should remove the outer conditional
 * from the following.
 *
 * If we're building without debug and profiling support, we never switch.
 */
#if defined(WITH_JIT)
# define NEED_INTERP_SWITCH(_current) (                                     \
    (_current == INTERP_STD) ?                                              \
        dvmJitDebuggerOrProfilerActive() : !dvmJitDebuggerOrProfilerActive() )
#else
# define NEED_INTERP_SWITCH(_current) (                                     \
    (_current == INTERP_STD) ?                                              \
        dvmDebuggerOrProfilerActive() : !dvmDebuggerOrProfilerActive() )
#endif

/*
 * Check to see if "obj" is NULL.  If so, throw an exception.  Assumes the
 * pc has already been exported to the stack.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler calls into
 * something that could throw an exception (so we have already called
 * EXPORT_PC at the top).
 */
static inline bool checkForNull(Object* obj)
{
    if (obj == NULL) {
        dvmThrowException("Ljava/lang/NullPointerException;", NULL);
        return false;
    }
#ifdef WITH_EXTRA_OBJECT_VALIDATION
    if (!dvmIsValidObject(obj)) {
        LOGE("Invalid object %p\n", obj);
        dvmAbort();
    }
#endif
#ifndef NDEBUG
    if (obj->clazz == NULL || ((u4) obj->clazz) <= 65536) {
        /* probable heap corruption */
        LOGE("Invalid object class %p (in %p)\n", obj->clazz, obj);
        dvmAbort();
    }
#endif
    return true;
}

/*
 * Check to see if "obj" is NULL.  If so, export the PC into the stack
 * frame and throw an exception.
 *
 * Perform additional checks on debug builds.
 *
 * Use this to check for NULL when the instruction handler doesn't do
 * anything else that can throw an exception.
 */
static inline bool checkForNullExportPC(Object* obj, u4* fp, const u2* pc)
{
    if (obj == NULL) {
        EXPORT_PC();
        dvmThrowException("Ljava/lang/NullPointerException;", NULL);
        return false;
    }
#ifdef WITH_EXTRA_OBJECT_VALIDATION
    if (!dvmIsValidObject(obj)) {
        LOGE("Invalid object %p\n", obj);
        dvmAbort();
    }
#endif
#ifndef NDEBUG
    if (obj->clazz == NULL || ((u4) obj->clazz) <= 65536) {
        /* probable heap corruption */
        LOGE("Invalid object class %p (in %p)\n", obj->clazz, obj);
        dvmAbort();
    }
#endif
    return true;
}
