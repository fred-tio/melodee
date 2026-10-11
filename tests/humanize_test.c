/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of HUM (PRD 0003, humanize.c), same sources as the firmware (through hostsim.c):
 *   build/host/humanize_test
 * 1. hum_vel: the curve's order and depth, the jitter's range, accents, 1..127; an external clock's offsets
 * 2. HUM 0: humanize draws nothing, every note at its step's velocity
 * 3. timing: +-30 ms around the grid, early and late, a chord's notes together; a quarter of the shortest step at
 *    most (no two steps swap); ties held
 * 4. velocity in the sequencer: the global bar's 16th (polymeter too), accents, new on every pass; gate +-10 %
 * 5. the arp: velocity, timing and gate vary, no drift
 * 6. the shared rng(): step chance and the arp's RANDOM play the same notes at HUM 0 and 100
 * 7. timed notes (live recording, Capture, edited notes: recording.c): velocity, timing and gate as steps
 * Note-ons are read from the voices (a new age), once a block (CTL samples). */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define HUM_T (FS * HUM_MS / 1000u)                  /* the timing at 100 %, in samples */

static int check(const char *what, int ok)
{
    printf("humanize: %-80s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------ capture --- */
typedef struct { uint32_t at, off, age; uint8_t k, v, note, vel; } ev_t;
static ev_t ev[4096];
static uint32_t nev, now;
static void block(void)
{
    int32_t o[2 * CTL];
    uint32_t top = vage, k, i, e;
    mix_block(o, CTL);
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &trk[k].v[i];
            if (v->active && v->age > top && nev < NELEM(ev))
                ev[nev++] = (ev_t){now, 0, v->age, (uint8_t)k, (uint8_t)i, v->note, v->vel};
        }
    for (e = 0; e < nev; e++)
        if (!ev[e].off && (trk[ev[e].k].v[ev[e].v].age != ev[e].age || !trk[ev[e].k].v[ev[e].v].gate))
            ev[e].off = now;
    now += CTL;
}
static void run(uint32_t samples, int play)
{
    nev = now = 0;
    if (play)
        transport_req = 1;
    while (now < samples)
        block();
    transport_req = 2;
    block();
}
/* the note-ons of track k in order, their times (a chord: one), at most n */
static uint32_t times(uint32_t k, uint32_t *at, uint32_t n)
{
    uint32_t e, c = 0;
    for (e = 0; e < nev && c < n; e++)
        if (ev[e].k == k && (!c || at[c - 1] != ev[e].at))
            at[c++] = ev[e].at;
    return c;
}

static void setup(int hum)
{
    uint32_t k;
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_HUM] = (int16_t)hum;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLEN] = 16;
        trk[k].p[P_LRATE] = 0;                       /* no S&H draw from the shared rng() in the test's time */
        trk[k].lfo_ph = 0;
    }
    rng_state = 0x1234567u;
    hum_state = 0x2545F491u;
    memset(live_held, 0, sizeof live_held);          /* (the arp tests' keys: never released) */
    memset(recording, 0, sizeof recording);
    memset(recording_run, 0, sizeof recording_run);
    memset(recording_flags, 0, sizeof recording_flags);
    recording_reindex();
}
static void timed(track_t *t, uint32_t step, uint32_t note)   /* a timed note on the step, half a step long, vel 100 */
{
    uint32_t j;
    step_t *s = &t->step[step];
    for (j = 0; j < RECORD_MAX && recording[j].vel; j++)
        ;
    recording_restore_note(t, j, (recorded_note_t){0, 32768, (uint8_t)note, 100, (uint8_t)recording_owner(t), (uint8_t)step, 0, 0});
    s->time = ST_NOTE;
    s->flags |= SF_RECORDED;
    s->note[s->n++] = (uint8_t)note;
}
static void timed_pattern(uint32_t n)                /* track 1: every step n timed notes (live, Capture) */
{
    uint32_t i, j;
    memset(trk[0].step, 0, sizeof trk[0].step);
    for (i = 0; i < 16u; i++)
        for (j = 0; j < n; j++)
            timed(&trk[0], i, 60u + 4u * j);
}
static void notes(track_t *t, uint32_t len, uint32_t n)   /* every step: n notes from C4 */
{
    uint32_t i, j;
    t->p[P_SLEN] = (int16_t)len;
    memset(t->step, 0, sizeof t->step);
    for (i = 0; i < len; i++) {
        t->step[i].time = ST_NOTE;
        t->step[i].n = (uint8_t)n;
        for (j = 0; j < n; j++)
            t->step[i].note[j] = (uint8_t)(60u + 4u * j);
    }
}
static uint32_t dip_class(uint32_t s16) { return !s16 ? 0u : s16 == 8u ? 1u : !(s16 & 3u) ? 2u : !(s16 & 1u) ? 3u : 4u; }
static int vel_in_class(uint32_t vel, uint32_t base, uint32_t s16)
{
    int32_t mid = (int32_t)base - HUM_DIP[dip_class(s16)];
    return (int32_t)vel >= mid - 30 && (int32_t)vel <= mid + 30;
}

