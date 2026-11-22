/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SV_BandFilter - Copyright (C) 2026 gmctec
 *
 * Response test. Uses only the LADSPA descriptor, the same way a host
 * such as Sonic Visualiser does: instantiate, connect ports, activate,
 * run in blocks. Feeds steady sine tones and checks the gain in dB.
 */

#include "ladspa.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BLOCK 1024

static int failures = 0;

typedef struct {
    const LADSPA_Descriptor *d;
    LADSPA_Handle h;
    LADSPA_Data low, high, remove;
} host_plugin;

static void open_plugin(host_plugin *p, unsigned long fs,
                        float low, float high, float remove)
{
    p->d = ladspa_descriptor(0);
    p->h = p->d->instantiate(p->d, fs);
    p->low = low;
    p->high = high;
    p->remove = remove;
    p->d->connect_port(p->h, 2, &p->low);
    p->d->connect_port(p->h, 3, &p->high);
    p->d->connect_port(p->h, 4, &p->remove);
    if (p->d->activate) p->d->activate(p->h);
}

static void close_plugin(host_plugin *p)
{
    p->d->cleanup(p->h);
}

/* Run a sine through the plugin; return output/input RMS in dB,
 * measured after the first 0.5 s so the filter has settled. */
static double gain_db(host_plugin *p, unsigned long fs, double freq,
                      int in_place)
{
    const unsigned long total = 2 * fs, skip = fs / 2;
    float in[BLOCK], out[BLOCK];
    double e_in = 0.0, e_out = 0.0;
    unsigned long done = 0, i;

    p->d->activate(p->h);

    while (done < total) {
        unsigned long n = total - done < BLOCK ? total - done : BLOCK;
        float *dst = in_place ? in : out;
        for (i = 0; i < n; ++i) {
            in[i] = (float)(0.5 * sin(2.0 * M_PI * freq * (double)(done + i) / fs));
        }
        for (i = 0; i < n; ++i) {
            if (done + i >= skip) e_in += (double)in[i] * in[i];
        }
        p->d->connect_port(p->h, 0, in);
        p->d->connect_port(p->h, 1, dst);
        p->d->run(p->h, n);
        for (i = 0; i < n; ++i) {
            if (done + i >= skip) e_out += (double)dst[i] * dst[i];
        }
        done += n;
    }
    if (e_out <= 0.0) return -300.0;
    return 10.0 * log10(e_out / e_in);
}

static void expect(const char *what, double got, double lo, double hi)
{
    const int ok = got >= lo && got <= hi;
    printf("%-4s %-52s %8.2f dB  (want %.1f .. %.1f)\n",
           ok ? "ok" : "FAIL", what, got, lo, hi);
    if (!ok) ++failures;
}

static void band_tests(unsigned long fs)
{
    host_plugin p;
    char label[96];

    printf("-- sample rate %lu, band 300..3000 Hz\n", fs);

    open_plugin(&p, fs, 300.0f, 3000.0f, 0.0f);
    expect("keep:   1000 Hz (inside)", gain_db(&p, fs, 1000.0, 0), -0.5, 0.5);
    expect("keep:    300 Hz (low edge, -3 dB point)", gain_db(&p, fs, 300.0, 0), -4.0, -2.0);
    expect("keep:   3000 Hz (high edge, -3 dB point)", gain_db(&p, fs, 3000.0, 0), -4.0, -2.0);
    expect("keep:     50 Hz (outside)", gain_db(&p, fs, 50.0, 0), -300.0, -40.0);
    expect("keep:  12000 Hz (outside)", gain_db(&p, fs, 12000.0, 0), -300.0, -40.0);
    expect("keep:   1000 Hz in-place buffers", gain_db(&p, fs, 1000.0, 1), -0.5, 0.5);
    close_plugin(&p);

    open_plugin(&p, fs, 300.0f, 3000.0f, 1.0f);
    expect("remove: 1000 Hz (inside)", gain_db(&p, fs, 1000.0, 0), -300.0, -40.0);
    expect("remove:   50 Hz (outside)", gain_db(&p, fs, 50.0, 0), -0.5, 0.5);
    expect("remove:12000 Hz (outside)", gain_db(&p, fs, 12000.0, 0), -0.5, 0.5);
    close_plugin(&p);

    /* Reversed range behaves as if given the right way round. */
    open_plugin(&p, fs, 3000.0f, 300.0f, 0.0f);
    expect("keep, low/high reversed: 1000 Hz", gain_db(&p, fs, 1000.0, 0), -0.5, 0.5);
    expect("keep, low/high reversed:   50 Hz", gain_db(&p, fs, 50.0, 0), -300.0, -40.0);
    close_plugin(&p);

    /* Mode toggled on a live instance. */
    open_plugin(&p, fs, 300.0f, 3000.0f, 0.0f);
    (void)gain_db(&p, fs, 1000.0, 0);
    p.remove = 1.0f;
    expect("mode switched keep -> remove: 1000 Hz", gain_db(&p, fs, 1000.0, 0), -300.0, -40.0);
    close_plugin(&p);

    /* Upper bound above Nyquist is clamped, not unstable. */
    open_plugin(&p, fs, 20.0f, 20000.0f, 0.0f);
    snprintf(label, sizeof label, "keep 20..20000 (max %lu Hz): 1000 Hz", fs * 45 / 100);
    expect(label, gain_db(&p, fs, 1000.0, 0), -0.5, 0.5);
    close_plugin(&p);
}

