/*
 * um: test function rlc_entity_am_reestablishment
 *     and also the function clear_entity, case 'while (cur_rx != NULL)'
 */
TIME, 1,
    ENB_UM, 100000, 100000, 35, 5,
    UE_UM, 100000, 100000, 35, 5,
    RE_ESTABLISH,
TIME, 2,
    ENB_SDU, 0, 10,
    RE_ESTABLISH,
TIME, 3,
    ENB_SDU, 0, 10,
    ENB_SDU, 0, 10,
    ENB_SDU, 0, 10,
    ENB_SDU, 0, 10,
    ENB_PDU_SIZE, 14,
TIME, 5,
    UE_RECV_FAILS, 1,
TIME, 6,
    UE_RECV_FAILS, 0,
TIME, 10,
    RE_ESTABLISH,
TIME, 998,
    ENB_SDU, 0, 10,
    ENB_SDU, 0, 10,
    UE_RECV_FAILS, 1,
TIME, 999,
    UE_RECV_FAILS, 0,
TIME, -1
