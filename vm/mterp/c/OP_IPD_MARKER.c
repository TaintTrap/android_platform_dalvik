HANDLE_OPCODE(OP_IPD_MARKER)
{
#ifdef WITH_IMPLICIT_TRACKING
  u4 id   = FETCH(2);          /* low-order 16 bits */

  /* if we are in taint mode, then we are looking for branch ipd so we check this ipd */
  if (implicitTaintMode &&
      implicitStartingFrame &&
      implicitBranchPdom == id)
    /* we found pdom for branch and can turn off tainting */
    {
#ifdef IMPLICIT_DEBUG
      u4 pdom = FETCH(1);          /* high-order 16 bits */
      TLOGV("[IFLOW] [ipd-marker] FOUND! id/%04x pdom/%04x implicitTaintMode = %s implicitStartingFrame = %s implicitBranchPdom = %04x",
            id, pdom,
            BOOL(implicitTaintMode), 
            BOOL(implicitStartingFrame),
            implicitBranchPdom
            );
#endif /* IMPLICIT_DEBUG */

      IMPLICIT_STOP_TAINTING("ipd-marker");
    }
#endif /* WITH_IMPLICIT_TRACKING */

  FINISH(3);
}
OP_END