/* ------------------------------------------------------------ 1. hum_vel --- */
static int test_curve(void)
{
    static const uint32_t S16[8] = {0, 8, 4, 12, 2, 6, 1, 15};   /* beat 1, 3, 2, 4, 8ths, 16ths */
    static const int32_t WANT[8] = {90, 78, 66, 66, 54, 54, 40, 40};   /* (90: +30 stays under 127) */
    int32_t lo[8], hi[8], m[8];
    uint32_t c, i, alo = 127, ahi = 0, clamp_bad = 0;
    int bad = 0, ok = 1;
    setup(100);
    for (c = 0; c < 8u; c++) {
        int64_t sum = 0;
        lo[c] = 127, hi[c] = 0;
        for (i = 0; i < 8000u; i++) {
            int32_t v = (int32_t)hum_vel(90, S16[c], 0);
            sum += v;
            lo[c] = v < lo[c] ? v : lo[c];
            hi[c] = v > hi[c] ? v : hi[c];
        }
        m[c] = (int32_t)((sum * 10 + 4000) / 8000);     /* tenths */
        ok &= abs(m[c] - 10 * WANT[c]) <= 10 && lo[c] == WANT[c] - 30 && hi[c] == WANT[c] + 30;
    }
    bad += check("curve at 100 %: beat 1 keeps the velocity, 3 -12, 2/4 -24, 8ths -36, 16ths -50; jitter +-30", ok);
    bad += check("curve order: beat 1 > beat 3 > beats 2 and 4 > 8th off-beats > 16th off-beats",
                 m[0] > m[1] && m[1] > m[2] && m[3] > m[4] && m[5] > m[6]);
    song.g[G_HUM] = 50;
    for (i = 0, lo[0] = 127, hi[0] = 0; i < 8000u; i++) {
        int32_t v = (int32_t)hum_vel(100, 1, 0);
        lo[0] = v < lo[0] ? v : lo[0];
        hi[0] = v > hi[0] ? v : hi[0];
    }
    bad += check("HUM 50 %: half the dip (-25) and half the jitter (+-15)", lo[0] == 100 - 25 - 15 && hi[0] == 100 - 25 + 15);
    song.g[G_HUM] = 100;
    for (i = 0; i < 8000u; i++) {
        uint32_t a = hum_vel(127, i & 15u, 1);
        alo = a < alo ? a : alo;
        ahi = a > ahi ? a : ahi;
        clamp_bad += hum_vel(5, 1, 0) < 1u || hum_vel(127, 0, 0) > 127u;
    }
    bad += check("accent at 100 %: 127, at most 6 lower, no curve (every 16th)", alo == 121u && ahi == 127u);
    bad += check("result stays in 1..127", !clamp_bad);
    song.g[G_HUM] = 0;
    bad += check("HUM 0: velocity and accent as written", hum_vel(77, 1, 0) == 77u && hum_vel(127, 3, 1) == 127u);

    {   /* an external clock: the offset in its units, on the predicted beat (F14) */
        int32_t omax = 0;
        song.g[G_HUM] = 100;
        song.g[G_CLOCK] = 1;
        midi_beat_samples = FS / 2u;                     /* 120 BPM: 30 ms = 24576 * 1323 / 22050 units */
        for (i = 0; i < 8000u; i++) {
            int32_t o = abs(hum_shift(&trk[0], MIDI_BEAT_UNITS / 4u));
            omax = o > omax ? o : omax;
        }
        ok = omax == (int32_t)(HUM_T * MIDI_BEAT_UNITS / (FS / 2u));
        midi_beat_samples = FS;                          /* 60 BPM: the same 30 ms are half the units */
        for (i = 0, omax = 0; i < 8000u; i++) {
            int32_t o = abs(hum_shift(&trk[0], MIDI_BEAT_UNITS / 4u));
            omax = o > omax ? o : omax;
        }
        ok &= omax == (int32_t)(HUM_T * MIDI_BEAT_UNITS / FS);
        song.g[G_CLOCK] = 0;
        midi_beat_samples = 0;
        bad += check("external clock: +-30 ms in clock units on the predicted beat (120 and 60 BPM)", ok);
    }
    return bad;
}

