/* SPDX-License-Identifier: GPL-3.0-only */
/* One stereo insert per track, four sections per side (32 logical biquads).
 * The EQ accelerator is time-shared, not claimed as 32 independent lanes.
 * OFF / MIX 0 leaves the existing dry mixer bit-identical and allocates nothing.
 * Only the audio owner mutates DSP state or the optional short-delay memory. */
#include "../hal/fm1_eq.h"
enum { FX_OFF, FX_PHASER, FX_WIDTH, FX_FORMANT, FX_TAPE, FX_SPIT, FX_ECHO, FX_ROOM, FX_COUNT };
typedef struct { float b0,b1,b2,a1,a2,z1,z2; } fx_bq_t;
typedef struct {
    fx_bq_t q[2][4];
    float hw[2][4][8];
    uint32_t phase, age, epoch, pos;
    int32_t rng, random, mix;
    int16_t rate, character;
    uint8_t type, engine, decim, failed;
    int32_t echo, damp[4];
    uint16_t comb[4];
} fx_state_t;
static fx_state_t fx_state[NPART];
static volatile uint32_t fx_epoch;
static int32_t fx_left[CTL],fx_right[CTL];
static float fx_in[CTL],fx_out[CTL],fx_low[CTL];
static void fx_clear_all(void) { fx_epoch++; } /* consumed at the next audio block */
static inline __attribute__((always_inline)) float fx_bound(float x,float lim) { return x < -lim ? -lim : x > lim ? lim : x; }
static inline __attribute__((always_inline)) int32_t fx_scale(int32_t a,uint32_t k)
{return (a>>16)*(int32_t)k+(int32_t)(((uint32_t)(a&65535)*k)>>16);}
static inline __attribute__((always_inline)) int32_t fx_limit(int32_t a,int32_t lo,int32_t hi)
{return a<lo?lo:a>hi?hi:a;}
static void fx_coeff(fx_bq_t *q,uint32_t type,int32_t hz,float resonance,float gain)
{
    uint32_t ph=(uint32_t)((uint64_t)(uint32_t)clamp(hz,60,8000)*0x100000000ull/FS);
    float s=(float)sine_i(ph)*(1.0f/32768.0f),c=(float)sine_i(ph+0x40000000u)*(1.0f/32768.0f);
    float alpha=s/(2.0f*resonance),inv=1.0f/(1.0f+alpha);
    q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha)*inv;
    if(type==0u){q->b0=q->a2;q->b1=q->a1;q->b2=1.0f;} /* all pass */
    else if(type==1u){q->b0=(1.0f-c)*0.5f*inv;q->b1=2.0f*q->b0;q->b2=q->b0;}
    else if(type==2u){q->b0=alpha*inv;q->b1=0;q->b2=-q->b0;} /* band pass */
    else { /* peaking EQ: bounded pole radius, gain is amplitude's square root */
        inv=1.0f/(1.0f+alpha/gain);
        q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha/gain)*inv;
        q->b0=(1.0f+alpha*gain)*inv;q->b1=q->a1;q->b2=(1.0f-alpha*gain)*inv;
    }
}
static inline __attribute__((always_inline)) float fx_tick(fx_bq_t *q,float x)
{
    float y=q->b0*x+q->z1;
    q->z1=fx_bound(q->b1*x-q->a1*y+q->z2,2000000.0f);
    q->z2=fx_bound(q->b2*x-q->a2*y,2000000.0f);
    return fx_bound(y,1000000.0f);
}
static void fx_identity(fx_bq_t *q) { q->b0=1;q->b1=q->b2=q->a1=q->a2=0; }
static void fx_design(fx_state_t *f,uint32_t n)
{
    static const uint16_t vowel[5][3]={{800,1150,2900},{400,1700,2600},{300,2200,3000},{500,800,2800},{350,650,2700}};
    int32_t sweep=(sine_i(f->phase)+32768)>>9,cha=f->character;
    uint32_t old=f->phase;f->phase+=LFO_INC[f->rate]*n/CTL;
    if(f->phase<old || !f->age)f->random=(int32_t)(noise32(&f->rng)&127u);
    f->age+=n;
    for(uint32_t ch=0;ch<2u;ch++)for(uint32_t k=0;k<4u;k++){
        fx_bq_t *q=&f->q[ch][k];
        if(f->type==FX_PHASER || f->type==FX_WIDTH)
            fx_coeff(q,0,180+(int32_t)k*310+sweep*(3+cha/8)+(f->type==FX_WIDTH?(int32_t)ch*130:0),0.65f,1);
        else if(f->type==FX_FORMANT && k<3u)
            fx_coeff(q,3,vowel[(uint32_t)f->random%5u][k]*(80+cha)/144,4.0f,1.8f);
        else if(f->type==FX_TAPE && k<2u)
            fx_coeff(q,1,k?1400+(f->random-64)*cha/4:200+(f->random-64)*cha/64,0.7f,1);
        else if(f->type==FX_TAPE && k==2u)
            fx_coeff(q,1,6500-cha*38,0.7f,1);
        else if(f->type==FX_SPIT && k==0u)
            fx_coeff(q,2,180+f->random*45,1.0f+cha*0.15f,1);
        else fx_identity(q);
        float *h=f->hw[ch][k];
        h[0]=q->b0;h[1]=-q->a2;h[2]=q->b2/q->b0;h[3]=-q->a1;h[4]=q->b1/q->b0;
    }
}
/* Try the accelerator once with known nontrivial poles, then trust only a
 * finite, bounded output. Both channels own their hardware history. A failed
 * probe permanently selects the CPU path for this session. */
