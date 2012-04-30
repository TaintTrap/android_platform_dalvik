HANDLE_OPCODE(OP_IF_MARKER)
{
  u4 pdom = FETCH(1);          /* high-order 16 bits */
  u4 id   = FETCH(2);          /* low-order 16 bits */
  TLOGV("|if-marker id/%04x pdom/%04x", id, pdom);

#ifdef WITH_IMPLICIT_TRACKING
  /* TLOGV("#define WITH_IMPLICIT_TRACKING enabled"); */
  // if we are not already in taint mode, then check branch taint
  TLOGV("[STATE] if-marker implicitTaintMode = %s", 
        BOOL(implicitTaintMode)
        );
  if (!implicitTaintMode)
    {
      implicitBranchPdom = pdom; 
      TLOGV("[STATE] if-marker implicitBranchPdom = %04x", implicitBranchPdom); 
    }
#endif

  FINISH(3);
}
OP_END
