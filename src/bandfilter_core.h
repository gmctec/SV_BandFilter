/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SV_BandFilter - frequency-range keep/remove filter
 * Copyright (C) 2026 gmctec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the LICENSE file.
 *
 * Filter core. Plain C, no dependency on any plugin API, so the same
 * code can sit behind LADSPA, LV2, CLAP, VST3 or a test harness.
 *
 * Design: an order-BF_PROTO_ORDER analog Butterworth low-pass prototype
 * is transformed to band-pass (keep) or band-stop (remove) with the band
 * edges as the -3 dB points, then mapped to digital with the bilinear
 * transform (edges pre-warped). Result: BF_PROTO_ORDER biquad sections.
 */

#ifndef BANDFILTER_CORE_H
#define BANDFILTER_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Prototype order. Must be even. 8 gives skirts of roughly 48 dB/octave
 * on each side of the band, using 8 biquad sections. */
#define BF_PROTO_ORDER 8
#define BF_SECTIONS    BF_PROTO_ORDER

typedef enum {
    BF_MODE_KEEP_BAND   = 0, /* band-pass: keep low..high, remove the rest */
    BF_MODE_REMOVE_BAND = 1  /* band-stop: remove low..high, keep the rest */
} bf_mode;

typedef struct {
    double b0, b1, b2, a1, a2; /* normalised coefficients (a0 == 1) */
    double z1, z2;             /* transposed direct form II state */
} bf_biquad;

typedef struct {
    double sample_rate;

    /* Last applied settings, after clamping. */
    double low_hz;
    double high_hz;
    bf_mode mode;
    int configured;

    bf_biquad chain[BF_SECTIONS];
} bf_state;

/* Prepare a filter for the given sample rate. Does not set the band. */
void bf_init(bf_state *s, double sample_rate);

/* Apply settings. Cheap to call every block: coefficients are only
 * recomputed when something changed. Values are clamped to
 * [1 Hz, 0.45 * sample_rate], low/high are swapped if reversed, and the
 * band is widened to at least BF_MIN_RATIO (high/low) if narrower.
 * Changing mode clears the filter memory. */
void bf_set(bf_state *s, double low_hz, double high_hz, int mode);

/* Clear filter memory (call on transport start/seek). */
void bf_reset(bf_state *s);

/* Filter n samples. in and out may be the same buffer. */
void bf_process(bf_state *s, const float *in, float *out, unsigned long n);

#ifdef __cplusplus
}
#endif

#endif
