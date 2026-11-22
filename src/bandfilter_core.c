/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SV_BandFilter - frequency-range keep/remove filter
 * Copyright (C) 2026 gmctec
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the LICENSE file.
 */

#include "bandfilter_core.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BF_MIN_HZ        1.0
#define BF_MAX_FRACTION  0.45   /* of sample rate; keeps clear of Nyquist */
#define BF_MIN_RATIO     1.01   /* narrowest band: high >= 1.01 * low */
#define BF_DENORMAL      1e-30

/* Minimal complex arithmetic. C99 <complex.h> is not available in MSVC. */
typedef struct { double re, im; } cplx;

static cplx c_make(double re, double im) { cplx c; c.re = re; c.im = im; return c; }
static cplx c_add(cplx a, cplx b) { return c_make(a.re + b.re, a.im + b.im); }
static cplx c_sub(cplx a, cplx b) { return c_make(a.re - b.re, a.im - b.im); }
static cplx c_mul(cplx a, cplx b)
{
    return c_make(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re);
}
static cplx c_scale(cplx a, double k) { return c_make(a.re * k, a.im * k); }
static cplx c_div(cplx a, cplx b)
{
    const double d = b.re * b.re + b.im * b.im;
    return c_make((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d);
}
static double c_abs(cplx a) { return hypot(a.re, a.im); }
static cplx c_sqrt(cplx a)
{
    const double r = c_abs(a);
    const double re = sqrt((r + a.re) / 2.0);
    const double im = sqrt((r - a.re) / 2.0);
    return c_make(re, a.im < 0.0 ? -im : im);
}

/* Bilinear map of an s-plane pole: z = (2fs + s) / (2fs - s). */
static cplx bilinear(cplx s, double fs)
{
    const cplx k = c_make(2.0 * fs, 0.0);
    return c_div(c_add(k, s), c_sub(k, s));
}

/* Set a biquad's denominator from a complex pole z (and its conjugate). */
static void set_poles(bf_biquad *bq, cplx z)
{
    bq->a1 = -2.0 * z.re;
    bq->a2 = z.re * z.re + z.im * z.im;
}

/* |H(e^{jw})| of the whole chain. */
static double chain_gain(const bf_biquad *chain, double w)
{
    const cplx z1 = c_make(cos(w), -sin(w));   /* z^-1 */
    const cplx z2 = c_mul(z1, z1);             /* z^-2 */
    cplx h = c_make(1.0, 0.0);
    int k;
    for (k = 0; k < BF_SECTIONS; ++k) {
        const bf_biquad *b = &chain[k];
        cplx num = c_add(c_add(c_make(b->b0, 0.0), c_scale(z1, b->b1)), c_scale(z2, b->b2));
        cplx den = c_add(c_add(c_make(1.0, 0.0), c_scale(z1, b->a1)), c_scale(z2, b->a2));
        h = c_mul(h, c_div(num, den));
    }
    return c_abs(h);
}

/* Design the full chain. lo < hi, both already clamped. */
static void design(bf_biquad *chain, double fs, double lo, double hi, bf_mode mode)
{
    /* Pre-warped analog edges (rad/s), bandwidth and centre. */
    const double wl = 2.0 * fs * tan(M_PI * lo / fs);
    const double wh = 2.0 * fs * tan(M_PI * hi / fs);
    const double bw = wh - wl;
    const double w0sq = wl * wh;
    const double w0 = sqrt(w0sq);
    /* Digital angle of the centre frequency, for zeros and normalisation. */
    const double theta0 = 2.0 * atan(w0 / (2.0 * fs));
    const int n = BF_PROTO_ORDER;
    int k, sec = 0;
    double g;

    for (k = 0; k < n / 2; ++k) {
        /* Upper-half-plane Butterworth prototype pole. Its conjugate gives
         * the conjugate results, so each root below becomes one biquad. */
        const double ang = M_PI * (2.0 * k + n + 1.0) / (2.0 * n);
        const cplx p = c_make(cos(ang), sin(ang));
        cplx bterm, disc, root, s1, s2;
        int j;

        if (mode == BF_MODE_KEEP_BAND) {
            /* s -> (s^2 + w0^2) / (B s): roots of s^2 - pB s + w0^2 */
            bterm = c_scale(p, bw);
        } else {
            /* s -> B s / (s^2 + w0^2): roots of s^2 - (B/p) s + w0^2 */
            bterm = c_div(c_make(bw, 0.0), p);
        }
        disc = c_sub(c_mul(bterm, bterm), c_make(4.0 * w0sq, 0.0));
        root = c_sqrt(disc);
        s1 = c_scale(c_add(bterm, root), 0.5);
        s2 = c_scale(c_sub(bterm, root), 0.5);

        for (j = 0; j < 2; ++j) {
            bf_biquad *bq = &chain[sec++];
            set_poles(bq, bilinear(j == 0 ? s1 : s2, fs));
            if (mode == BF_MODE_KEEP_BAND) {
                /* one zero at DC, one at Nyquist */
                bq->b0 = 1.0;
                bq->b1 = 0.0;
                bq->b2 = -1.0;
            } else {
                /* zero pair on the unit circle at the centre frequency */
                bq->b0 = 1.0;
                bq->b1 = -2.0 * cos(theta0);
                bq->b2 = 1.0;
            }
        }
    }

    /* Normalise: unity gain at the band centre (keep) or at DC (remove). */
    g = chain_gain(chain, mode == BF_MODE_KEEP_BAND ? theta0 : 0.0);
    if (g > 0.0) {
        const double scale = pow(g, -1.0 / BF_SECTIONS);
        for (k = 0; k < BF_SECTIONS; ++k) {
            chain[k].b0 *= scale;
            chain[k].b1 *= scale;
            chain[k].b2 *= scale;
        }
    }
}

static double clamp_hz(double hz, double fs)
{
    const double max_hz = BF_MAX_FRACTION * fs;
    if (!(hz == hz)) return BF_MIN_HZ;   /* NaN */
    if (hz < BF_MIN_HZ) return BF_MIN_HZ;
    if (hz > max_hz) return max_hz;
    return hz;
}

void bf_init(bf_state *s, double sample_rate)
{
    memset(s, 0, sizeof(*s));
    s->sample_rate = sample_rate > 0.0 ? sample_rate : 44100.0;
}

void bf_reset(bf_state *s)
{
    int k;
    for (k = 0; k < BF_SECTIONS; ++k) {
        s->chain[k].z1 = 0.0;
        s->chain[k].z2 = 0.0;
    }
}

void bf_set(bf_state *s, double low_hz, double high_hz, int mode)
{
    const double fs = s->sample_rate;
    const double max_hz = BF_MAX_FRACTION * fs;
    const bf_mode m = mode ? BF_MODE_REMOVE_BAND : BF_MODE_KEEP_BAND;
    double lo = clamp_hz(low_hz, fs);
    double hi = clamp_hz(high_hz, fs);

    if (lo > hi) {
        const double t = lo;
        lo = hi;
        hi = t;
    }
    if (hi < lo * BF_MIN_RATIO) {
        hi = lo * BF_MIN_RATIO;
        if (hi > max_hz) {
            hi = max_hz;
            lo = hi / BF_MIN_RATIO;
        }
    }

    if (s->configured && lo == s->low_hz && hi == s->high_hz && m == s->mode) {
        return;
    }
    if (s->configured && m != s->mode) {
        bf_reset(s);
    }

    design(s->chain, fs, lo, hi, m);

    s->low_hz = lo;
    s->high_hz = hi;
    s->mode = m;
    s->configured = 1;
}

void bf_process(bf_state *s, const float *in, float *out, unsigned long n)
{
    unsigned long i;
    int k;

    if (!s->configured) {
        if (in != out) memmove(out, in, n * sizeof(float));
        return;
    }

    for (i = 0; i < n; ++i) {
        double x = in[i];
        for (k = 0; k < BF_SECTIONS; ++k) {
            bf_biquad *b = &s->chain[k];
            const double y = b->b0 * x + b->z1;
            b->z1 = b->b1 * x - b->a1 * y + b->z2;
            b->z2 = b->b2 * x - b->a2 * y;
            x = y;
        }
        out[i] = (float)x;
    }

    /* Flush tiny state values so silent tails don't hit slow denormals. */
    for (k = 0; k < BF_SECTIONS; ++k) {
        if (fabs(s->chain[k].z1) < BF_DENORMAL) s->chain[k].z1 = 0.0;
        if (fabs(s->chain[k].z2) < BF_DENORMAL) s->chain[k].z2 = 0.0;
    }
}
