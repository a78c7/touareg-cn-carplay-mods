/*
 * vhpatch - patch / unpatch entries of a zip file in place on the head unit (MH2p, QNX 6.6), from a byte patch.
 *
 * Full-screen CarPlay changes 38 bytes in two compiled UI files inside the car's own
 * /mnt/app/eso/hmi/lsd/Resources/skin3/viewhandler.zip. Instead of shipping a modified copy of that VW file,
 * the USB package carries only the patch (offset, stock byte, new byte) and this program applies it to the
 * file already on the car - or reverts it. Every entry is checked by size and CRC-32 before and after, and
 * every stock/new byte is checked, so nothing is written unless the file is exactly the expected version.
 *
 *   vhpatch check  <zip> <patch>          prints "stock", "patched" or "unknown"   (exit 0, 0, 3)
 *   vhpatch apply  <zip> <patch> <out>    stock   -> patched copy in <out>          (exit 0 on success)
 *   vhpatch revert <zip> <patch> <out>    patched -> stock copy in <out>
 *
 * The output zip keeps every other entry byte for byte; the patched entries are re-deflated.
 * zlib is compiled in (no dependency on the unit's libz). Licence: GPL-3.0 (this repository).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"
#ifdef __QNXNTO__
#include "aeabi_shim.h"   /* QNX 6.6 libc lacks the __aeabi_mem* helpers clang emits (also for the zlib objects) */
#endif

#define MAXP 8      /* patched entries */
#define MAXB 256    /* bytes per entry */

typedef struct {
    char name[128];
    uint32_t size, crc_stock, crc_patched;
    int nb;
    uint32_t off[MAXB];
    uint8_t from[MAXB], to[MAXB];
} pentry;

static pentry P[MAXP];
static int NP;

static int die(const char *m) { fprintf(stderr, "vhpatch: %s\n", m); return 2; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static int load_patch(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return die("cannot open patch file");
    char l[512];
    while (fgets(l, sizeof l, f)) {
        if (l[0] == '#' || l[0] == '\n' || l[0] == '\r') continue;
        if (!strncmp(l, "entry ", 6)) {
            if (NP >= MAXP) { fclose(f); return die("too many entries in patch"); }
            pentry *e = &P[NP++];
            memset(e, 0, sizeof *e);
            if (sscanf(l + 6, "%127s %u %x %x", e->name, &e->size, &e->crc_stock, &e->crc_patched) != 4) { fclose(f); return die("bad entry line"); }
        } else {
            unsigned o, a, b;
            if (!NP || sscanf(l, "%u %x %x", &o, &a, &b) != 3) { fclose(f); return die("bad byte line"); }
            pentry *e = &P[NP - 1];
            if (e->nb >= MAXB || o >= e->size) { fclose(f); return die("bad byte offset"); }
            e->off[e->nb] = o; e->from[e->nb] = (uint8_t)a; e->to[e->nb] = (uint8_t)b; e->nb++;
        }
    }
    fclose(f);
    return NP ? 0 : die("empty patch");
}

static uint8_t *load_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len > 0 ? *len : 1);
    if (b && fread(b, 1, *len, f) != (size_t)*len) { free(b); b = NULL; }
    fclose(f);
    return b;
}

/* central directory */
typedef struct { const uint8_t *cd; uint32_t cd_off, cd_size; uint16_t n; long eocd; } zipinfo;

static int zip_open(const uint8_t *z, long len, zipinfo *zi) {
    long lo = len - 22 - 65535; if (lo < 0) lo = 0;
    for (long i = len - 22; i >= lo; i--) {
        if (rd32(z + i) == 0x06054b50) {
            zi->eocd = i; zi->n = rd16(z + i + 10); zi->cd_size = rd32(z + i + 12); zi->cd_off = rd32(z + i + 16);
            if ((long)zi->cd_off + zi->cd_size > i) return -1;
            zi->cd = z + zi->cd_off;
            return 0;
        }
    }
    return -1;
}

