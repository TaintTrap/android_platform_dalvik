HANDLE_OPCODE(OP_IPD_MARKER)
{
  u4 pdom = FETCH(1);          /* high-order 16 bits */
  u4 id   = FETCH(2);          /* low-order 16 bits */
  TLOGV("|ipd-marker id/%04x pdom/%04x", id, pdom);
  /* printf("|ipd-marker works\n"); */

#ifdef WITH_IMPLICIT_TRACKING
  /* if we are in taint mode, then we are looking for branch ipd so we check this ipd */
  TLOGV("[STATE] ipd-marker implicitTaintMode = %s implicitStartingFrame = %s implicitBranchPdom = %04x", 
        BOOL(implicitTaintMode), 
        BOOL(implicitStartingFrame),
        implicitBranchPdom
        );

  if (implicitTaintMode && implicitStartingFrame && implicitBranchPdom == id)
    /* we found pdom for branch and can turn off tainting */
    {
      /* implicitTaintMode = false; /\* disable implicit taint *\/ */
      setImplicitTaintMode(&implicitTaintMode, false);
      implicitTaintTag  = TAINT_CLEAR;
      TLOGV("[STATE] [FOUND!] ipd-marker implicitTaintMode = %s implicitStartingFrame = %s implicitBranchPdom = %04x", 
            BOOL(implicitTaintMode), 
            BOOL(implicitStartingFrame),
            implicitBranchPdom
            );
    }
#endif

  FINISH(3);
}
OP_END
