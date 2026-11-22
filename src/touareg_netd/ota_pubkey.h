/*
 * ota_pubkey.h — Official Ed25519 public key (32 bytes) for verifying offline OTA packages.
 *
 * The corresponding private signing key is kept strictly outside this repository
 * (GitHub Actions Secret: OTA_SIGNING_KEY) and is NEVER committed to git.
 */
#ifndef TOUAREG_OTA_PUBKEY_H
#define TOUAREG_OTA_PUBKEY_H

#include <stdint.h>

/* Official release signing public key (hex: fa295b4f1c20cb6b5e7bfad327dcf4ba65e7f2fca09e84c3dc10d878a77cffdb) */
static const uint8_t OTA_OFFICIAL_PUBKEY[32] = {
    0xfa, 0x29, 0x5b, 0x4f, 0x1c, 0x20, 0xcb, 0x6b,
    0x5e, 0x7b, 0xfa, 0xd3, 0x27, 0xdc, 0xf4, 0xba,
    0x65, 0xe7, 0xf2, 0xfc, 0xa0, 0x9e, 0x84, 0xc3,
    0xdc, 0x10, 0xd8, 0x78, 0xa7, 0x7c, 0xff, 0xdb
};

#endif /* TOUAREG_OTA_PUBKEY_H */