/* ------------------------------------------------------------ 2. HUM 0 --- */
static int test_off(void)
{
    uint32_t e, vel = 0;
    setup(0);
    notes(&trk[0], 16, 3);
    trk[0].step[3].vel = 50;
    run(2u * 16u * 5512u, 1);
    for (e = 0; e < nev; e++)
        vel += ev[e].vel != (((ev[e].at / 5512u) & 15u) == 3u ? 50u : 96u);
    return check("HUM 0: no draw from humanize's generator, every note at its step's velocity",
                 hum_state == 0x2545F491u && nev > 90u && !vel);
}

/* ------------------------------------------------------------ 3. timing --- */
static int test_timing(void)
{
    static uint32_t g[300], a[300], len0[300];
    uint32_t ng, na, n, k, e, order = 1, chord = 1, differ = 0, nlen = 0;
    int32_t dmin = 0, dmax = 0, sum = 0, glo = 0x7FFFFFFF, ghi = 0;
    int bad = 0;
    setup(0);                                        /* the grid: HUM 0 */
    notes(&trk[0], 16, 1);
    run(16u * 16u * 5512u, 1);
    ng = times(0, g, NELEM(g));
    for (e = 0; e < nev && nlen < NELEM(len0); e++)
        if (ev[e].off)
            len0[nlen++] = ev[e].off - ev[e].at;
    setup(100);
    notes(&trk[0], 16, 1);
    run(16u * 16u * 5512u, 1);
    na = times(0, a, NELEM(a));
    n = (ng < na ? ng : na) - 1u;
    for (k = 0; k < n; k++) {
        int32_t d = (int32_t)a[k] - (int32_t)g[k];
        dmin = d < dmin ? d : dmin;
        dmax = d > dmax ? d : dmax;
        sum += d;
        order &= !k || a[k] > a[k - 1];
    }
    for (e = 0; e < nev; e++)
        if (ev[e].off) {
            int32_t l = (int32_t)(ev[e].off - ev[e].at);
            glo = l < glo ? l : glo;
            ghi = l > ghi ? l : ghi;
        }
    bad += check("timing at 100 %: +-30 ms around the grid (1323 samples), early and late, centred",
                 n > 250u && dmin >= -(int32_t)HUM_T - (int32_t)CTL && dmax <= (int32_t)HUM_T + (int32_t)CTL && dmin < -900 && dmax > 900 &&
                 abs(sum / (int32_t)n) < 200);
    bad += check("timing: the steps keep their order", order);
    bad += check("gate at 100 %: the note length +-10 % (HUM 0: one length)",
                 nlen > 200u && len0[0] == len0[nlen / 2u] && glo >= (int32_t)len0[0] * 9 / 10 - 2 * (int32_t)CTL &&
                 ghi <= (int32_t)len0[0] * 11 / 10 + 2 * (int32_t)CTL && glo < (int32_t)len0[0] * 95 / 100 &&
                 ghi > (int32_t)len0[0] * 105 / 100);

    setup(100);                                      /* a chord: one offset, a velocity each */
    notes(&trk[0], 16, 3);
    run(4u * 16u * 5512u, 1);
    for (e = 0; e + 2u < nev; e += 3u) {
        chord &= ev[e].at == ev[e + 1].at && ev[e].at == ev[e + 2].at;
        differ += ev[e].vel != ev[e + 1].vel || ev[e].vel != ev[e + 2].vel;
    }
    bad += check("a chord's notes start together, each with its own velocity", nev > 180u && chord && differ > 30u);

    {   /* the shortest step: 240 BPM, 1/32, SWG 100 (0.6 x 1378): a quarter of it, 206 samples */
        uint32_t sh = swing_step_len(&trk[0], 1378u, 1u);
        setup(0);
        song.g[G_BPM] = 240;
        song.g[G_SWING] = 100;
        trk[0].p[P_SDIV] = 3;
        notes(&trk[0], 16, 1);
        sh = swing_step_len(&trk[0], div_samples(3), 1u);
        run(32u * 16u * 1378u, 1);
        ng = times(0, g, NELEM(g));
        setup(100);
        song.g[G_BPM] = 240;
        song.g[G_SWING] = 100;
        trk[0].p[P_SDIV] = 3;
        notes(&trk[0], 16, 1);
        run(32u * 16u * 1378u, 1);
        na = times(0, a, NELEM(a));
        n = (ng < na ? ng : na) - 1u;
        order = 1;
        dmax = 0;
        for (k = 0; k < n; k++) {
            int32_t d = abs((int32_t)a[k] - (int32_t)g[k]);
            dmax = d > dmax ? d : dmax;
            order &= !k || a[k] - a[k - 1] >= sh / 2u - CTL;
        }
        bad += check("shortest step (240 BPM, 1/32, SWG 100): at most a quarter of it, never two steps swap",
                     n > 250u && dmax <= (int32_t)(sh / 4u + CTL) && dmax > (int32_t)(sh / 4u) - 40 && order);
    }

    {   /* a TIE chain at 100 %: one note, held through its ties */
        uint32_t held = 1, i;
        setup(100);
        notes(&trk[0], 8, 1);
        for (i = 1; i < 4u; i++)
            trk[0].step[i].time = ST_TIE;
        for (i = 4; i < 8u; i++)
            trk[0].step[i].time = ST_REST;
        run(8u * 8u * 5512u, 1);
        for (e = 0; e < nev; e++)
            held &= !ev[e].off || ev[e].off - ev[e].at >= 3u * 5512u;
        bad += check("ties at 100 %: one note-on, held through its three ties", nev == 8u && held);
    }
    return bad;
}