static uint8_t fx_hw_status; /* 0 untried, 1 verified, 2 CPU */
static void fx_probe(void)
{
#if MELODEE_HW_EQ && defined(FM1_IRQ_TARGET)
    fx_bq_t q[4]={{0}};float row[4][8]={{0}},want[CTL];
    for(uint32_t k=0;k<4u;k++){
        fx_coeff(&q[k],0,300+(int32_t)k*400,0.7f,1);
        row[k][0]=q[k].b0;row[k][1]=-q[k].a2;row[k][2]=q[k].b2/q[k].b0;row[k][3]=-q[k].a1;row[k][4]=q[k].b1/q[k].b0;
    }
    for(uint32_t i=0;i<CTL;i++){
        float x=i?0:8192;fx_in[i]=x;
        for(uint32_t k=0;k<4u;k++)x=fx_tick(&q[k],x);want[i]=x;
    }
    fx_hw_status=fm1_eq_run(row,fx_in,fx_out,CTL)?1:2;
    if(fx_hw_status==1)for(uint32_t i=0;i<CTL;i++)
        if(!(fx_out[i]>=want[i]-2 && fx_out[i]<=want[i]+2))fx_hw_status=2;
#else
    fx_hw_status=2;
#endif
}
/* A tape crossover is parallel: low LP, upper LP; their difference is the
 * middle band. Preserve separate state when time-sharing a single HW lane. */
static __attribute__((noinline,section(".dsp_text"))) void fx_single(fx_state_t *f,uint32_t ch,uint32_t k,const float *in,float *out,uint32_t n)
{
    if(fx_hw_status==1){
        float row[4][8];for(uint32_t j=0;j<32u;j++)((float *)row)[j]=j<8u?f->hw[ch][k][j]:0;
        for(uint32_t j=1;j<4u;j++)row[j][0]=1;
        if(fm1_eq_run(row,in,out,n)){
            int ok=1;for(uint32_t i=0;i<n;i++)if(!(out[i]>=-1000000 && out[i]<=1000000))ok=0;
            if(ok){for(uint32_t j=0;j<8u;j++)f->hw[ch][k][j]=row[0][j];return;}
        }
        fx_hw_status=2;
    }
    for(uint32_t i=0;i<n;i++)out[i]=fx_tick(&f->q[ch][k],in[i]);
}
static __attribute__((noinline,section(".dsp_text"))) void fx_filter(fx_state_t *f,uint32_t ch,int32_t *b,uint32_t n)
{
    for(uint32_t i=0;i<n;i++)fx_in[i]=(float)b[i];
    if(f->type==FX_TAPE){
        fx_single(f,ch,0,fx_in,fx_low,n);fx_single(f,ch,1,fx_in,fx_out,n);
        float sign=f->character>32 && (f->random&64)?-1.0f:1.0f;
        for(uint32_t i=0;i<n;i++)fx_in[i]=fx_low[i]+sign*(fx_out[i]-fx_low[i])+fx_in[i]-fx_out[i];
        fx_single(f,ch,2,fx_in,fx_out,n);
        for(uint32_t i=0;i<n;i++)b[i]=(int32_t)fx_out[i];return;
    }
    if(fx_hw_status==1 && fm1_eq_run(f->hw[ch],fx_in,fx_out,n)){
        int ok=1;
        for(uint32_t i=0;i<n;i++)if(!(fx_out[i]>=-1000000 && fx_out[i]<=1000000))ok=0;
        if(ok){for(uint32_t i=0;i<n;i++)b[i]=(int32_t)fx_out[i];return;}
        fx_hw_status=2;
    }else if(fx_hw_status==1)fx_hw_status=2;
    for(uint32_t i=0;i<n;i++){
        float x=fx_in[i];for(uint32_t k=0;k<4u;k++)x=fx_tick(&f->q[ch][k],x);b[i]=(int32_t)x;
    }
}
/* Tiny time effects run at FS/4. ECHO: 10..370 ms, 8 KiB. ROOM: four
 * damped combs at 13..26 ms, 2 KiB. They are intentionally lo-fi. */
