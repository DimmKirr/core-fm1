/* SPDX-License-Identifier: GPL-3.0-only */
/* Real UI/mixer/storage integration plus deterministic DSP stress. */
#include <assert.h>
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
static void reset_fx(void)
{
    fx_clear_all();memset(fx_state,0,sizeof fx_state);
    for(uint32_t k=0;k<NPART;k++)resource_release(RES_FX0+k);
    memset(trk,0,sizeof trk);spread_on=0;trk[0].p[P_FXRATE]=60;trk[0].p[P_FXCHAR]=64;
}
static uint64_t fx_signal(uint32_t type,int rate,int character,int mix,const char *demo)
{
    reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=(int16_t)type;t->p[P_FXRATE]=(int16_t)rate;
    t->p[P_FXCHAR]=(int16_t)character;t->p[P_FXMIX]=(int16_t)mix;
    uint32_t phase=0;uint64_t h=1469598103934665603ull,energy=0;int32_t b[CTL];
    FILE *w=demo?fopen(demo,"wb"):0;if(demo)assert(w);if(w)wav_hdr(w,2u*FS/CTL*CTL);
    for(uint32_t frame=0;frame<2u*FS/CTL;frame++){
        for(uint32_t i=0;i<CTL;i++){
            phase+=0x02347120u;
            b[i]=((sine_i(phase)+sine_i(phase*3u)/2)/4)*2;
            if(frame%173u==0u && !i)b[i]+=18000;
        }
        int on=fx_process(t,b,CTL);
        assert(resource[RES_FX0].size==(type==FX_ECHO && mix?8192u:type==FX_ROOM && mix?2048u:0u));
        for(uint32_t i=0;i<CTL;i++){
            int32_t l=on?fx_left[i]:b[i],r=on?fx_right[i]:b[i];
            assert(l>=-1000000 && l<=1000000 && r>=-1000000 && r<=1000000);
            if(type==FX_WIDTH)assert(r==b[i]);
            h=(h^(uint32_t)l)*1099511628211ull;h=(h^(uint32_t)r)*1099511628211ull;
            energy+=(uint64_t)(l<0?-l:l)+(uint64_t)(r<0?-r:r);
            if(w)wav_put(w,clamp(l,-32767,32767),clamp(r,-32767,32767));
        }
    }
    if(w)fclose(w);assert(energy>1000000u);return h;
}
static void legacy(void)
{
    static uint8_t b[BANK_STORE_SIZE];
    const char *names[]={"fun16.bin","fbkk.bin","tplf.bin"};
    for(uint32_t k=0;k<3u;k++){
        char path[96];snprintf(path,sizeof path,"tests/fixtures/fx/%s",names[k]);
        FILE *f=fopen(path,"rb");assert(f);uint32_t n=(uint32_t)fread(b,1,sizeof b,f);fclose(f);
        ui_power_on();
        if(k==2u){assert(tmpl_take(b,n));template_load();}
        else{
            assert(k?bank_valid(b,n):proj_import(&proj_scratch,b,(int)n));
            assert(proj_scratch.t[0].p[P_E0]==17 && proj_scratch.motion.event[0].param==P_E0);
            assert(proj_scratch.recording[0].on==55 && proj_scratch.recording[0].duration==217 && proj_scratch.recording[0].owner==11);
            assert(!project_restore_runtime(&proj_scratch));if(k)bank_restore(b);
            assert(TSEL->step[0].n==3 && TSEL->step[0].note[2]==67);
            if(k)assert(pattern_at(1,3)->step[7].note[0]==72);
        }
        assert(TSEL->p[P_E0]==17 && !TSEL->p[P_FXTYPE] && !TSEL->p[P_FXMIX]);
        assert(TSEL->p[P_FXRATE]==60 && TSEL->p[P_FXCHAR]==64);
    }
    puts("Previous firmware FUN16, FBKK, TPLF: notes, banks, timing, motion and engine IDs preserved; FX dry: OK");
}
int main(int argc,char **argv)
{
    legacy();
    uint64_t dry=fx_signal(FX_OFF,60,64,0,0);
    for(uint32_t mode=FX_PHASER;mode<FX_COUNT;mode++){
        assert(fx_signal(mode,60,64,0,0)==dry);
        uint64_t a=fx_signal(mode,60,64,127,0);assert(a!=dry && a==fx_signal(mode,60,64,127,0));
        assert(a!=fx_signal(mode,127,64,127,0));assert(a!=fx_signal(mode,60,127,127,0));
        for(int rate=0;rate<=127;rate+=127)for(int cha=0;cha<=127;cha+=127)fx_signal(mode,rate,cha,127,0);
        if(argc>1){char path[256];snprintf(path,sizeof path,"%s/%s.wav",argv[1],N_FX[mode]);fx_signal(mode,60,64,100,path);}
        printf("%s: deterministic, audible controls, bounded extremes, zero-mix bypass: OK\n",N_FX[mode]);
    }
    /* Time tails survive voice completion, bypass releases their allocation. */
    for(uint32_t mode=FX_ECHO;mode<=FX_ROOM;mode++){
        reset_fx();track_t *t=&trk[0];t->p[P_FXTYPE]=(int16_t)mode;t->p[P_FXMIX]=127;t->p[P_FXRATE]=0;
        t->p[P_FXCHAR]=80;fx_state[0].mix=32768;int32_t b[CTL]={20000};
        assert(fx_process(t,b,CTL));uint64_t tail=0;
        for(uint32_t k=0;k<1500;k++){memset(b,0,sizeof b);assert(fx_process(t,b,CTL));for(uint32_t i=0;i<CTL;i++)tail+=(uint32_t)abs(fx_left[i]);}
        assert(tail>1000);t->p[P_FXMIX]=0;
        for(uint32_t k=0;k<16;k++)fx_process(t,b,CTL);assert(!resource[RES_FX0].size);
    }
    /* Exhaust all usable banks: fail dry, with no per-block allocation retry. */
    reset_fx();for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    for(uint32_t id=0;id<RES_FX0;id++)assert(id==4u || resource_get(id,id==0?163840u:id==1?20480u:id==2?4096u:2048u));
    trk[0].p[P_FXTYPE]=FX_ECHO;trk[0].p[P_FXMIX]=127;int32_t b[CTL]={0};
    assert(!fx_process(&trk[0],b,CTL));uint32_t fail=resource_failures;assert(fx_state[0].failed);
    for(uint32_t k=0;k<100;k++)assert(!fx_process(&trk[0],b,CTL));assert(resource_failures==fail);
    for(uint32_t id=0;id<RES_COUNT;id++)resource_release(id);
    ui_power_on();press(B_FX);assert(!ui.home && cur_page()->fam==FAM_FX && !ui.layer);
    turn(EN_K1,2);assert(TSEL->p[P_FXTYPE]==FX_WIDTH);turn(EN_K2,50);assert(TSEL->p[P_FXMIX]>0);
    project_t q;project_capture(&q);project_store_t p;assert(proj_pack(&p,&q));
    assert(proj_import(&proj_scratch,&p,sizeof p));assert(proj_scratch.t[0].p[P_FXTYPE]==FX_WIDTH);
    frames(400);press(B_OCTUP);assert(pop.on==POP_SHEET);press(B_OCTDN);assert(!pop.on);
    press(B_OCTDN);assert(ui.home);
    /* Four live tracks through the actual mixer, then mute a sounding insert. */
    ui_power_on();int32_t out[CTL*2];
    for(uint32_t k=0;k<NPART;k++){
        set_engine_of(&trk[k],ENGI_FM6);apply_preset_to(&trk[k],8);
        trk[k].p[P_FXTYPE]=(int16_t)(FX_PHASER+k);trk[k].p[P_FXMIX]=100;trk_note_on(&trk[k],48+k*7u,100);
    }
    for(uint32_t k=0;k<256u;k++){
        mix_block(out,CTL);for(uint32_t i=0;i<CTL*2u;i++)assert(out[i]>=-32767 && out[i]<=32767);
    }
    for(uint32_t k=0;k<NPART;k++)trk[k].p[P_MUTE]=1;
    for(uint32_t k=0;k<2048u;k++)mix_block(out,CTL);
    for(uint32_t i=0;i<CTL*2u;i++)assert(abs(out[i])<=1);
    puts("FX: tails, allocator exhaustion/recovery, FX button/knobs/Esc and project round trip: OK");return 0;
}