/* --------------------------------------------- 4. velocity in the sequencer --- */
static int test_velocity(void)
{
    uint32_t e, c[NTRK] = {0}, inside = 1, acc = 1, lane1 = 1, nl1 = 0, same = 1;
    uint8_t bar[2][16];
    setup(100);
    notes(&trk[0], 16, 1);                           /* 1/16 */
    notes(&trk[1], 3, 1);                            /* polymeter: 3 steps, the global bar's 16ths */
    notes(&trk[2], 16, 1);                           /* every step accented */
    for (e = 0; e < 16u; e++)
        trk[2].step[e].flags = SF_ACCENT;
    host_preset(&trk[3], ENGI_DRUM, 0);              /* DRUM: lane 1 accented, lane 2 not */
    for (e = 0; e < 16u; e++) {
        trk[3].step[e].time = ST_NOTE;
        trk[3].step[e].hit = 3u;
        trk[3].step[e].acc = 1u;
    }
    run(8u * 16u * 5512u, 1);
    for (e = 0; e < nev; e++) {
        const ev_t *x = &ev[e];
        uint32_t s16 = (x->at + 2756u) / 5512u % 16u;   /* the nearest 16th (the offset is under half a step) */
        if (x->k < 2u) {
            inside &= vel_in_class(x->vel, 96u, c[x->k] % 16u);
            if (!x->k && c[0] < 32u)
                bar[c[0] / 16u][c[0] % 16u] = x->vel;
            c[x->k]++;
        } else if (x->k == 2u || x->note == DRUM_LANE_NOTE[0])
            acc &= x->vel >= 121u && x->vel <= 127u;
        else if (x->note == DRUM_LANE_NOTE[1]) {
            lane1 &= vel_in_class(x->vel, 96u, s16);
            nl1++;
        }
    }
    for (e = 0; e < 16u; e++)
        same &= bar[0][e] == bar[1][e];
    return check("sequencer: each note in its 16th's range of the global bar (1/16 and a 3-step track)",
                 c[0] > 120u && c[1] > 120u && inside) +
           check("accented steps and accented drum hits: 127, at most 6 lower", acc) +
           check("an unaccented drum hit follows the curve", nl1 > 120u && lane1) +
           check("new on every pass: bar 2's velocities are not bar 1's", !same);
}

