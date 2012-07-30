HANDLE_OPCODE(OP_IF_MARKER)
{
#ifdef WITH_IMPLICIT_TRACKING
  u4 pdom = FETCH(1);          /* high-order 16 bits */
  // if we are not already in taint mode, then check branch taint
  if (!implicitTaintMode)
    {
      implicitBranchPdom = pdom; 

#ifdef IMPLICIT_DEBUG
      /* u4 id   = FETCH(2);          /\* low-order 16 bits *\/ */
      /* TLOGD("[IFLOW] [if-marker] id/%04x pdom/%04x implicitTaintMode = %s", id, pdom, BOOL(implicitTaintMode)); */
#endif  /* IMPLICIT_DEBUG */
    }
#endif

  FINISH(3);
}
OP_END
