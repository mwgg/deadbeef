/*
    DeaDBeeF - ultimate music player for GNU/Linux systems with X11
    Copyright (C) 2009-2010 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "deadbeef.h"
#include "premix.h"
#include "optmath.h"

#define trace(...) { fprintf(stderr, __VA_ARGS__); }
//#define trace(fmt,...)


static inline void
pcm_write_samples_8_to_8 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                *(output + (outputfmt->bps >> 3) * channelmap[c]) = *input;
            }
            input++;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_8_to_16 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                *((int16_t*)(output + (outputfmt->bps >> 3) * channelmap[c])) = (int16_t)(*input) << 8;
            }
            input++;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_8_to_24 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                char *out = output + (outputfmt->bps >> 3) * channelmap[c];
                out[0] = 0;
                out[1] = 0;
                out[2] = input[0];
            }
            input += 1;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_8_to_32 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                char *out = output + (outputfmt->bps >> 3) * channelmap[c];
                out[0] = 0;
                out[1] = 0;
                out[2] = 0;
                out[3] = input[0];
            }
            input += 1;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_8_to_float (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                float sample = (*input) / (float)0x7f;
                *((float *)(output + (outputfmt->bps >> 3) * channelmap[c])) = sample;
            }
            input++;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_16_to_16 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                *((int16_t*)(output + (outputfmt->bps >> 3) * channelmap[c])) = *((int16_t*)input);
            }
            input += 2;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_16_to_8 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                *((int8_t*)(output + (outputfmt->bps >> 3) * channelmap[c])) = *((int16_t*)input) >> 8;
            }
            input += 2;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_16_to_24 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                char *out = output + (outputfmt->bps >> 3) * channelmap[c];
                out[0] = 0;
                out[1] = input[0];
                out[2] = input[1];
            }
            input += 2;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_16_to_32 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                char *out = output + (outputfmt->bps >> 3) * channelmap[c];
                out[0] = 0;
                out[1] = 0;
                out[2] = input[0];
                out[3] = input[1];
            }
            input += 2;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_16_to_float (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                float sample = (*((int16_t*)input)) / (float)0x7fff;
                *((float *)(output + (outputfmt->bps >> 3) * channelmap[c])) = sample;
            }
            input += 2;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_32_to_32 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                *((int32_t*)(output + (outputfmt->bps >> 3) * channelmap[c])) = *((int32_t*)input);
            }
            input += 4;
        }
        output += outputsamplesize;
    }
}

static inline void
pcm_write_samples_24_to_24 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                char *out = output + (outputfmt->bps >> 3) * channelmap[c];
                out[0] = input[0];
                out[1] = input[1];
                out[2] = input[2];
            }
            input += 3;
        }
        output += outputsamplesize;
    }
}
static inline void
pcm_write_samples_float_to_16 (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int nsamples, int * restrict channelmap, int outputsamplesize) {
    fpu_control ctl;
    fpu_setround (&ctl);
    for (int s = 0; s < nsamples; s++) {
        for (int c = 0; c < inputfmt->channels; c++) {
            if (channelmap[c] != -1) {
                int16_t *out = (int16_t*)(output + (outputfmt->bps >> 3) * channelmap[c]);
                float sample = *((float*)input);
                *out = (int16_t)ftoi (sample*0x7fff);
            }
            input += 4;
        }
        output += outputsamplesize;
    }
    fpu_restore (ctl);
}

int
pcm_convert (const ddb_waveformat_t * restrict inputfmt, const char * restrict input, const ddb_waveformat_t * restrict outputfmt, char * restrict output, int inputsize) {
    // calculate output size
    int inputsamplesize = (inputfmt->bps >> 3) * inputfmt->channels;
    int outputsamplesize = (outputfmt->bps >> 3) * outputfmt->channels;
    int nsamples = inputsize / inputsamplesize;

    uint32_t outchannels = 0;

    if (output) {
        // build channelmap
        int channelmap[32] = {-1};
        uint32_t inputbitmask = 1;
        for (int i = 0; i < inputfmt->channels; i++) {
            // find next input channel
            while (inputbitmask < 0x80000000 && !(inputfmt->channelmask & inputbitmask)) {
                inputbitmask <<= 1;
            }
            if (!(inputfmt->channelmask & inputbitmask)) {
                trace ("pcm_convert: channelmask doesn't correspond inputfmt (channels=%d, channelmask=%X)!\n", inputfmt->channels, inputfmt->channelmask);
                break;
            }
            if (outputfmt->channelmask & inputbitmask) {
                int o = 0;
                uint32_t outputbitmask = 1;
                while (outputbitmask < 0x80000000 && (outputfmt->channelmask & outputbitmask) != inputbitmask) {
                    outputbitmask <<= 1;
                    o++;
                }
                if (!(inputfmt->channelmask & outputbitmask)) {
                    // no corresponding output channel -- ignore
                    continue;
                }
                outchannels |= outputbitmask;
                channelmap[i] = o; // input channel i going to output channel o
                //trace ("channelmap[%d]=%d\n", i, o);
            }
            inputbitmask <<= 1;
        }

        if (outchannels != outputfmt->channelmask) {
            // some of the channels are not used
            memset (output, 0, nsamples * outputsamplesize);
        }

        // FIXME: access through function pointer table
        //trace ("converting from %d to %d\n", inputfmt->bps, outputfmt->bps);
        if (inputfmt->bps == 8 && outputfmt->bps == 8) {
            pcm_write_samples_8_to_8 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 8 && outputfmt->bps == 16) {
            pcm_write_samples_8_to_16 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 8 && outputfmt->bps == 24) {
            pcm_write_samples_8_to_24 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 8 && outputfmt->bps == 32 && !outputfmt->is_float) {
            pcm_write_samples_8_to_32 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 8 && outputfmt->bps == 32 && outputfmt->is_float) {
            pcm_write_samples_8_to_float (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 16 && outputfmt->bps == 8) {
            pcm_write_samples_16_to_8 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 16 && outputfmt->bps == 16) {
            pcm_write_samples_16_to_16 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 16 && outputfmt->bps == 24) {
            pcm_write_samples_16_to_24 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 16 && outputfmt->bps == 32 && !outputfmt->is_float) {
            pcm_write_samples_16_to_32 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 16 && outputfmt->bps == 32 && outputfmt->is_float) {
            pcm_write_samples_16_to_float (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 24 && outputfmt->bps == 24) {
            pcm_write_samples_24_to_24 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->bps == 32 && outputfmt->bps == 32) {
            pcm_write_samples_32_to_32 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else if (inputfmt->is_float && inputfmt->bps == 32 && outputfmt->bps == 16) {
            pcm_write_samples_float_to_16 (inputfmt, input, outputfmt, output, nsamples, channelmap, outputsamplesize);
        }
        else {
            trace ("no converter from %d to %d\n", inputfmt->bps, outputfmt->bps);
        }
    }
    return nsamples * outputsamplesize;
}

