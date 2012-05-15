
/* undefine "magic" name remapping */
#undef retval
#undef pc
#undef fp
#undef curMethod
#undef methodClassDex
#undef self
#undef debugTrackedRefStart

#ifdef WITH_TAINT_TRACKING
#undef rtaint
#undef implicitStartingFrame
#undef implicitBranchPdomm
#undef implicitTaintMode
#undef implicitTaintTag
#undef prevInst
#endif
