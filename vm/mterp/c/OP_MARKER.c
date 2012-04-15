HANDLE_OPCODE(OP_MARKER)
{
    s4 id = FETCH(1);               /* low-order 16 bits */
    id |= ((s4) FETCH(2)) << 16;    /* high-order 16 bits */
    ILOGV("|marker 0x%08x", id);
    /* LOGE("MARKER opcode 0x%02x\n", INST_INST(inst)); */
    FINISH(3);
}
OP_END
