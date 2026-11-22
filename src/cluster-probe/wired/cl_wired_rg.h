/*
 * cl_wired_rg.h — Clean-room iAP2 Route Guidance TLV parser, Identify patcher,
 * and /tmp/cluster_cp.state snapshot publisher for wired CarPlay (Cinemo stack).
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under PolyForm Noncommercial License 1.0.0 + LICENSE.ADDITIONAL-TERMS.md
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */
#ifndef CL_WIRED_RG_H
#define CL_WIRED_RG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* iAP2 Control Session message IDs for Identification & Route Guidance */
#define W_IAP2_IDENT_INFO          0x1D01
#define W_IAP2_IDENT_ACCEPTED      0x1D02
#define W_IAP2_IDENT_REJECTED      0x1D03
#define W_IAP2_RG_START            0x5200
#define W_IAP2_RG_UPDATE           0x5201
#define W_IAP2_RG_MANEUVER         0x5202
#define W_IAP2_RG_STOP             0x5203
#define W_IAP2_RG_LANE             0x5204
#define W_IAP2_LOC_INFO            0xFFFB

#define W_IDENT_TAG_SENT_MSGS      0x0006
#define W_IDENT_TAG_RECV_MSGS      0x0007
#define W_IDENT_TAG_RG_COMPONENT   0x001E

/* Global logging control (reads DIO_CLUSTER_LOG / CL_WIRED_LOG) */
void w_rg_log_init(void);
void w_rg_log_enable(int on);
void w_rg_log(const char *fmt, ...);
void w_rg_log_hex(const char *label, const uint8_t *buf, size_t len);

/* Set output state file path (defaults to "/tmp/cluster_cp.state"; overridable for host tests) */
void w_rg_set_state_path(const char *path);

/* Reset all cached route/maneuver/lane state (e.g., on new session or unit test) */
void w_rg_reset(void);

/* Patch an outgoing 0x1D01 IdentificationInformation message to advertise
 * Route Guidance (0x5200/0x5203 sent, 0x5201/0x5202/0x5204 recv) and append
 * RouteGuidanceDisplayComponent (0x001E) with the given component_id.
 * Returns new length on success, 0 on failure or if 0x001E already exists. */
size_t w_rg_patch_identify(const uint8_t *in, size_t in_len,
                           uint16_t component_id,
                           uint8_t *out, size_t out_cap);

/* Patch an outgoing 0x5200 StartRouteGuidanceUpdates message so empty flag TLVs
 * 0x0001 (sourceName), 0x0002 (sourceSupportsRG), 0x0003 (exitInfo/lane) exist.
 * Returns new length (may equal in_len), or 0 on overflow/error. */
size_t w_rg_patch_start(const uint8_t *in, size_t in_len,
                        uint8_t *out, size_t out_cap);

/* Feed an incoming full iAP2 control message (starting with 0x40 0x40 LL LL ID ID)
 * for 0x5201 / 0x5202 / 0x5204 and immediately publish the updated snapshot. */
void w_rg_handle_rx(uint16_t msgid, const uint8_t *msg, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CL_WIRED_RG_H */
