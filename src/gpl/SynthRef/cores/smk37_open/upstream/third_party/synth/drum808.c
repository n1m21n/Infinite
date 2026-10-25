/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet; 2026 M-VAVE.
 * Bass/snare excitation and metallic noise adapted from DaisySP/Plaits.
 * FM1 additions: other voices, parameter storage and routing.
 * See THIRD_PARTY.md for exact revisions and MIT terms. */
#ifndef FM1_HOST_TEST
#pragma code_seg(".volatile_ram_code")
#pragma const_seg(".volatile_ram.fm1")
#endif
#include "drum808_state.h"
#ifdef FM1_OPEN_WEB
#define fm1_render_guard_take(n) 0
#else
#include "render_guard.h"
#endif
#include <string.h>
static const char *const dist_names[]={"DIODE","CLIP","SAT","BFZ","PDIST","FOLD","CRUSH"};
static const char *const tom_names[]={"TOM","CONGA"},*const rim_names[]={"RIM","CLAVE"};
static const char *const clap_names[]={"CLAP","MARAC"},*const choke_names[]={"OFF","CH>OH","BOTH"};
#define P(n,d) {n,127,d,0}
#define DR P("Drive",0),{"Dist",6,0,dist_names}
#define SEND P("Rev",0),P("Dly",0),P("Pan",64)
static const fm1_param_t bd_params[]={P("Level",64),P("Tone",42),P("Decay",87),P("Tune",64),P("Attack",24),DR,P("Pan",64)};
static const fm1_param_t sd_params[]={P("Level",64),P("Tone",64),P("Snappy",89),P("Tune",64),P("Decay",108),DR,SEND};
static const fm1_param_t lt_params[]={P("Level",64),P("Tune",64),P("Decay",68),{"Sound",1,0,tom_names},DR,SEND};
static const fm1_param_t mt_params[]={P("Level",64),P("Tune",64),P("Decay",56),{"Sound",1,0,tom_names},DR,SEND};
static const fm1_param_t ht_params[]={P("Level",64),P("Tune",64),P("Decay",53),{"Sound",1,0,tom_names},DR,SEND};
static const fm1_param_t rs_params[]={P("Level",64),P("Tune",64),P("Decay",40),{"Sound",1,0,rim_names},DR,SEND};
static const fm1_param_t cp_params[]={P("Level",64),P("Tune",64),P("Decay",64),P("Attack",32),{"Sound",1,0,clap_names},DR,SEND};
static const fm1_param_t cb_params[]={P("Level",64),P("Tune",64),P("Decay",62),DR,SEND};
static const fm1_param_t cy_params[]={P("Level",64),P("Decay",70),P("Tune",64),DR,SEND};
static const fm1_param_t oh_params[]={P("Level",64),P("Decay",74),P("Tune",64),DR,SEND};
static const fm1_param_t ch_params[]={P("Level",64),P("Tune",64),P("Decay",47),DR,SEND};
static const fm1_param_t kit_params[]={P("Level",100),P("Accent",127),{"Choke",2,1,choke_names}};
static const fm1_param_t *const descriptors[]={bd_params,sd_params,lt_params,mt_params,ht_params,rs_params,cp_params,cb_params,cy_params,oh_params,ch_params,kit_params};
static const uint8_t counts[]={8,10,9,9,9,9,10,8,8,8,8,3};
/* Storage slots are stable because the existing project format stores
 * both sound variants. 9=switch, 10=pan, 11=maraca attack. */
static const uint8_t layout[12][10]={
 {0,7,2,1,8,3,4,10},{0,8,7,1,2,3,4,5,6,10},
 {0,1,2,9,3,4,5,6,10},{0,1,2,9,3,4,5,6,10},{0,1,2,9,3,4,5,6,10},
 {0,1,2,9,3,4,5,6,10},{0,1,2,11,9,3,4,5,6,10},
 {0,1,2,3,4,5,6,10},{0,2,1,3,4,5,6,10},{0,2,1,3,4,5,6,10},{0,1,2,3,4,5,6,10},{0,1,2}};
