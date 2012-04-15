HANDLE_OPCODE(OP_IF_MARKER)
{
    s4 id  = FETCH(1);          /* low-order 16 bits */
    s4 ipd = FETCH(2);          /* high-order 16 bits */
    ILOGV("|if-marker id/%04x pdom/%04x", id, ipd);

    // TODO fetch only if tainting allowed
    if (implicitTaintAllowed)
        {
            assert(implicitTaintMode == false);
            implicitTaintMode = true;
            implicitTaintTag  = implicitBranchTag; /* enable implicit taint propagation */
        }

    FINISH(3);
}
OP_END
