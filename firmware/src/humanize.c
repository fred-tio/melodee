/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HUM (G_HUM, SEQ > TEMPO; PRD 0003): the sequencer's steps and the arp's notes as a person plays them. Included by
 * seq.c. 0 % plays the grid as written. At 100 %: a velocity curve over the 4/4 bar (beat 1 as written, the 16th
 * off-beats 50 lower), +-30 velocity per note, +-30 ms per step (its notes and hits together), +-10 % gate. An
 * accent stays at 127, at most 6 lower. Its own generator: the shared rng() (step chance, the arp's RANDOM and PROB,
 * S&H) is not drawn. A track that records plays on the grid: humanize never writes into the pattern.
 * Timing never moves the step clock (seq_idx, seq_pos): a late step waits in hum[] (seq.c hum_enter), an early one
 * plays before its step (hum_tick). The arp's clock counts from its last note, so its offsets are relative. Timed
 * notes (recording.c) play when their phase of the loop comes: it moves by their step's offset (hum_timed). */
#define HUM_MS 30u                                      /* timing at 100 % */
#define HUM_TICKS 96u                                   /* a beat: every DIV (the triplets too) is whole ticks */
static uint32_t hum_state = 0x2545F491u;
static uint32_t hum_rng(void)
{
    hum_state ^= hum_state << 13;
    hum_state ^= hum_state >> 17;
    hum_state ^= hum_state << 5;
    return hum_state;
}
static int32_t hum_pm(int32_t a) { return a > 0 ? (int32_t)(hum_rng() % (2u * (uint32_t)a + 1u)) - a : 0; }
static int32_t hum_pct(int32_t full) { return full * song.g[G_HUM] / 100; }

/* per track: the step's place in the bar (HUM_TICKS a beat, from PLAY), the next step's offset, a late step waiting
 * (its index, skip bits, length argument and 16th), an early one played */
static struct hum_step {
    uint32_t tick, wait, skip, arg;
    int32_t next;
    uint8_t pend, early, idx, s16;
} hum[NTRK];
static struct hum_arp { int32_t cur, next; } hum_arp[NPART];   /* the arp: the offset of its last note, of its next */

/* the 16th of the bar (0..15) at tick, the nearest one (triplets) */
static uint32_t hum_s16(uint32_t tick) { return (tick % (4u * HUM_TICKS) + HUM_TICKS / 8u) / (HUM_TICKS / 4u) % 16u; }
static uint32_t hum_ticks(uint32_t len)                 /* a straight step of len clock units, in ticks */
{
    uint32_t b = seq_beat_samples();
    return (len * HUM_TICKS + b / 2u) / b;
}
static uint32_t hum_beat_s16(void)                      /* the 16th of the global beat (beat_n, beat_pos) */
{
    return hum_s16(beat_n * HUM_TICKS + beat_pos * HUM_TICKS / seq_beat_samples());
}

/* the dip at 100 %: beat 1, beat 3, beats 2 and 4, the 8th off-beats, the 16th off-beats (F4). PRD 0003's first
 * values (25, +-25, 20 ms) were about -2 dB on the 808: too little to hear */
static const uint8_t HUM_DIP[5] = {0, 12, 24, 36, 50};
static uint32_t hum_vel(uint32_t vel, uint32_t s16, int accent)
{
    uint32_t k = !s16 ? 0u : s16 == 8u ? 1u : !(s16 & 3u) ? 2u : !(s16 & 1u) ? 3u : 4u;
    if (!song.g[G_HUM])
        return vel;
    if (accent)
        return (uint32_t)clamp((int32_t)vel - (int32_t)(hum_rng() % (uint32_t)(hum_pct(6) + 1)), 1, 127);
    return (uint32_t)clamp((int32_t)vel - hum_pct(HUM_DIP[k]) + hum_pm(hum_pct(30)), 1, 127);
}

static uint32_t hum_gate(const track_t *t, uint32_t gate)
{
    if (!song.g[G_HUM] || rec_on(t))
        return gate;
    return (uint32_t)((int32_t)gate + (int32_t)gate * hum_pm(hum_pct(100)) / 1000);
}

/* the range of a note's offset in clock units (an external clock's: on its predicted beat), at most a quarter of
 * the shortest step: two steps never swap (F9) */
static uint32_t hum_amp(const track_t *t, uint32_t shortest)
{
    uint32_t a;
    if (!song.g[G_HUM] || rec_on(t))
        return 0;
    a = (uint32_t)hum_pct((int32_t)(FS * HUM_MS / 1000u));
    if (song.g[G_CLOCK])
        a = a * MIDI_BEAT_UNITS / beat_samples();
    return a < shortest / 4u ? a : shortest / 4u;
}
static int32_t hum_shift(const track_t *t, uint32_t shortest) { return hum_pm((int32_t)hum_amp(t, shortest)); }

/* a timed note (recording.c) keeps no offset: it comes from its step and the track's pass of the loop, so the notes
 * of one step move together and every pass moves them anew. -a..a */
static uint32_t hum_pass[NTRK];
static int32_t hum_timed(const track_t *t, uint32_t step, uint32_t a)
{
    uint32_t x = trk_index(t) << 24 ^ step << 16 ^ hum_pass[trk_index(t)];
    x ^= x >> 16;                                       /* lowbias32 (C. Wellons): a hash as good as a generator */
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (int32_t)(x % (2u * a + 1u)) - (int32_t)a;
}
/* the 16th of a note `ahead` clock units after the start of the step the clock is in (hum[].tick) */
static uint32_t hum_ahead_s16(const track_t *t, int32_t ahead)
{
    int32_t x = (int32_t)hum[trk_index(t)].tick + ahead * (int32_t)HUM_TICKS / (int32_t)seq_beat_samples();
    return hum_s16((uint32_t)(x % (int32_t)(4u * HUM_TICKS) + (int32_t)(4u * HUM_TICKS)));
}
