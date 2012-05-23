HANDLE_OPCODE(OP_IPD_MARKER)
{
#ifdef WITH_IMPLICIT_TRACKING
  u4 pdom = FETCH(1);          /* high-order 16 bits */
  u4 id   = FETCH(2);          /* low-order 16 bits */
#ifdef IMPLICIT_DEBUG
  TLOGV("|ipd-marker id/%04x pdom/%04x", id, pdom);
  TLOGD("[IFLOW] [ipd-marker] implicitTaintMode = %s implicitStartingFrame = %s implicitBranchPdom = %04x",
        BOOL(implicitTaintMode), 
        BOOL(implicitStartingFrame),
        implicitBranchPdom
        );
#endif  /* IMPLICIT_DEBUG */

  /* if we are in taint mode, then we are looking for branch ipd so we check this ipd */
  if (implicitTaintMode &&
      implicitStartingFrame &&
      implicitBranchPdom == id)
    /* we found pdom for branch and can turn off tainting */
    {
      IMPLICIT_STOP_TAINTING("ipd-marker");

#ifdef IMPLICIT_DEBUG
      TLOGV("[IFLOW] [ipd-marker] FOUND! implicitTaintMode = %s implicitStartingFrame = %s implicitBranchPdom = %04x",
            BOOL(implicitTaintMode), 
            BOOL(implicitStartingFrame),
            implicitBranchPdom
            );
#endif /* IMPLICIT_DEBUG */
    }
#endif /* WITH_IMPLICIT_TRACKING */

  FINISH(3);
}
OP_END