static __attribute__((noinline,section(".dsp_text"))) void fx_time(fx_state_t *f,int16_t *mem,uint32_t n)
{
    static const uint16_t len[4]={149,211,263,293};
    uint32_t delay=110u+(uint32_t)f->rate*31u;
    for(uint32_t i=0;i<n;i++){
        int32_t mono=(fx_left[i]+fx_right[i])/2;
        if(!f->decim){
            if(f->type==FX_ECHO){
                f->echo=mem[(f->pos+4096u-delay)&4095u];
                mem[f->pos]=(int16_t)fx_limit(mono+f->echo*f->character*220/32768,-32767,32767);
                f->pos=(f->pos+1u)&4095u;
            }else{
                int32_t sum=0;uint32_t off=0;
                for(uint32_t k=0;k<4u;k++){
                    uint32_t span=(uint32_t)len[k]*(64u+(uint32_t)f->rate)/191u;
                    if(f->comb[k]>=span)f->comb[k]=0;
                    int32_t x=mem[off+f->comb[k]];
                    f->damp[k]+=(x-f->damp[k])/4;
                    mem[off+f->comb[k]]=(int16_t)fx_limit(mono/4+f->damp[k]*(12000+f->character*120)/32768,-32767,32767);
                    if(++f->comb[k]>=span)f->comb[k]=0;off+=len[k];sum+=x;
                }
                f->echo=sum/2;
            }
        }
        f->decim=(f->decim+1u)&3u;
        fx_left[i]=f->echo;fx_right[i]=f->type==FX_ROOM?f->damp[1]+f->damp[3]:f->echo;
    }
}
static __attribute__((noinline)) int fx_process(track_t *t,int32_t *b,uint32_t n)
{
    uint32_t ti=(uint32_t)(t-trk),type=(uint32_t)clamp(t->p[P_FXTYPE],0,FX_COUNT-1);
    fx_state_t *f=&fx_state[ti];
    int32_t target=type?clamp(t->p[P_FXMIX],0,127)*256:0;
    if(f->type && type && f->type!=type && f->mix)target=0;
    if(!target && !f->mix){
        if(f->type || resource[RES_FX0+ti].size){resource_release(RES_FX0+ti);memset(f,0,sizeof *f);}
        return 0;
    }
    if(f->epoch!=fx_epoch || (type && f->type!=type && !f->mix) || f->engine!=t->engine){
        resource_release(RES_FX0+ti);memset(f,0,sizeof *f);
        f->epoch=fx_epoch;f->engine=t->engine;f->type=(uint8_t)type;f->rng=(int32_t)(0x73a9825du+ti*997u);
    }
    f->rate=(int16_t)clamp(t->p[P_FXRATE],0,127);f->character=(int16_t)clamp(t->p[P_FXCHAR],0,127);
    int16_t *mem=0;
    if(f->type>=FX_ECHO){
        uint32_t bytes=f->type==FX_ECHO?8192u:2048u;
        if(!f->failed && !resource[RES_FX0+ti].size){
            mem=resource_get(RES_FX0+ti,bytes);
            if(mem)memset(mem,0,bytes);else f->failed=1;
        }else if(resource[RES_FX0+ti].size)mem=resource[RES_FX0+ti].ptr;
        if(!mem){f->mix=0;return 0;} /* recover after bypass or a mode change */
    }
    int32_t pan=t->p[P_PAN],s=spread_on?(t->p[P_SPRD]+1)>>1:0;
    int32_t pa=clamp(pan-s,-64,63),pb=clamp(pan+s,-64,63);
    int32_t gla=4096-(pa>0?pa*64:0),gra=4096+(pa<0?pa*64:0),glb=4096-(pb>0?pb*64:0),grb=4096+(pb<0?pb*64:0);
    for(uint32_t i=0;i<n;i++){
        int32_t a=spread_on?spread_buf[i]:b[i]/2,c=b[i]-a;
        a=clamp(a,-262143,262143);c=clamp(c,-262143,262143);
        fx_left[i]=clamp((a*gla+c*glb)/4096,-65536,65536);
        fx_right[i]=clamp((a*gra+c*grb)/4096,-65536,65536);
    }
    if(f->type>=FX_ECHO){void (*volatile run)(fx_state_t *,int16_t *,uint32_t)=fx_time;run(f,mem,n);}
    else {
        if(!fx_hw_status)fx_probe();fx_design(f,n);
        void (*volatile run)(fx_state_t *,uint32_t,int32_t *,uint32_t)=fx_filter;
        run(f,0,fx_left,n);
        if(f->type!=FX_WIDTH)run(f,1,fx_right,n); /* wet all-pass L, dry R */
    }
    for(uint32_t i=0;i<n;i++){
        int32_t a=spread_on?spread_buf[i]:b[i]/2,c=b[i]-a;
        a=clamp(a,-262143,262143);c=clamp(c,-262143,262143);
        int32_t l=(a*gla+c*glb)/4096,r=(a*gra+c*grb)/4096;
        int32_t wl=fx_left[i],wr=fx_right[i];
        if(f->type==FX_PHASER){wl=(wl+l)/2;wr=(wr+r)/2;}
        if(f->type==FX_FORMANT){wl=wl/2;wr=wr/2;}
        if(f->type==FX_SPIT){wl=softclip(clamp(wl,-100000,100000)*(1+f->character/8));wr=softclip(clamp(wr,-100000,100000)*(1+f->character/8));}
        f->mix+=clamp(target-f->mix,-128,128);
        fx_left[i]=l+mulq16(wl-l,(uint32_t)f->mix*2u);
        fx_right[i]=r+mulq16(wr-r,(uint32_t)f->mix*2u);
    }
    return 1;
}
