/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Pure allowlist and identification policy for the documented FM-1 1 MiB flash parts.
 * Kept separate from fm1_flash.h so a host test can compile it without the pi32v2 SFC
 * primitives.
 *
 * `0x12` (JEDEC ID) alone is not an identification: a floating or mis-clocked bus reads
 * 0x000000 or 0xFFFFFF, a glitch reads a neighbour, and one sample of either is not
 * evidence. The policy below is what the firmware applies before it lets any write
 * through (`flash_ok` in project.c):
 *
 *   1. the ID must be one of the two documented 1 MiB parts, exactly;
 *   2. the capacity byte must say 1 MiB (8 Mbit): the firmware's layout is that part;
 *   3. two independent reads must agree, which rejects a bus that is not answering;
 *   4. the status register must be readable, idle, and not block-protected: a part with
 *      BP0..BP2 set silently ignores erase/program, which is a brick-shaped failure.
 *
 * No entry is accepted by similarity (same vendor, same capacity, "close enough"), and
 * nothing here can be satisfied by a hardcoded value the hardware did not return. */
#pragma once
#include <stdint.h>

#define FL_JEDEC_P25Q80H 0x856014u             /* Puya P25Q80H (documented OEM) */
#define FL_JEDEC_B36014  0xB36014u             /* UCUN UC25HQ80 (documented variant) */

/* Documented 1 MiB SPI NOR chips known in FM-1 / AC791N hardware ecosystem.
 * Read-only & backup support enabled; writes strictly restricted to verified production parts. */
#define FL_JEDEC_GD25Q80 0xC84014u             /* GigaDevice GD25Q80E / GD25Q80C */
#define FL_JEDEC_W25Q80  0xEF4014u             /* Winbond W25Q80DV / W25Q80DL */
#define FL_JEDEC_ZB25Q80 0x5E4014u             /* Zbit ZB25VQ80 */
#define FL_JEDEC_BY25D80 0x684014u             /* BoyaMicro BY25D80 */
#define FL_JEDEC_XM25Q80 0x204014u             /* XMC XM25QH80B */

#define FL_CAPACITY_1MIB 0x14u                 /* JEDEC byte 3: 2^0x14 = 1 MiB (8 Mbit) */

/* SR1 layout of both documented parts (P25Q80H family / B36014 variant) */
#define FL_SR1_BUSY 0x01u
#define FL_SR1_WEL  0x02u
#define FL_SR1_BP   0x1Cu                      /* BP0..BP2: block protection */
#define FL_SR1_SRP  0x80u

/* one identification attempt: two JEDEC reads and one status read */
typedef struct {
    uint32_t id_a, id_b;                       /* 0x9F, twice */
    uint32_t sr;                               /* SR1 | SR2 << 8, as fl_status_ram() returns it */
} fl_id_t;

/* why an identification was refused; 0 is the only value that allows writes */
enum {
    FL_ID_OK          = 0,
    FL_ID_UNKNOWN     = -1,                    /* not a documented part */
    FL_ID_BUS         = -2,                    /* a read failed (0 / 0xFFFFFFFF / mismatch) */
    FL_ID_CAPACITY    = -3,                    /* documented part, not a 1 MiB geometry */
    FL_ID_PROTECTED   = -4                     /* status register busy or block-protected */
};

static inline int fl_jedec_writable(uint32_t id)
{
    return id == FL_JEDEC_P25Q80H || id == FL_JEDEC_B36014;
}

static inline int fl_jedec_readable(uint32_t id)
{
    return fl_jedec_writable(id) ||
           id == FL_JEDEC_GD25Q80 ||
           id == FL_JEDEC_W25Q80  ||
           id == FL_JEDEC_ZB25Q80 ||
           id == FL_JEDEC_BY25D80 ||
           id == FL_JEDEC_XM25Q80;
}

static inline int fl_jedec_supported(uint32_t id)
{
    return fl_jedec_writable(id);
}

static inline int fl_jedec_capacity_ok(uint32_t id)
{
    return (id & 0xFFu) == FL_CAPACITY_1MIB;
}

/* a reading no part produces: all zeros or all ones, on 24 bits or on the full word */
static inline int fl_id_dead(uint32_t id)
{
    return id == 0u || id == 0xFFFFFFFFu || (id & 0xFFFFFFu) == 0u || (id & 0xFFFFFFu) == 0xFFFFFFu;
}

/* 0: the part may be written by this firmware */
static inline int fl_id_verify(const fl_id_t *r)
{
    if (fl_id_dead(r->id_a) || fl_id_dead(r->id_b))
        return FL_ID_BUS;                      /* floating / shorted bus: nothing to identify */
    if (r->id_a != r->id_b)
        return FL_ID_BUS;                      /* two reads of one register disagree: not trusted */
    if (!fl_jedec_supported(r->id_a))
        return FL_ID_UNKNOWN;
    if (!fl_jedec_capacity_ok(r->id_a))
        return FL_ID_CAPACITY;                 /* the same part in another density: layout is 1 MiB */
    if (r->sr > 0xFFFFu)
        return FL_ID_BUS;                      /* status read failed (fl_status_ram: 0xFFFFFFFF) */
    if (r->sr & (FL_SR1_BUSY | FL_SR1_BP))
        return FL_ID_PROTECTED;                /* busy, or erase/program is ignored */
    return FL_ID_OK;
}

static inline const char *fl_id_reason(int rc)
{
    switch (rc) {
    case FL_ID_OK:        return "ok";
    case FL_ID_UNKNOWN:   return "unknown jedec";
    case FL_ID_BUS:       return "flash bus";
    case FL_ID_CAPACITY:  return "capacity";
    case FL_ID_PROTECTED: return "protected";
    default:              return "?";
    }
}
