/* SPDX-License-Identifier: MIT
 * Copyright (c) 2014 Emilie Gillet (SVF), 2026 M-VAVE (math helpers).
 * See THIRD_PARTY.md for the MIT terms. */
#ifndef FM1_DSP_MATH_H
#define FM1_DSP_MATH_H
#include "engine.h"
#include <string.h>
#define DSP_PI 3.14159265358979323846f
static inline float dsp_abs(float x) { return x < 0 ? -x : x; }
static inline float dsp_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
static inline float dsp_min(float a,float b) { return a<b?a:b; }
static inline float dsp_max(float a,float b) { return a>b?a:b; }
/* Taylor expansion after reduction to a fractional exponent in [-0.5,0.5]. */
static FM1_DSP_CODE inline float dsp_exp2(float x) {
    x=dsp_clamp(x,-120.0f,120.0f);
    int n=(int)(x+(x<0?-0.5f:0.5f));
    float a=(x-(float)n)*0.6931471805599453f;
    float p=1.0f+a*(1.0f+a*(0.5f+a*(1.0f/6.0f+a*(1.0f/24.0f+a*(1.0f/120.0f+a/720.0f)))));
    uint32_t bits=(uint32_t)(n+127)<<23;float scale;memcpy(&scale,&bits,sizeof scale);
    return p*scale;
}
static inline float dsp_exp(float x) { return dsp_exp2(x*1.4426950408889634f); }
static inline float dsp_decay(float seconds) { return dsp_exp(-1.0f/(44100.0f*dsp_max(seconds,0.00001f))); }
static FM1_DSP_CODE inline float dsp_sin(float x) {
    x*=1.0f/(2.0f*DSP_PI); x-=(float)(int)x;
    if(x>.5f)x-=1; if(x<-.5f)x+=1;
    float a=x*(2.0f*DSP_PI);
    if(a>DSP_PI*.5f)a=DSP_PI-a; if(a<-DSP_PI*.5f)a=-DSP_PI-a;
    float q=a*a;
    return a*(1.0f+q*(-1.0f/6.0f+q*(1.0f/120.0f+q*(-1.0f/5040.0f+q*(1.0f/362880.0f-q/39916800.0f)))));
}
static inline float dsp_cos(float x) {return dsp_sin(x+DSP_PI*.5f);}
static inline float dsp_tan(float x) {return dsp_sin(x)/dsp_cos(x);}
static FM1_DSP_CODE inline float dsp_sqrt(float x) {
    if(x<=0)return 0;
    uint32_t bits;memcpy(&bits,&x,4);bits=(bits>>1)+0x1fc00000u;
    float y;memcpy(&y,&bits,4);
    for(unsigned i=0;i<4;i++)y=.5f*(y+x/y);
    return y;
}
static FM1_DSP_CODE inline float dsp_log2(float x) {
    x=dsp_max(x,1e-30f);uint32_t bits;memcpy(&bits,&x,4);
    int e=(int)(bits>>23)-127;bits=(bits&0x7fffffu)|0x3f800000u;
    float m;memcpy(&m,&bits,4);
    if(m>1.41421356f){m*=.5f;e++;}
    float z=(m-1)/(m+1),q=z*z;
    return (float)e+2.8853900817779268f*z*(1+q*(1.0f/3+q*(1.0f/5+q*(1.0f/7+q/9))));
}
static inline float dsp_tanh(float x) {
    if(x>=8)return 1;if(x<=-8)return -1;
    float p=dsp_exp2(2.8853900817779268f*x);return (p-1)/(p+1);
}
static inline float dsp_noise(uint32_t *state) {
    uint32_t n=*state;n^=n<<13;n^=n>>17;n^=n<<5;*state=n;
    return (float)(n>>8)*(1.0f/8388608.0f)-1.0f;
}
typedef struct {float low,band,g,r,h;} dsp_svf_t;
typedef struct {float z1,z2,b0,a1,a2;} dsp_lp_t;
/* Fixed low-pass sections use transposed direct form: three multiplies/sample.
 * Keep the state-variable form below for modulated or very low frequencies. */
static inline void dsp_lp_set(dsp_lp_t *s,float hz,float q) {
    float g=dsp_tan(DSP_PI*hz/44100),r=1/q,h=1/(1+r*g+g*g);
    s->b0=g*g*h;s->a1=2*(g*g-1)*h;s->a2=(1-r*g+g*g)*h;
}
static inline float dsp_lp(dsp_lp_t *s,float x) {
    float v=s->b0*x,y=v+s->z1;s->z1=2*v-s->a1*y+s->z2;s->z2=v-s->a2*y;return y;
}
/* Trapezoidal SVF as in MIT stmlib/Plaits, the DaisySP drum model source. */
static inline void dsp_svf_set(dsp_svf_t *s,float hz,float q) {
    s->g=dsp_tan(DSP_PI*dsp_clamp(hz,1.0f,18000.0f)/44100.0f);
    s->r=1.0f/dsp_max(q,.1f);s->h=1.0f/(1.0f+s->r*s->g+s->g*s->g);
}
static inline float dsp_svf(dsp_svf_t *s,float x,unsigned mode) {
    float hp=(x-(s->r+s->g)*s->band-s->low)*s->h;
    float bp=s->g*hp+s->band;s->band=bp+s->g*hp;
    float lp=s->g*bp+s->low;s->low=lp+s->g*bp;
    return mode==0?lp:mode==1?bp:mode==2?hp:lp+hp;
}
typedef struct {float x,y,a;} dsp_hp_t;
static inline void dsp_hp_set(dsp_hp_t *s,float hz,float sr) {s->a=dsp_exp(-2*DSP_PI*hz/sr);}
static inline float dsp_hp(dsp_hp_t *s,float x) {float y=.5f*(1+s->a)*(x-s->x)+s->a*s->y;s->x=x;s->y=y;return y;}
#endif
