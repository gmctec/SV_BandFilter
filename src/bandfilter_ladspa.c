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
 * LADSPA wrapper. Thin by design: all DSP lives in bandfilter_core.c.
 *
 * The plugin is mono. Sonic Visualiser creates one instance per channel
 * for single-input LADSPA plugins, so stereo and multichannel audio are
 * handled without a separate stereo plugin.
 */

#include "bandfilter_core.h"
#include "ladspa.h"

#include <stdlib.h>

/* LADSPA IDs were once assigned centrally; that registry is no longer
 * run. Sonic Visualiser identifies LADSPA plugins by library file name
 * and label, so the ID only needs to be unlikely to collide in hosts
 * that key on it. */
#define BF_UNIQUE_ID 4719

enum {
    PORT_INPUT = 0,
    PORT_OUTPUT,
    PORT_LOW_HZ,
    PORT_HIGH_HZ,
    PORT_REMOVE_BAND,
    PORT_COUNT
};

typedef struct {
    bf_state filter;
    LADSPA_Data *ports[PORT_COUNT];
} bf_instance;

static LADSPA_Handle instantiate(const LADSPA_Descriptor *d,
                                 unsigned long sample_rate)
{
    bf_instance *p = (bf_instance *)calloc(1, sizeof(bf_instance));
    (void)d;
    if (!p) return NULL;
    bf_init(&p->filter, (double)sample_rate);
    return p;
}

static void connect_port(LADSPA_Handle h, unsigned long port,
                         LADSPA_Data *data)
{
    bf_instance *p = (bf_instance *)h;
    if (port < PORT_COUNT) p->ports[port] = data;
}

static void activate(LADSPA_Handle h)
{
    bf_reset(&((bf_instance *)h)->filter);
}

static void run(LADSPA_Handle h, unsigned long n)
{
    bf_instance *p = (bf_instance *)h;
    const float low  = p->ports[PORT_LOW_HZ]      ? *p->ports[PORT_LOW_HZ]      : 112.0f;
    const float high = p->ports[PORT_HIGH_HZ]     ? *p->ports[PORT_HIGH_HZ]     : 3557.0f;
    const float mode = p->ports[PORT_REMOVE_BAND] ? *p->ports[PORT_REMOVE_BAND] : 0.0f;

    if (!p->ports[PORT_INPUT] || !p->ports[PORT_OUTPUT]) return;

    bf_set(&p->filter, low, high, mode > 0.5f);
    bf_process(&p->filter, p->ports[PORT_INPUT], p->ports[PORT_OUTPUT], n);
}

static void cleanup(LADSPA_Handle h)
{
    free(h);
}

static const LADSPA_PortDescriptor port_descriptors[PORT_COUNT] = {
    LADSPA_PORT_INPUT  | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL,
    LADSPA_PORT_INPUT  | LADSPA_PORT_CONTROL
};

static const char * const port_names[PORT_COUNT] = {
    "Input",
    "Output",
    "Low frequency (Hz)",
    "High frequency (Hz)",
    "Remove band (off = keep band)"
};

/* Frequency defaults on a 20 Hz - 20 kHz log scale:
 * DEFAULT_LOW  ~= 112 Hz, DEFAULT_HIGH ~= 3557 Hz. */
static const LADSPA_PortRangeHint port_hints[PORT_COUNT] = {
    { 0, 0.0f, 0.0f },
    { 0, 0.0f, 0.0f },
    { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
      LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_LOW, 20.0f, 20000.0f },
    { LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE |
      LADSPA_HINT_LOGARITHMIC | LADSPA_HINT_DEFAULT_HIGH, 20.0f, 20000.0f },
    { LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0, 0.0f, 0.0f }
};

static const LADSPA_Descriptor descriptor = {
    BF_UNIQUE_ID,
    "sv_bandfilter",
    LADSPA_PROPERTY_HARD_RT_CAPABLE,
    "Band Filter (keep or remove a frequency range)",
    "gmctec",
    "GPL-2.0-or-later",
    PORT_COUNT,
    port_descriptors,
    port_names,
    port_hints,
    NULL,           /* ImplementationData */
    instantiate,
    connect_port,
    activate,
    run,
    NULL,           /* run_adding */
    NULL,           /* set_run_adding_gain */
    NULL,           /* deactivate */
    cleanup
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    return index == 0 ? &descriptor : NULL;
}
