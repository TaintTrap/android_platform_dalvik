HANDLE_OPCODE(OP_IPD_MARKER)
{
    s4 id  = FETCH(1);          /* low-order 16 bits */
    s4 ipd = FETCH(2);          /* high-order 16 bits */
    ILOGV("|ipd-marker id/%04x pdom/%04x", id, ipd);

    // TODO fetch only if tainting allowed
    if (implicitTaintAllowed)   /* we found pdom for branch and can turn off tainting */
        {
            assert(implicitTaintMode == true);
            implicitTaintMode = false; /* disable implicit taint */
            implicitTaintTag  = TAINT_CLEAR;
        }

    FINISH(3);
}
OP_END
