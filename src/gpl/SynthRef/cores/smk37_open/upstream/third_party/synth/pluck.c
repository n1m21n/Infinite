/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet; 2026 M-VAVE.
 * DaisySP String/DelayLine adaptation: fractional delay and per-sample
 * feedback damping, without dispersion; FM1 MIDI and voice handling.
 * See THIRD_PARTY.md for MIT terms and the upstream revision. */
#ifndef FM1_HOST_TEST
#pragma code_seg(".volatile_ram_code")
#pragma const_seg(".volatile_ram.fm1")
#endif
#include "voices.h"
#include "dsp_math.h"
#ifdef FM1_OPEN_WEB
#define fm1_render_guard_take(n) 0
#else
#include "render_guard.h"
#endif
#include "fixed.h"
#include <string.h>
static FM1_DSP_DATA const fm1_param_t parameters[]={
    {"DECAY",127,90,0},{"BRIGHT",127,100,0},{"TUNE",48,24,0},{"RELEASE",127,35,0}};
const fm1_param_t *pluck_param(unsigned p) {return p<PLUCK_PARAMS?parameters+p:0;}
size_t pluck_size(void) {return sizeof(pluck_t);}
unsigned pluck_get(const pluck_t *p,unsigned k) {return k<PLUCK_PARAMS?p->pot[k]:0;}
static FM1_DSP_CODE void retune(pluck_t *p,pluck_voice_t *v) {
    float hz=dsp_clamp(440*dsp_exp2(((int)v->note+(int)p->pot[2]-93+(p->bend+v->tune)/65536.0f)/12),2,11025);
    v->frequency=hz;
    float period=44100/hz;v->rate=dsp_min(1,1020/period);period*=v->rate;
    v->damping=.53f+.02f*dsp_clamp((60.0f-v->note)/24,0,1);
    period-=(1-v->damping)/v->damping;
    v->delay_length=(unsigned)period;v->delay_fraction=period-v->delay_length;
    v->feedback=dsp_exp(-6.907755279f/(hz*(.05f+7.95f*p->pot[0]/127.0f)));
    v->release=dsp_decay((.005f+.995f*p->pot[3]/127.0f)/6.907755279f);
}
void pluck_set(pluck_t *p,unsigned k,unsigned value) {
    if(k>=PLUCK_PARAMS||value>parameters[k].max||p->pot[k]==value)return;
    p->pot[k]=(uint8_t)value;
    /* Brightness shapes the next excitation only. Decay/release edits leave
     * the sounding string's pitch, delay position and filter history alone. */
    if(k==1)return;
    float release=k==3?dsp_decay((.005f+.995f*value/127.0f)/6.907755279f):0;
    for(unsigned i=0;i<PLUCK_VOICES;i++){
        pluck_voice_t *v=p->voice+i;if(v->note==255)continue;
        if(k==0)v->feedback=dsp_exp(-6.907755279f/(v->frequency*(.05f+7.95f*value/127.0f)));
        else if(k==3)v->release=release;
        else retune(p,v);
    }
}
FM1_DSP_CODE void pluck_panic(pluck_t *p) {
    for(unsigned i=0;i<PLUCK_VOICES;i++){p->voice[i].note=255;p->voice[i].gain=0;}
    p->pedal=0;p->bend=0;
}
void pluck_init(pluck_t *p) {
    /* Inactive delay lines are never read. A new note initializes its entire
     * voice, so a project reload need not clear eight 4 KiB strings first. */
    memset(&p->random,0,sizeof *p - offsetof(pluck_t,random));
    p->limit=PLUCK_VOICES;p->random=1;pluck_panic(p);
    for(unsigned i=0;i<PLUCK_PARAMS;i++)p->pot[i]=parameters[i].def;
}
FM1_DSP_CODE unsigned pluck_active(const pluck_t *p) {
    unsigned n=0;for(unsigned i=0;i<PLUCK_VOICES;i++)n+=p->voice[i].note!=255;return n;
}
static unsigned oldest(const pluck_t *p) {
    unsigned selected=PLUCK_VOICES,priority=0;uint32_t longest=0;
    for(unsigned i=0;i<PLUCK_VOICES;i++) {
        const pluck_voice_t *v=p->voice+i;if(v->note==255)continue;
        uint32_t age=p->age-v->age,rank=(!v->down&&!p->pedal?2u:!v->down?1u:0u);
        if(selected==PLUCK_VOICES||rank>priority||(rank==priority&&age>longest)){
            selected=i;priority=rank;longest=age;
        }
    }
    return selected;
}
FM1_DSP_CODE unsigned pluck_limit(pluck_t *p,unsigned limit) {
    if(!limit||limit>PLUCK_VOICES)return 0;p->limit=(uint8_t)limit;
    unsigned retired=0;while(pluck_active(p)>limit){unsigned i=oldest(p);p->voice[i].note=255;retired++;}return retired;
}
FM1_DSP_CODE void pluck_note(pluck_t *p,unsigned note,unsigned velocity,int cents) {
    if(note>127||velocity>127)return;
    if(!velocity){for(unsigned i=0;i<PLUCK_VOICES;i++)if(p->voice[i].note==note)p->voice[i].down=0;return;}
    unsigned slot=PLUCK_VOICES;
    for(unsigned i=0;i<PLUCK_VOICES;i++)if(p->voice[i].note==note){slot=i;break;}
    if(slot==PLUCK_VOICES&&pluck_active(p)<p->limit)for(unsigned i=0;i<PLUCK_VOICES;i++)if(p->voice[i].note==255){slot=i;break;}
    if(slot==PLUCK_VOICES)slot=oldest(p);
    pluck_voice_t *v=p->voice+slot;
    unsigned stolen=v->note!=255;int32_t tail=stolen?v->last:0;
    memset(&v->delay_fraction,0,sizeof *v - offsetof(pluck_voice_t,delay_fraction));
    v->tail=tail;v->fade=stolen?32:0;
    v->note=(uint8_t)note;v->tune=cents*16384/25;v->down=1;v->age=++p->age;
    v->gain=.25f*velocity/127*(1+.04f*p->pot[1]/127*(1+dsp_clamp((60.0f-note)/24,0,1)));
    retune(p,v);v->position=0;v->clock=0;
    unsigned n=(unsigned)v->delay_length+2;
    /* The attack below overwrites [0,n). Clear only the untouched tail; a
     * later pitch bend can read it. Do not clear then refill the same samples. */
    memset(v->delay+n,0,sizeof v->delay - n*sizeof v->delay[0]);
    float brightness=.01f+.99f*p->pot[1]/127.0f,lp=0,sum=0;
    for(unsigned i=0;i<n;i++) {
        p->random=(1103515245u*p->random+12345u)&0x7fffffffu;
        float noise=(float)(p->random>>3)*(1.0f/134217728.0f)-1;
        lp+=brightness*(noise-lp);v->delay[i]=lp;sum+=lp;
    }
    float mean=sum/n;for(unsigned i=0;i<n;i++)v->delay[i]-=mean;
}
FM1_DSP_CODE void pluck_midi(pluck_t *p,const uint8_t m[3]) {
    unsigned type=m[0]&240;
    if(type==0xb0&&m[1]==64)p->pedal=m[2]>=64;
    if(type==0xe0||(type==0xb0&&m[1]==121)) {
        p->bend=type==0xe0?((int)m[1]+128*(int)m[2]-8192)*16:0;
        if(type==0xb0)p->pedal=0;
        for(unsigned i=0;i<PLUCK_VOICES;i++)if(p->voice[i].note!=255)retune(p,p->voice+i);
    }
}
static FM1_DSP_CODE inline float read_string(const pluck_voice_t *v) {
    unsigned index=v->position+v->delay_length;
    float a=v->delay[index&1023],b=v->delay[(index+1)&1023];
    return a+(b-a)*v->delay_fraction;
}
static inline uint32_t peak_bits(uint32_t peak,float sample) {
    uint32_t bits;memcpy(&bits,&sample,4);bits&=0x7fffffffu;return bits>peak?bits:peak;
}
FM1_DSP_CODE void pluck_render(pluck_t *restrict p,int32_t *restrict out,unsigned frames) {
    if(!frames)return;
    memset(out,0,frames*sizeof *out);
    for(unsigned k=0;k<PLUCK_VOICES;k++) {
        pluck_voice_t *v=p->voice+k;if(v->note==255)continue;
        if(fm1_render_guard_take(1)){v->note=255;continue;}
        int32_t before=out[0],end=out[frames-1];
        float release=v->down||p->pedal?1:v->release;
        uint32_t peak=0;
        if(v->rate==1) {
            /* The normal pitch range needs no resampling clock or crossfade.
             * Linear interpolation and damping are convex: each delay sample
             * remains within +/-2; eight voices fit Q24 with ample headroom. */
            float filter=v->filter,gain=v->gain;
            const float fraction=v->delay_fraction,damping=v->damping,feedback=v->feedback;
            const unsigned length=v->delay_length;
            unsigned position=v->position;
            for(unsigned i=0;i<frames;i++) {
                unsigned index=position+length;
                float a=v->delay[index&1023],b=v->delay[(index+1)&1023];
                float x=a+(b-a)*fraction;filter+=damping*(x-filter);
                v->delay[position]=filter*feedback;position=(position-1)&1023;
                out[i]+=(int32_t)(filter*gain*16777216);gain*=release;
                peak=peak_bits(peak,filter);
            }
            v->previous=v->current=v->filter=filter;v->gain=gain;v->position=position;
        } else {
        for(unsigned i=0;i<frames;i++) {
            v->clock+=v->rate;
            if(v->clock>=1) {
                v->clock-=1;float x=read_string(v);
                v->filter+=v->damping*(x-v->filter);
                v->delay[v->position]=v->filter*v->feedback;
                v->position=(v->position-1)&1023;
                v->previous=v->current;v->current=v->filter;
            }
            float sample=v->previous+v->clock*(v->current-v->previous);
            out[i]+=(int32_t)(sample*v->gain*16777216);
            v->gain*=release;
            peak=peak_bits(peak,sample);
        }
        }
        /* Cancel only the boundary step when an active string is replaced.
         * The correction expires in 32 frames; ordinary attacks and the
         * steady delay-line loop stay unchanged. Short render calls retain
         * the remainder, including a second trigger during the correction. */
        if(v->fade){
            if(v->fade==32)v->tail-=out[0]-before;
            for(unsigned i=0;i<frames && v->fade;++i,--v->fade)
                out[i]+=(int32_t)((int64_t)v->tail*v->fade/32);
        }
        v->last=out[frames-1]-end;
        float level;memcpy(&level,&peak,4);
        if(level<1e-5f)v->quiet+=frames;else v->quiet=0;
        if(v->gain<.000001f||v->quiet>22050)v->note=255;
    }
}