static int find_patch(const char *name, int nlen) {
    for (int i = 0; i < NP; i++) if ((int)strlen(P[i].name) == nlen && !memcmp(P[i].name, name, nlen)) return i;
    return -1;
}

/* 1 = all patch entries stock, 2 = all patched, 0 = anything else */
static int zip_state(const uint8_t *z, long len) {
    zipinfo zi;
    if (zip_open(z, len, &zi)) return 0;
    int st = -1, found = 0;
    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        if (p + 46 > z + len || rd32(p) != 0x02014b50) return 0;
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32);
        int k = find_patch((const char *)p + 46, nl);
        if (k >= 0) {
            uint32_t crc = rd32(p + 16), us = rd32(p + 24);
            int s = us != P[k].size ? 0 : crc == P[k].crc_stock ? 1 : crc == P[k].crc_patched ? 2 : 0;
            if (!s || (st >= 0 && st != s)) return 0;
            st = s; found++;
        }
        p += 46 + nl + el + cl;
    }
    return found == NP ? st : 0;
}

static uint8_t *inflate_raw(const uint8_t *src, uint32_t clen, uint32_t ulen) {
    uint8_t *out = malloc(ulen ? ulen : 1);
    z_stream s; memset(&s, 0, sizeof s);
    if (!out || inflateInit2(&s, -15) != Z_OK) { free(out); return NULL; }
    s.next_in = (Bytef *)src; s.avail_in = clen; s.next_out = out; s.avail_out = ulen;
    int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (r != Z_STREAM_END || s.total_out != ulen) { free(out); return NULL; }
    return out;
}