/* ------------------------------------------------------------ 5. the arp --- */
static uint32_t arp_run(int hum, uint32_t *at, uint8_t *vel, uint32_t *len, uint32_t n)
{
    uint32_t e, c = 0;
    setup(hum);
    trk[0].p[P_AMODE] = 1;                           /* UP, 1/16, transport stopped (INT) */
    trk[0].p[P_ARATE] = 2;
    input_on(&trk[0], 60, 100);
    input_on(&trk[0], 64, 100);
    input_on(&trk[0], 67, 100);
    run(70u * 5512u, 0);
    for (e = 0; e < nev && c < n; e++, c++) {
        at[c] = ev[e].at;
        vel[c] = ev[e].vel;
        len[c] = ev[e].off ? ev[e].off - ev[e].at : 0u;
    }
    return c;
}
static int test_arp(void)
{
    static uint32_t a0[80], a1[80], l0[80], l1[80];
    static uint8_t v0[80], v1[80];
    uint32_t n0 = arp_run(0, a0, v0, l0, 64), n1 = arp_run(100, a1, v1, l1, 64), k;
    int32_t lim = (int32_t)HUM_T + (int32_t)CTL, c0 = (int32_t)(a0[1] - a0[0]), ilo = 0x7FFFFFFF, ihi = 0;
    uint32_t vin = 1, vdiff = 0, ldiff = 0, even = 1, drift = 0;
    for (k = 1; k < 64u; k++)                        /* the INT arp drops a block's remainder at each note, at HUM 0 too */
        drift |= abs((int32_t)(a1[k] - a1[0]) - (int32_t)(a0[k] - a0[0])) > lim + (int32_t)(k * CTL);
    for (k = 1; k < 63u; k++) {
        int32_t d = (int32_t)(a1[k] - a1[k - 1]);
        ilo = d < ilo ? d : ilo;
        ihi = d > ihi ? d : ihi;
        even &= (int32_t)(a0[k] - a0[k - 1]) == c0;
        vin &= v1[k] >= 20u && v1[k] <= 127u;
        vdiff += v1[k] != v1[k - 1];
        ldiff += l1[k] && l1[k - 1] && l1[k] != l1[k - 1];
    }
    return check("arp at 100 %: each interval within +-60 ms of HUM 0's, shorter and longer (HUM 0: even)",
                 n0 == 64u && n1 == 64u && even && ilo >= c0 - 2 * lim && ihi <= c0 + 2 * lim && ilo < c0 - 400 &&
                 ihi > c0 + 400) +
           check("arp: the offsets do not add up (note k within 30 ms + k blocks of HUM 0's)", !drift) +
           check("arp: velocity (100 on the curve, +-30) and gate vary", vin && vdiff > 40u && ldiff > 20u);
}

