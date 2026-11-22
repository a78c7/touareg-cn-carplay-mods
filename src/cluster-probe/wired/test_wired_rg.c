/* Host unit test for clean-room wired iAP2 Route Guidance parser (cl_wired_rg.c). */
#include "cl_wired_rg.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

static size_t add_tlv(uint8_t *buf, size_t off, uint16_t tag, const void *val, size_t vlen) {
    put16(buf + off, (uint16_t)(4 + vlen));
    put16(buf + off + 2, tag);
    if (vlen && val) memcpy(buf + off + 4, val, vlen);
    return off + 4 + vlen;
}

static void finish_msg(uint8_t *buf, size_t len, uint16_t msgid) {
    buf[0] = 0x40; buf[1] = 0x40;
    put16(buf + 2, (uint16_t)len);
    put16(buf + 4, msgid);
}

int main(void) {
    /* 1) Test 0x1D01 Identify patching */
    uint8_t id_in[128], id_out[512];
    size_t o = 6;
    uint8_t sent[2] = { 0x1D, 0x01 };
    uint8_t recv[2] = { 0x1D, 0x02 };
    o = add_tlv(id_in, o, 0x0006, sent, sizeof(sent));
    o = add_tlv(id_in, o, 0x0007, recv, sizeof(recv));
    finish_msg(id_in, o, W_IAP2_IDENT_INFO);

    size_t nl = w_rg_patch_identify(id_in, o, 0x0010, id_out, sizeof(id_out));
    assert(nl > o);
    assert(id_out[0] == 0x40 && id_out[1] == 0x40);
    assert(((id_out[2] << 8) | id_out[3]) == (int)nl);
    /* Re-patching already patched identify must return 0 (idempotent guard) */
    uint8_t id_out2[512];
    assert(w_rg_patch_identify(id_out, nl, 0x0010, id_out2, sizeof(id_out2)) == 0);

    /* 2) Test 0x5200 Start patching */
    uint8_t st_in[32], st_out[64];
    uint8_t comp[2] = { 0x00, 0x10 };
    size_t so = add_tlv(st_in, 6, 0x0000, comp, 2);
    finish_msg(st_in, so, W_IAP2_RG_START);
    size_t snl = w_rg_patch_start(st_in, so, st_out, sizeof(st_out));
    assert(snl == so + 12);
    assert(((st_out[2] << 8) | st_out[3]) == (int)snl);

    /* 3) Test 0x5201 + 0x5202 + 0x5204 RX and /tmp state output */
    const char *tmp = "/tmp/test_wired_cp.state";
    unlink(tmp);
    w_rg_reset();
    w_rg_set_state_path(tmp);

    /* 0x5204 lane guidance index 2 with 2 lanes */
    uint8_t lg_msg[256];
    uint8_t l0[64], l1[64];
    size_t lo = 0;
    uint8_t pos0[2] = {0, 0}, st0[1] = {1}, ang0[4] = {0, 0, 0xFF, 0xA6}, hl0[2] = {0, 0};
    lo = add_tlv(l0, lo, 0x0000, pos0, 2);
    lo = add_tlv(l0, lo, 0x0001, st0, 1);
    lo = add_tlv(l0, lo, 0x0002, ang0, 4);
    lo = add_tlv(l0, lo, 0x0003, hl0, 2);
    size_t l1o = 0;
    uint8_t pos1[2] = {0, 1}, st1[1] = {2}, ang1[2] = {0, 90};
    l1o = add_tlv(l1, l1o, 0x0000, pos1, 2);
    l1o = add_tlv(l1, l1o, 0x0001, st1, 1);
    l1o = add_tlv(l1, l1o, 0x0002, ang1, 2);

    size_t p = 6;
    uint8_t lgi[2] = {0, 2};
    p = add_tlv(lg_msg, p, 0x0001, lgi, 2);
    p = add_tlv(lg_msg, p, 0x0002, l0, lo);
    p = add_tlv(lg_msg, p, 0x0002, l1, l1o);
    finish_msg(lg_msg, p, W_IAP2_RG_LANE);
    w_rg_handle_rx(W_IAP2_RG_LANE, lg_msg, p);

    /* 0x5202 maneuver index 5 with AMap "去往滨江大道" */
    uint8_t m_msg[256];
    p = 6;
    uint8_t midx[2] = {0, 5}, mtype[1] = {2}, mdist[4];
    put32(mdist, 260);
    const char *road = "\xe5\x8e\xbb\xe5\xbe\x80\xe6\xbb\xa8\xe6\xb1\x9f\xe5\xa4\xa7\xe9\x81\x93"; /* 去往滨江大道 */
    const char *desc = "Right\nRamp";
    p = add_tlv(m_msg, p, 0x0001, midx, 2);
    p = add_tlv(m_msg, p, 0x0002, desc, strlen(desc));
    p = add_tlv(m_msg, p, 0x0003, mtype, 1);
    p = add_tlv(m_msg, p, 0x0004, road, strlen(road));
    p = add_tlv(m_msg, p, 0x0005, mdist, 4);
    finish_msg(m_msg, p, W_IAP2_RG_MANEUVER);
    w_rg_handle_rx(W_IAP2_RG_MANEUVER, m_msg, p);

    /* 0x5201 update pointing maneuver_list=[5] and lane_guidance_index=2 */
    uint8_t u_msg[256];
    p = 6;
    uint8_t rs[1] = {1}, du[1] = {2}, dr[4], dm[4], tr[8], eta[8], ml[2] = {0, 5}, ls_show[1] = {1}, srg[1] = {1};
    put32(dr, 9500); put32(dm, 260); put64(tr, 960); put64(eta, 1700000000ULL);
    const char *src = "\xe9\xab\x98\xe5\xbe\xb7\xe5\x9c\xb0\xe5\x9b\xbe"; /* 高德地图 */
    p = add_tlv(u_msg, p, 0x0001, rs, 1);
    p = add_tlv(u_msg, p, 0x0005, eta, 8);
    p = add_tlv(u_msg, p, 0x0006, tr, 8);
    p = add_tlv(u_msg, p, 0x0007, dr, 4);
    p = add_tlv(u_msg, p, 0x0009, du, 1);
    p = add_tlv(u_msg, p, 0x000A, dm, 4);
    p = add_tlv(u_msg, p, 0x000C, du, 1);
    p = add_tlv(u_msg, p, 0x000D, ml, 2);
    p = add_tlv(u_msg, p, 0x0010, lgi, 2);
    p = add_tlv(u_msg, p, 0x0012, ls_show, 1);
    p = add_tlv(u_msg, p, 0x0013, src, strlen(src));
    p = add_tlv(u_msg, p, 0x0014, srg, 1);
    finish_msg(u_msg, p, W_IAP2_RG_UPDATE);
    w_rg_handle_rx(W_IAP2_RG_UPDATE, u_msg, p);

    FILE *f = fopen(tmp, "r");
    assert(f != NULL);
    char out[4096];
    size_t rn = fread(out, 1, sizeof(out) - 1, f);
    fclose(f);
    out[rn] = '\0';
    unlink(tmp);

    assert(strstr(out, "@cluster_cp\n") != NULL);
    assert(strstr(out, "session_active:b:true\n") != NULL);
    assert(strstr(out, "source_name:s:\xe9\xab\x98\xe5\xbe\xb7\xe5\x9c\xb0\xe5\x9b\xbe\n") != NULL);
    assert(strstr(out, "slot_0_road:s:\xe6\xbb\xa8\xe6\xb1\x9f\xe5\xa4\xa7\xe9\x81\x93\n") != NULL); /* 去往 stripped */
    assert(strstr(out, "slot_0_description:s:Right / Ramp\n") != NULL);
    assert(strstr(out, "slot_0_exit_angle:n:1000\n") != NULL);
    assert(strstr(out, "slot_count:n:1\n") != NULL);
    assert(strstr(out, "lane_active:b:true\n") != NULL);
    assert(strstr(out, "lane_count:n:2\n") != NULL);
    assert(strstr(out, "lane_0_angles:s:0,-90\n") != NULL);
    assert(strstr(out, "lane_1_dir:n:90\n") != NULL);

    printf("test_wired_rg: ALL PASSED\n");
    return 0;
}