static uint8_t *deflate_raw(const uint8_t *src, uint32_t len, uint32_t *clen) {
    z_stream s; memset(&s, 0, sizeof s);
    if (deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return NULL;
    uLong cap = deflateBound(&s, len);
    uint8_t *out = malloc(cap);
    if (!out) { deflateEnd(&s); return NULL; }
    s.next_in = (Bytef *)src; s.avail_in = len; s.next_out = out; s.avail_out = cap;
    int r = deflate(&s, Z_FINISH);
    *clen = (uint32_t)s.total_out;
    deflateEnd(&s);
    if (r != Z_STREAM_END) { free(out); return NULL; }
    return out;
}

/* rewrite the zip: patched entries re-deflated with the bytes changed, everything else copied as is */
static int rewrite(const uint8_t *z, long len, int to_patched, const char *outpath) {
    zipinfo zi;
    if (zip_open(z, len, &zi)) return die("not a zip file");
    uint8_t *o = malloc(len + 65536 + NP * 65536);
    uint8_t *cd = malloc(zi.cd_size + 64);
    if (!o || !cd) return die("out of memory");
    long w = 0, cw = 0, done = 0;
    const uint8_t *p = zi.cd;
    for (int i = 0; i < zi.n; i++) {
        uint16_t nl = rd16(p + 28), el = rd16(p + 30), cl = rd16(p + 32), flags = rd16(p + 8), method = rd16(p + 10);
        uint32_t csize = rd32(p + 20), usize = rd32(p + 24), lho = rd32(p + 42), crc = rd32(p + 16);
        const uint8_t *lh = z + lho;
        if (lho + 30 > (uint32_t)len || rd32(lh) != 0x04034b50) return die("bad local header");
        uint16_t lnl = rd16(lh + 26), lel = rd16(lh + 28);
        const uint8_t *data = lh + 30 + lnl + lel;
        if (data + csize > z + len) return die("entry beyond end of file");
        uint32_t newoff = (uint32_t)w;
        int k = find_patch((const char *)p + 46, nl);
        if (k < 0) {
            /* copy local header + data (+ data descriptor) unchanged */
            long end = (data - z) + csize;
            if (flags & 8) end += (end + 4 <= len && rd32(z + end) == 0x08074b50) ? 16 : 12;
            memcpy(o + w, lh, end - lho); w += end - lho;
        } else {
            pentry *e = &P[k];
            if (method != 8 || usize != e->size) return die("unexpected entry format");
            uint8_t *u = inflate_raw(data, csize, usize);
            if (!u) return die("inflate failed");
            uint32_t want_before = to_patched ? e->crc_stock : e->crc_patched, want_after = to_patched ? e->crc_patched : e->crc_stock;
            if (crc32(0, u, usize) != want_before || crc != want_before) { free(u); return die("entry CRC is not the expected version - nothing written"); }
            for (int b = 0; b < e->nb; b++) {
                uint8_t from = to_patched ? e->from[b] : e->to[b], to = to_patched ? e->to[b] : e->from[b];
                if (u[e->off[b]] != from) { free(u); return die("byte check failed - nothing written"); }
                u[e->off[b]] = to;
            }
            uint32_t ncrc = (uint32_t)crc32(0, u, usize);
            if (ncrc != want_after) { free(u); return die("result CRC mismatch - nothing written"); }
            uint32_t nc; uint8_t *c = deflate_raw(u, usize, &nc);
            free(u);
            if (!c) return die("deflate failed");
            uint8_t *h = o + w;
            memcpy(h, lh, 30);
            wr16(h + 6, rd16(lh + 6) & ~8u);              /* no data descriptor */
            wr32(h + 14, ncrc); wr32(h + 18, nc); wr32(h + 22, usize);
            wr16(h + 26, lnl); wr16(h + 28, 0);
            memcpy(h + 30, lh + 30, lnl);
            w += 30 + lnl;
            memcpy(o + w, c, nc); w += nc;
            free(c);
            crc = ncrc; csize = nc; flags &= ~8u; done++;
        }
        /* central directory record with the new offset (and new crc/size for patched entries) */
        memcpy(cd + cw, p, 46 + nl + el + cl);
        wr16(cd + cw + 8, flags); wr32(cd + cw + 16, crc); wr32(cd + cw + 20, csize); wr32(cd + cw + 42, newoff);
        cw += 46 + nl + el + cl;
        p += 46 + nl + el + cl;
    }
    if (done != NP) return die("not all patch entries found");
    uint32_t cdoff = (uint32_t)w;
    memcpy(o + w, cd, cw); w += cw;
    memcpy(o + w, z + zi.eocd, len - zi.eocd);                /* EOCD + comment */
    wr32(o + w + 12, (uint32_t)cw); wr32(o + w + 16, cdoff);
    w += len - zi.eocd;
    /* self check before writing */
    if (zip_state(o, w) != (to_patched ? 2 : 1)) return die("self check of the result failed - nothing written");
    FILE *f = fopen(outpath, "wb");
    if (!f) return die("cannot create output file");
    if (fwrite(o, 1, w, f) != (size_t)w || fclose(f)) return die("write failed");
    free(o); free(cd);
    printf("%s: %s, %ld bytes\n", outpath, to_patched ? "patched" : "stock", w);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: vhpatch check <zip> <patch> | apply|revert <zip> <patch> <out>\n"); return 2; }
    if (load_patch(argv[3])) return 2;
    long len; uint8_t *z = load_file(argv[2], &len);
    if (!z) return die("cannot read zip");
    int st = zip_state(z, len);
    if (!strcmp(argv[1], "check")) {
        puts(st == 1 ? "stock" : st == 2 ? "patched" : "unknown");
        return st ? 0 : 3;
    }
    if (argc < 5) return die("missing output file");
    if (!strcmp(argv[1], "apply")) {
        if (st != 1) return die(st == 2 ? "already patched" : "zip is not the expected stock version - nothing written");
        return rewrite(z, len, 1, argv[4]);
    }
    if (!strcmp(argv[1], "revert")) {
        if (st != 2) return die(st == 1 ? "already stock" : "zip is not the expected patched version - nothing written");
        return rewrite(z, len, 0, argv[4]);
    }
    return die("unknown command");
}