/* ------------------------------------------------------- 6. the shared rng() --- */
static uint32_t chance_run(int hum, uint8_t *note, uint32_t n)
{
    uint32_t i, c = 0, e;
    setup(hum);
    notes(&trk[0], 16, 1);
    for (i = 0; i < 16u; i++) {
        trk[0].step[i].note[0] = (uint8_t)(48u + i);
        step_set_chance(&trk[0].step[i], 50);
    }
    run(8u * 16u * 5512u, 1);
    for (e = 0; e < nev && c < n; e++)
        note[c++] = ev[e].note;
    return c;
}
static uint32_t random_arp_run(int hum, uint8_t *note, uint32_t n)
{
    uint32_t c = 0, e;
    setup(hum);
    trk[0].p[P_AMODE] = 4;                           /* RANDOM, PROB about half */
    trk[0].p[P_ARATE] = 2;
    trk[0].p[P_APROB] = 64;
    input_on(&trk[0], 60, 100);
    input_on(&trk[0], 62, 100);
    input_on(&trk[0], 65, 100);
    input_on(&trk[0], 67, 100);
    run(130u * 5512u, 0);
    for (e = 0; e < nev && c < n; e++)
        note[c++] = ev[e].note;
    return c;
}
static int test_rng(void)
{
    static uint8_t a[200], b[200];
    uint32_t na = chance_run(0, a, 200), nb = chance_run(100, b, 200), k, same = na == nb && na > 40u && na < 100u;
    int bad;
    for (k = 0; same && k < na; k++)
        same = a[k] == b[k];
    bad = check("step chance: the same steps sound at HUM 0 and 100 (it rolls on the grid)", same);
    na = random_arp_run(0, a, 200);
    nb = random_arp_run(100, b, 200);
    same = na > 40u && (na > nb ? na - nb : nb - na) <= 1u;
    for (k = 0; same && k + 1u < na && k + 1u < nb; k++)
        same = a[k] == b[k];
    bad += check("arp RANDOM and PROB: the same notes at HUM 0 and 100 (humanize's own generator)", same);
    return bad;
}

/* ------------------------------------------------------------ 7. timed notes --- */
static int test_timed(void)
{
    static uint32_t g[300], a[300];
    uint32_t ng, na, n, k, e, order = 1, vel0 = 1, inside = 1, vdiff = 0, chord = 1, passes = 0;
    int32_t dmin = 0, dmax = 0, lo = 0x7FFFFFFF, hi = 0, len0;
    int bad = 0;
    setup(0);
    timed_pattern(1);
    run(8u * 16u * 5512u, 1);
    ng = times(0, g, NELEM(g));
    len0 = (int32_t)(ev[1].off - ev[1].at);
    for (e = 0; e < nev; e++)
        vel0 &= ev[e].vel == 100u;
    setup(100);
    timed_pattern(1);
    run(8u * 16u * 5512u, 1);
    na = times(0, a, NELEM(a));
    n = (ng < na ? ng : na) - 1u;
    for (k = 0; k < n; k++) {
        int32_t d = (int32_t)a[k] - (int32_t)g[k];
        dmin = d < dmin ? d : dmin;
        dmax = d > dmax ? d : dmax;
        order &= !k || a[k] > a[k - 1];
        passes += k >= 16u && d != (int32_t)a[k - 16u] - (int32_t)g[k - 16u];
    }
    for (e = 0; e < nev; e++) {
        inside &= vel_in_class(ev[e].vel, 100u, e % 16u);
        vdiff += e && ev[e].vel != ev[e - 1].vel;
        if (ev[e].off) {
            int32_t l = (int32_t)(ev[e].off - ev[e].at);
            lo = l < lo ? l : lo;
            hi = l > hi ? l : hi;
        }
    }
    bad += check("timed notes, HUM 0: as recorded (velocity 100, on their onsets)", ng > 120u && vel0);
    bad += check("timed notes at 100 %: +-30 ms around their onsets, early and late, in order, new each pass",
                 n > 120u && dmin >= -(int32_t)HUM_T - (int32_t)CTL && dmax <= (int32_t)HUM_T + (int32_t)CTL &&
                 dmin < -900 && dmax > 900 && order && passes > 60u);
    bad += check("timed notes at 100 %: velocity on the bar's curve +-30, gate +-10 %",
                 inside && vdiff > 60u && lo >= len0 * 9 / 10 - 2 * (int32_t)CTL && hi <= len0 * 11 / 10 + 2 * (int32_t)CTL &&
                 lo < len0 * 95 / 100 && hi > len0 * 105 / 100);
    setup(100);
    timed_pattern(3);
    run(4u * 16u * 5512u, 1);
    for (e = 0; e + 2u < nev; e += 3u)
        chord &= ev[e].at == ev[e + 1].at && ev[e].at == ev[e + 2].at;
    bad += check("timed notes: a step's notes move together", nev > 180u && chord);
    return bad;
}

int main(void)
{
    int bad = test_curve() + test_off() + test_timing() + test_velocity() + test_arp() + test_rng() + test_timed();
    printf(bad ? "humanize: %d FAILED\n" : "humanize: all ok\n", bad);
    return bad != 0;
}