/* Narrow bands. Regression guard: an earlier design summed a low-pass
 * and a high-pass for "remove", which failed to remove narrow bands. */
static void narrow_tests(unsigned long fs)
{
    static const double ratio[] = { 2.0, 1.26, 1.12, 1.0595 };
    static const char * const name[] = { "1 octave", "1/3 octave", "1/6 octave", "1 semitone" };
    char label[96];
    host_plugin p;
    int k;

    printf("-- narrow bands from 1000 Hz, sample rate %lu\n", fs);
    for (k = 0; k < 4; ++k) {
        const float lo = 1000.0f, hi = (float)(1000.0 * ratio[k]);
        const double centre = sqrt(lo * hi);

        open_plugin(&p, fs, lo, hi, 0.0f);
        snprintf(label, sizeof label, "keep %-11s centre", name[k]);
        expect(label, gain_db(&p, fs, centre, 0), -0.5, 0.5);
        snprintf(label, sizeof label, "keep %-11s low edge", name[k]);
        expect(label, gain_db(&p, fs, lo, 0), -4.0, -2.0);
        close_plugin(&p);

        open_plugin(&p, fs, lo, hi, 1.0f);
        snprintf(label, sizeof label, "remove %-11s centre", name[k]);
        expect(label, gain_db(&p, fs, centre, 0), -300.0, -40.0);
        snprintf(label, sizeof label, "remove %-11s at 500 Hz", name[k]);
        expect(label, gain_db(&p, fs, 500.0, 0), -0.5, 0.5);
        close_plugin(&p);
    }
}

/* Stress: very low band at a high sample rate (poles near z = 1), and a
 * zero-width band (widened internally to a 1% band). */
static void stress_tests(void)
{
    host_plugin p;
    printf("-- stress\n");
    open_plugin(&p, 96000, 20.0f, 40.0f, 0.0f);
    expect("keep 20..40 Hz at 96 kHz: 28 Hz", gain_db(&p, 96000, 28.0, 0), -0.5, 0.5);
    expect("keep 20..40 Hz at 96 kHz: 1000 Hz", gain_db(&p, 96000, 1000.0, 0), -300.0, -40.0);
    close_plugin(&p);
    open_plugin(&p, 44100, 1000.0f, 1000.0f, 1.0f);
    expect("remove 1000..1000 Hz (widened): 1005 Hz", gain_db(&p, 44100, 1005.0, 0), -300.0, -20.0);
    expect("remove 1000..1000 Hz (widened):  500 Hz", gain_db(&p, 44100, 500.0, 0), -0.5, 0.5);
    close_plugin(&p);
}

/* Two instances with different settings must not share state:
 * this is what several filters in one session look like to the plugin. */
static void independence_test(unsigned long fs)
{
    host_plugin a, b;
    printf("-- two independent instances\n");
    open_plugin(&a, fs, 300.0f, 3000.0f, 0.0f);
    open_plugin(&b, fs, 300.0f, 3000.0f, 1.0f);
    expect("instance A keep:   1000 Hz", gain_db(&a, fs, 1000.0, 0), -0.5, 0.5);
    expect("instance B remove: 1000 Hz", gain_db(&b, fs, 1000.0, 0), -300.0, -40.0);
    expect("instance A again:  1000 Hz", gain_db(&a, fs, 1000.0, 0), -0.5, 0.5);
    close_plugin(&a);
    close_plugin(&b);
}

int main(void)
{
    const LADSPA_Descriptor *d = ladspa_descriptor(0);
    if (!d || ladspa_descriptor(1) != NULL) {
        printf("FAIL descriptor enumeration\n");
        return 1;
    }
    printf("plugin: %s [%s] id %lu\n", d->Name, d->Label, d->UniqueID);

    band_tests(44100);
    band_tests(48000);
    band_tests(96000);
    narrow_tests(44100);
    narrow_tests(48000);
    stress_tests();
    independence_test(44100);

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