static const uint8_t selection[11][2]={{0,0},{1,1},{2,5},{3,6},{4,7},{8,9},{11,10},{12,12},{15,15},{14,14},{13,13}};
int drum808_nparams(int part) {return (unsigned)part<=D8_KIT?counts[part]:0;}
const fm1_param_t *drum808_param(int part,int p) {return (unsigned)part<=D8_KIT&&(unsigned)p<counts[part]?descriptors[part]+p:0;}
static d8_voice_t *voice(drum808_t *d,unsigned s) {
    if(s==0)return &d->bd;if(s==1)return &d->sd;
    if(s>=2&&s<=7)return &d->tom[(s-2)%3];
    switch(s){case 8:return &d->rs;case 9:return &d->cl;case 10:return &d->ma;case 11:return &d->cp;case 12:return &d->cb;case 13:return &d->ch;case 14:return &d->oh;default:return &d->cy;}
}
static unsigned part_for(unsigned s) {
    static const uint8_t parts[]={0,1,2,3,4,2,3,4,5,5,6,6,7,10,9,8};return parts[s];
}
static void configure(drum808_t *d,d8_voice_t *v);
void drum808_set(drum808_t *d,int part,int p,int value) {
    const fm1_param_t *desc=drum808_param(part,p);if(!desc)return;
    value=value<0?0:value>desc->max?desc->max:value;
    if(part==D8_KIT){
        d->kpot[p]=(uint8_t)value;
        for(unsigned s=0;s<D8S_NUM;s++) {
            d8_voice_t *v=voice(d,s);
            if(v->active&&v->sound==s)configure(d,v);
        }
        return;
    }
    unsigned slot=layout[part][p],s=selection[part][d->sw[part]&1];
    if(slot==9)d->sw[part]=(uint8_t)value;
    else if(slot==10){
        d->tpan[part]=(uint8_t)value;
        float a=(value-64)*(DSP_PI/256);
        d->pan_l[part]=value==64?1:dsp_cos(a)-dsp_sin(a);
        d->pan_r[part]=value==64?1:dsp_cos(a)+dsp_sin(a);
    }
    else if(slot==11)d->pot[D8S_MA][D8P_X1]=(uint8_t)value;
    else d->pot[s][slot]=(uint8_t)value;
    if(slot<9||slot==11){
        d8_voice_t *v=voice(d,slot==11?D8S_MA:s);
        if(v->active&&v->sound==(slot==11?D8S_MA:s))configure(d,v);
    }
}
int drum808_get(const drum808_t *d,int part,int p) {
    if(!drum808_param(part,p))return 0;if(part==D8_KIT)return d->kpot[p];
    unsigned slot=layout[part][p];
    return slot==9?d->sw[part]:slot==10?d->tpan[part]:slot==11?d->pot[D8S_MA][D8P_X1]:d->pot[selection[part][d->sw[part]&1]][slot];
}
void drum808_pan_settle(drum808_t *d) {memcpy(d->pan_lc,d->pan_l,sizeof d->pan_l);memcpy(d->pan_rc,d->pan_r,sizeof d->pan_r);}
void drum808_init(drum808_t *d) {
    memset(d,0,sizeof *d);
    for(int variant=0;variant<2;variant++) {
        memset(d->sw,variant,sizeof d->sw);
        for(int p=0;p<=D8_KIT;p++)for(int k=0;k<counts[p];k++)if(layout[p][k]!=9)drum808_set(d,p,k,descriptors[p][k].def);
    }
    memset(d->sw,0,sizeof d->sw);drum808_pan_settle(d);
    d->pot[D8S_LC][D8P_DECAY]=58;d->pot[D8S_MC][D8P_DECAY]=36;d->pot[D8S_HC][D8P_DECAY]=34;
    d->pot[D8S_CL][D8P_DECAY]=64;d->pot[D8S_MA][D8P_DECAY]=51;
    for(unsigned s=0;s<D8S_NUM;s++)voice(d,s)->random=0x682f0745u+s*0x7193u;
}
static float diode(float x) {return x>=0?x:1.4f*x/(1+2*dsp_abs(x));}
static float softclip(float x) {x=dsp_clamp(x,-3,3);return x*(27+x*x)/(27+9*x*x);}
static void configure(drum808_t *d,d8_voice_t *v) {
    unsigned s=v->sound;
    const uint8_t *p=d->pot[s];
    static const float frequencies[]={61,175,84,122,175,207,304,407,506,2517,11000,1000,540,414,414,414};
    static const float durations[]={.50f,.27f,.41f,.29f,.27f,.31f,.17f,.16f,.017f,.03f,.04f,.40f,.62f,.07f,.40f,1.53f};
    static const uint8_t defaults[]={87,108,68,56,53,58,36,34,40,64,51,64,62,47,74,70};
    static const float trims[]={1.63f,.74f,.388f,.383f,.394f,.333f,.359f,.354f,.712f,.520f,.555f,2.55f,.925f,1.94f,.866f,.58f};
    float tune=dsp_exp2(((int)p[D8P_TUNE]-64)/(s>=D8S_LT&&s<=D8S_HC?384.0f:64.0f));
    v->frequency=frequencies[s]*tune;
    float seconds=durations[s]*dsp_exp2(((int)p[D8P_DECAY]-defaults[s])/32.0f);
    if(s==D8S_BD)seconds=(.0163f+.00106f*p[D8P_DECAY])*4.605170186f/tune;
    v->decay=dsp_decay(seconds/4.605170186f);v->tail_step=1/(44100*seconds);
    v->transient_decay=.9994962f;
    v->accent=.5f*(1-d->kpot[1]/127.0f)+v->velocity*d->kpot[1]/127.0f;
    float balance=s==D8S_BD?1.2589254f:s==D8S_LT?1.7782794f:s>=D8S_MT&&s<=D8S_HT?1.9952623f:1;
    v->gain=trims[s]*(p[D8P_LEVEL]/64.0f)*(d->kpot[0]/127.0f)*v->accent*balance;
    float drive=p[D8P_DRIVE]/127.0f,group=s<=D8S_HC||s==D8S_CB?1:2;
    float boost=dsp_exp2(5*drive);
    int crush=p[D8P_DIST]==6;
    v->drive=(crush?1:.7f*boost)*trims[s]/group;
    v->drive_norm=group*(p[D8P_LEVEL]/64.0f)*(d->kpot[0]/127.0f)*v->accent/(crush?1:dsp_sqrt(boost))*balance;
    v->crush_step=.04f+.62f*drive*drive;
    v->rev=p[D8P_REV]/127.0f;v->dly=p[D8P_DLY]/127.0f;
    v->tone=p[D8P_X1]/127.0f;v->snappy=p[D8P_X1]/127.0f;v->attack=p[D8P_X2]/127.0f;
    v->q=dsp_max(1,DSP_PI*v->frequency*seconds/4.605170186f);
    v->tone_coefficient=dsp_min(4*v->frequency*dsp_exp2(v->tone*9)/44100,1);
    v->pulse_gain=44.1f/v->frequency;
    dsp_svf_set(&v->filter[0],v->frequency,v->q);
    dsp_svf_set(&v->filter[1],2*v->frequency,s==D8S_BD?.85f:.25f*v->q);
    dsp_svf_set(&v->filter[2],s==D8S_BD?28:s==D8S_SD?6500:s>=D8S_LT&&s<=D8S_HT?4000:8000,.7071f);
    dsp_svf_set(&v->filter[3],s==D8S_CP?1000:10000,.7f);
    static const float ratios[]={1,1.304f,1.466f,1.787f,1.932f,2.536f};
    static const float metal[]={207.0f/414,307.4f/414,370.3f/414,523.8f/414,544.3f/414,807.3f/414};
    for(unsigned j=0;j<6;j++)v->increment[j]=(uint32_t)(dsp_min(v->frequency*(s>=D8S_CH?metal[j]:ratios[j])/44100,.49f)*4294967296.0f);
    if(s==D8S_RS)v->increment[1]=(uint32_t)(v->frequency*3.4f*(4294967296.0f/44100));
    if(s==D8S_CB){v->increment[0]=(uint32_t)(540*tune*(4294967296.0f/44100));v->increment[1]=(uint32_t)(810*tune*(4294967296.0f/44100));dsp_svf_set(&v->filter[0],1600*tune,.8f);}
    if(s>=D8S_CH) {
        dsp_svf_set(&v->filter[2],s==D8S_CY?7100:s==D8S_CH?6000:6200,s==D8S_CY?1.5f:2.0f);
        dsp_svf_set(&v->filter[3],s==D8S_CH?9500:s==D8S_OH?5500:5000,.7071f);
    }
    if(s==D8S_SD){
        float body=(.0118f+.00028f*p[D8P_DECAY])/tune;
        float q=DSP_PI*v->frequency*body;
        dsp_svf_set(&v->filter[0],v->frequency,q);
        dsp_svf_set(&v->filter[1],1.94f*v->frequency,.55f*q);
        v->pulse_gain=dsp_exp2(.9f*dsp_log2(.042f/(body*tune)));
        dsp_svf_set(&v->filter[2],2500,.7071f);
        v->decay=dsp_decay(.02f+.000512f*p[D8P_DECAY]);
    }
    if(s>=D8S_CH)dsp_svf_set(&v->filter[0],3000,.7071f);
    if(s==D8S_CH){v->decay=dsp_decay(seconds*.32f);v->tail_step/=1.12f;}
    if(s==D8S_CY){v->decay=dsp_decay(seconds*.065f);v->tail_step/=1.06f;}
    if(s==D8S_CP){dsp_svf_set(&v->filter[3],900*tune,1.2f);v->decay=dsp_decay(seconds*.09f);v->transient_decay=dsp_decay(seconds*.57f);}
}
void drum808_trigger(drum808_t *d,int part,float velocity) {
    if((unsigned)part>=D8_NUM||!(velocity>0))return;
    unsigned s=selection[part][d->sw[part]&1];d8_voice_t *v=voice(d,s);
    uint32_t rng=v->random;memset(v,0,sizeof *v);v->random=rng?rng:0x716fbc31u;
    v->sound=(uint8_t)s;v->active=1;v->env=1;v->level=1;v->choke=1;v->choke_decay=1;
    v->velocity=dsp_clamp(velocity,0,1);v->pulse_count=44;v->fm_count=265;
    configure(d,v);
    if(d->kpot[2]&&part==D8_CH)d->oh.choke_decay=.94f;
    if(d->kpot[2]==2&&part==D8_OH)d->ch.choke_decay=.94f;
}
static FM1_DSP_CODE inline __attribute__((always_inline)) float percussion(d8_voice_t *v) {
    unsigned s=v->sound;float result=0;
    if(s==D8S_BD) {
        float pulse;
        if(v->pulse_count){--v->pulse_count;pulse=10-(v->pulse_count?0:1);v->pulse=pulse;}
        else {v->pulse*=.886621315f;pulse=v->pulse;}
        v->pulse_lp+=(pulse-v->pulse_lp)*.22675737f;
        pulse=diode(pulse-v->pulse_lp+.044f*pulse);
        float fm=v->fm_count?1:0;if(v->fm_count&&!--v->fm_count)v->retrigger=-.8f;
        if(!v->fm_count)v->retrigger*=.999546485f;
        v->fm_lp+=(fm-v->fm_lp)*.22675737f;
        float f=v->frequency*(.958f+(.053f+.08f*v->accent)*v->env+v->fm_lp*v->attack*2.0f);
        if(!(v->age&3))dsp_svf_set(&v->filter[0],f,v->q);
        float bp=dsp_svf(&v->filter[0],(pulse-.2f*v->retrigger)*v->pulse_gain,1);v->low=v->filter[0].low;
        v->last+=(bp+.08f*(v->tone+.25f)*pulse-v->last)*v->tone_coefficient;
        float body=1.38f*(.5f+.5f*v->level)*dsp_svf(&v->filter[1],dsp_abs(v->last),1);
        result=dsp_svf(&v->filter[2],v->last+body,2);
    } else if(s==D8S_SD) {
        float pulse;if(v->pulse_count){--v->pulse_count;v->pulse=10-(v->pulse_count?0:1);pulse=v->pulse;}else{v->pulse*=.77324263f;pulse=v->pulse;}
        v->pulse_lp+=(pulse-v->pulse_lp)*.75f;
        float a=dsp_svf(&v->filter[0],pulse-v->pulse_lp+.006f*pulse,1);
        float b=dsp_svf(&v->filter[1],.026f*pulse,1);
        float tone=dsp_clamp(v->attack,0,1),shell=softclip((1.5f+4.5f*(1-tone)*(1-tone))*a+(.15f+2*tone)*b);
        float noise=dsp_noise(&v->random);
        result=5.4f*v->pulse_gain*(1-v->snappy)*shell+.40f*v->snappy*dsp_svf(&v->filter[2],noise*v->env,2);
    } else if(s>=D8S_LT&&s<=D8S_HC) {
        float pitch=v->frequency*(1+(s<=D8S_HT?.035f:.008f)*v->level);
        v->phase[0]+=(uint32_t)(pitch*(4294967296.0f/44100));
        float wave=dsp_sin((float)(v->phase[0]>>8)*(2*DSP_PI/16777216));
        result=wave*v->env;
        if(s<=D8S_HT){
            float square=wave*wave,fade=dsp_min(1,(float)v->age/96);
            result+=(.22f*(1-2*square)*(.65f+.35f*v->level)+.14f*wave*(3-4*square)*v->level)*v->env*fade;
        }
        if(s<=D8S_HT)result+=.05f*dsp_svf(&v->filter[2],dsp_noise(&v->random),0)*(v->level*v->level*v->level*v->level*v->level*v->level);
    } else if(s==D8S_RS||s==D8S_CL) {
        v->phase[0]+=v->increment[0];v->phase[1]+=v->increment[1];
        float phase=(float)(v->phase[0]>>8)*(2*DSP_PI/16777216);
        result=(dsp_sin(phase)+(s==D8S_RS?.8f*dsp_sin((float)(v->phase[1]>>8)*(2*DSP_PI/16777216)):0))*v->env;
        if(s==D8S_RS)result+=.16f*dsp_noise(&v->random)*v->env;
    } else if(s==D8S_MA) {
        float attack=dsp_min(1,(float)(v->age+1)/(1+v->tone*100));
        result=dsp_svf(&v->filter[2],dsp_noise(&v->random),2)*v->env*attack;
    }
    else if(s==D8S_CP) {
        if(v->age<1323) {
            if(!(v->age%441))v->pulse=1;
            v->pulse*=.990f;
        } else if(v->age==1323) {v->env=1;v->pulse=0;}
        float e=(v->age>=1323?v->env:0)+v->pulse+.037f*v->level;
        result=dsp_svf(&v->filter[3],dsp_noise(&v->random),1)*e;
    } else if(s==D8S_CB) {
        v->phase[0]+=v->increment[0];v->phase[1]+=v->increment[1];
        float square=(float)(v->phase[0]>>31)+(float)(v->phase[1]>>31)-1;
        result=dsp_svf(&v->filter[0],square,1)*v->env*(.24f+.76f*v->level*v->level*v->level);
    } else {
        unsigned total=0;for(unsigned j=0;j<6;j++){v->phase[j]+=v->increment[j];total+=v->phase[j]>>31;}
        float metal=.33f*total-1;
        float colored=dsp_svf(&v->filter[2],metal,1);
        float e=v->env;
        if(s==D8S_OH){float u=(float)v->age*v->tail_step;e=dsp_max(0,1-u*u*u);e=e*e*dsp_min(1,(float)v->age/400);}
        if(s==D8S_CH){float u=(float)v->age*v->tail_step;e=dsp_max(0,1-u*u*u*u)*v->env;}
        if(s==D8S_CY)e=1.2f*e+.23f*dsp_max(0,1-(float)v->age*v->tail_step);
        float swing=colored*(colored>0?10:.1f);swing=swing/(1+dsp_abs(swing));
        result=dsp_svf(&v->filter[0],dsp_svf(&v->filter[3],(swing+1)*e,2),2);

    }
    v->age++;v->env*=v->decay;v->level*=v->transient_decay;v->choke*=v->choke_decay;
    return result*v->choke;
}
int drum808_active(const drum808_t *d) {return d->bd.active||d->sd.active||d->tom[0].active||d->tom[1].active||d->tom[2].active||d->rs.active||d->cl.active||d->ma.active||d->cp.active||d->cb.active||d->ch.active||d->oh.active||d->cy.active;}
void drum808_advance_idle(drum808_t *d,int n) {
    float c=1-dsp_exp(-(float)n/128);for(unsigned p=0;p<D8_NUM;p++){d->pan_lc[p]+=c*(d->pan_l[p]-d->pan_lc[p]);d->pan_rc[p]+=c*(d->pan_r[p]-d->pan_rc[p]);}
}
static FM1_DSP_CODE void render(drum808_t *d,float *l,float *r,float *rev,float *dly,int count) {
    for(unsigned s=0;s<D8S_NUM;s++) {
        if(s>=D8S_LC&&s<=D8S_HC)continue;
        d8_voice_t *v=voice(d,s);if(!v->active)continue;
        if(fm1_render_guard_take(1)){v->active=0;continue;}
        unsigned part=part_for(v->sound),mode=d->pot[v->sound][D8P_DIST],driving=d->pot[v->sound][D8P_DRIVE];
        float pl=d->pan_lc[part],pr=d->pan_rc[part],tl=d->pan_l[part],tr=d->pan_r[part];
        int pan=r&&(dsp_abs(pl-tl)+dsp_abs(pr-tr)>1e-6f);
        for(int i=0;i<count;i++) {
            float x=percussion(v),a=dsp_abs(x);if(a>v->peak)v->peak=a;
            if(a<dsp_max(.00001f,v->peak*.001f))v->quiet++;else v->quiet=0;
            if((v->quiet>882&&v->age>2205)||v->age>44100u*16u){v->active=0;break;}
            if(driving){
                x*=v->drive;
                switch(mode){
                case 0:x=1.8f*x/(.5f+dsp_abs(x));break;
                case 1:x=dsp_clamp(x,-1,1);break;
                case 3:x=softclip(x*.85f);break;
                case 4:x=1.1f*(softclip(x+.2f)-.197414f);break;
                case 5:x=1.2f*dsp_sin(x);break;
                case 6:{float quantized=(float)(int)(x/v->crush_step+(x<0?-.5f:.5f))*v->crush_step;x=.95f*quantized+.05f*x;break;}
                default:x=softclip(x);break;
                }
                x*=v->drive_norm;
            }else x*=v->gain;
            if(pan){pl+=(tl-pl)*.0078125f;pr+=(tr-pr)*.0078125f;}
            l[i]+=x*(r?pl:1);if(r)r[i]+=x*pr;
            if(v->rev)rev[i]+=x*v->rev;if(v->dly)dly[i]+=x*v->dly;
        }
    }
    for(unsigned p=0;p<D8_NUM;p++) {
        float pl=d->pan_lc[p],pr=d->pan_rc[p],tl=d->pan_l[p],tr=d->pan_r[p];
        if(dsp_abs(pl-tl)+dsp_abs(pr-tr)<=1e-6f)continue;
        for(int i=0;i<count;i++){pl+=(tl-pl)*.0078125f;pr+=(tr-pr)*.0078125f;}
        d->pan_lc[p]=pl;d->pan_rc[p]=pr;
    }
}
void drum808_render(drum808_t *d,float *dry,float *rev,float *dly,int n) {render(d,dry,0,rev,dly,n);}
void drum808_render_st(drum808_t *d,float *l,float *r,float *rev,float *dly,int n) {render(d,l,r,rev,dly,n);}
