/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host check of the documented FM-1 flash-id allowlist and of the identification policy
 * (hal/fm1_flash_id.h). This is a pure helper test: it does not claim that either part was
 * read from hardware; it proves what the firmware refuses. */
#include <stdio.h>
#include "../firmware/hal/fm1_flash_id.h"

static int bad;

static void expect(const char *what, int rc, int want)
{
    if (rc != want) {
        printf("%-58s got %d (%s), want %d\n", what, rc, fl_id_reason(rc), want);
        bad++;
    }
}

/* one identification attempt: two equal reads of `id` and status `sr` */
#define ID(id, sr) (&(fl_id_t){(id), (id), (sr)})

int main(void)
{
    /* the allowlist itself */
    if (!fl_jedec_supported(FL_JEDEC_P25Q80H) || !fl_jedec_supported(FL_JEDEC_B36014)) {
        puts("documented id rejected");
        bad++;
    }
    if (fl_jedec_supported(0x123456u) || fl_jedec_supported(0u) || fl_jedec_supported(0xFFFFFFFFu)) {
        puts("unknown id accepted");
        bad++;
    }

    /* a documented part, two agreeing reads, idle and unprotected: writable */
    expect("P25Q80H  (0x856014) verified", fl_id_verify(ID(FL_JEDEC_P25Q80H, 0)), FL_ID_OK);
    expect("B36014   (0xB36014) verified", fl_id_verify(ID(FL_JEDEC_B36014, 0)), FL_ID_OK);

    /* a floating / shorted bus is not a part */
    expect("floating bus (0x000000)", fl_id_verify(ID(0u, 0)), FL_ID_BUS);
    expect("shorted bus (0xFFFFFF)", fl_id_verify(ID(0xFFFFFFu, 0)), FL_ID_BUS);
    expect("failed status read", fl_id_verify(ID(FL_JEDEC_P25Q80H, 0xFFFFFFFFu)), FL_ID_BUS);

    /* two reads that disagree: the register is not stable, so nothing may be written */
    expect("id_a != id_b", fl_id_verify(&(fl_id_t){FL_JEDEC_P25Q80H, FL_JEDEC_B36014, 0}), FL_ID_BUS);

    /* an undocumented part is refused by name, never by similarity */
    expect("undocumented part", fl_id_verify(ID(0x856013u, 0)), FL_ID_UNKNOWN);
    expect("same vendor, other density", fl_id_verify(ID(0x856018u, 0)), FL_ID_UNKNOWN);

    /* a documented family in another density: the layout is 1 MiB, so it is not this layout */
    expect("documented family, 2 MiB", fl_id_verify(ID(0x856015u, 0)), FL_ID_UNKNOWN);
    expect("1 MiB, undocumented id", fl_id_verify(ID(0xC22014u, 0)), FL_ID_UNKNOWN);

    /* block protection: erase/program would be ignored, so writes are refused */
    expect("BP0 set", fl_id_verify(ID(FL_JEDEC_P25Q80H, 0x04u)), FL_ID_PROTECTED);
    expect("BP0..BP2 set", fl_id_verify(ID(FL_JEDEC_P25Q80H, FL_SR1_BP)), FL_ID_PROTECTED);
    expect("busy", fl_id_verify(ID(FL_JEDEC_P25Q80H, FL_SR1_BUSY)), FL_ID_PROTECTED);
    /* WEL / SRP alone do not block a write cycle */
    expect("WEL|SRP only", fl_id_verify(ID(FL_JEDEC_P25Q80H, FL_SR1_WEL | FL_SR1_SRP)), FL_ID_OK);

    puts(bad ? "flash id test FAILED" : "flash id test passed");
    return bad != 0;
}
