#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>   

#define NUM_BANDS 3
#define SAMPLE_RATE 16000

typedef struct {
    float b0, b1, b2;   // feedforward coefficients
    float a1, a2;       // feedback coefficients
    float x1, x2;       // input delays
    float y1, y2;       // output delays
} biquad_t;

static inline float biquad_process(biquad_t *q, float x)
{
    float y = q->b0 * x
            + q->b1 * q->x1
            + q->b2 * q->x2
            - q->a1 * q->y1
            - q->a2 * q->y2;

    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;

    return y;
}



typedef struct {
    float alpha;     
    float env;        
    float threshold;  
    float max_gain;     
    float gain; 
} band_agc_t;

static inline float apply_band_agc(band_agc_t *b, float sample)
{
    b->env = b->alpha * b->env + (1.0f - b->alpha) * fabsf(sample);

    float ratio = b->env / b->threshold;
    float target_gain;

    if(ratio >= 1.0f){
        
        target_gain =
            1.0f + (b->max_gain - 1.0f) * powf(ratio, 0.6f);
    } else {

        target_gain = powf(ratio, 4.0f);
    }

    if(target_gain > b->max_gain) target_gain = b->max_gain;
    if(target_gain < 0.03f) target_gain = 0.03f;

    float attack  = 0.03f;
    float release = 0.001f;
    float c = (target_gain > b->gain) ? attack : release;
    b->gain += c * (target_gain - b->gain);

    return sample * b->gain;
}



// Band 0: 0-1kHz, Band 1: 1-3kHz, Band 2: 3-6kHz
biquad_t bands[NUM_BANDS*2]; 
band_agc_t band_ctrl[NUM_BANDS];


void init_bands()
{
    // Band 0: LP 1kHz
    bands[0] = (biquad_t){0.2929f, 0.5858f, 0.2929f, -0.0000f, 0.1716f,0,0,0,0};
    bands[1] = (biquad_t){0.2929f, 0.5858f, 0.2929f, -0.0000f, 0.1716f,0,0,0,0};

    // Band 1: BP 1-3kHz
    bands[2] = (biquad_t){0.2066f,0,-0.2066f,-0.0000f,0.5868f,0,0,0,0};
    bands[3] = (biquad_t){0.2066f,0,-0.2066f,-0.0000f,0.5868f,0,0,0,0};

    // Band 2: BP 3-6kHz
    bands[4] = (biquad_t){0.2066f,0,-0.2066f,-0.0000f,0.5868f,0,0,0,0};
    bands[5] = (biquad_t){0.2066f,0,-0.2066f,-0.0000f,0.5868f,0,0,0,0};

    // Band AGC/VAD
    for(int i=0;i<NUM_BANDS;i++){
        band_ctrl[i].alpha = 0.995f;
        band_ctrl[i].env = 0.0f;
        band_ctrl[i].threshold = 0.1f;    // tweak per test
        band_ctrl[i].max_gain = 10.0f;   // max amplification for quiet speech
        band_ctrl[i].gain = 1.0f;
    }
}


float process_sample(float x)
{
    float band_samples[NUM_BANDS];

    // Filter per band
    for(int b=0;b<NUM_BANDS;b++){
        float y = x;
        y = biquad_process(&bands[b*2], y);
        y = biquad_process(&bands[b*2+1], y);
        band_samples[b] = apply_band_agc(&band_ctrl[b], y);
    }

    // Mix bands back
    float out = 0;
    for(int b=0;b<NUM_BANDS;b++){
        out += band_samples[b];
    }
    out /= NUM_BANDS; // normalize

    // final soft clip
    if(out>1.0f) out=1.0f;
    if(out<-1.0f) out=-1.0f;

    return out;
}


int main(int argc,char **argv)
{
    if(argc<3){ printf("Usage: %s input.raw output.raw\n",argv[0]); return 1; }
    FILE *fin=fopen(argv[1],"rb"); if(!fin){ perror("input"); return 1; }
    FILE *fout=fopen(argv[2],"wb"); if(!fout){ perror("output"); fclose(fin); return 1; }

    init_bands();

    int16_t sample_in;
    while(fread(&sample_in,sizeof(int16_t),1,fin)==1){
        float x = sample_in/32768.0f;
        float y = process_sample(x);
        int16_t out = (int16_t)(y*32767.0f);
        fwrite(&out,sizeof(int16_t),1,fout);
    }

    fclose(fin); fclose(fout);
    printf("Processing done: %s -> %s\n",argv[1],argv[2]);
    return 0;
}
