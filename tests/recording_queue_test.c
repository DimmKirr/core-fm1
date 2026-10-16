/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
#if !MELODEE_RECORD_QUEUE
#error "This test exercises timestamped releases"
#endif
static void queue_fixture(uint32_t count)
{
    timing_reset(0,24000000u);seq_stop();song.rec=0;
    for(uint32_t tr=0;tr<NTRK;tr++){
        trk[tr].p[P_SLEN]=64;
        for(uint32_t s=0;s<NSTEP;s++){trk[tr].step[s].time=ST_NOTE;trk[tr].step[s].flags=SF_RECORDED;}
    }
    for(uint32_t i=0;i<count;i++){
        recorded_note_t r={0};r.duration=1000;r.note=(uint8_t)(i%128u);r.vel=100;r.step=(uint8_t)(i%64u);
        recording_restore_note(TSEL,i,r);
    }
    recording_reindex();
}
static int queue_invariants(void)
{
    uint8_t seen[RECORD_MAX]={0};
    for(uint32_t tr=0;tr<NTRK;tr++){
        uint16_t refs[128]={0},watch[NSTEP]={0};uint32_t bits[2]={0};
        for(uint32_t at=0;at<recording_heap_count[tr];at++){
            uint32_t i=recording_heap[tr][at];
            if(i>=RECORD_MAX || seen[i]++ || !recording_sounding(i) ||
               (recording[i].owner&31u)/NPAT!=tr || (recording_heap_pos[i]&1023u)!=at ||
               (at && recording_before(i,recording_heap[tr][(at-1u)/2u])))return 0;
            uint32_t view=recording_heap_pos[i]>>10;
            watch[view]++;bits[view>>5]|=1u<<(view&31u);refs[recording_message[i]&127u]++;
        }
        if(memcmp(refs,recording_refs[tr],sizeof refs) || memcmp(watch,recording_watch[tr],sizeof watch) ||
           memcmp(bits,recording_watch_bits[tr],sizeof bits))return 0;
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if((seen[i]!=0)!=recording_sounding(i))return 0;
    return 1;
}
static int rollover_and_retrigger(void)
{
    queue_fixture(2);track_t *t=TSEL;
    recording[0].note=0;recording[1].note=127;recording[0].duration=recording[1].duration=100;
    recording_clock[0]=UINT32_MAX-99u;
    recording_fire(t,0,RECORD_UNIT,0);recording_fire(t,1,RECORD_UNIT,0);
    int bad=check("pitch 0/127 release tags and a zero deadline remain active",recording_run[0].left==0 && recording_sounding(0) && recording_message[0]==128 && recording_message[1]==255 && queue_invariants());
    recording_queue_tick(t,99);
    bad+=check("clock rollover preserves the final unit of both gates",recording_left(0)==1 && recording_left(1)==1 && recording_owns(t,0) && recording_owns(t,127));
    recording_flags[0]&=(uint8_t)~4u;recording_fire(t,0,RECORD_UNIT,0);
    recording_queue_tick(t,1);
    bad+=check("retrigger cancels its old release while the other pitch ends",recording_left(0)==99 && !recording_sounding(1) && recording_owns(t,0) && !recording_owns(t,127) && queue_invariants());
    recording_queue_tick(t,99);
    bad+=check("the replacement gate releases exactly once",!recording_sounding(0) && !recording_owns(t,0) && queue_invariants());
    return bad;
}
static int dense_cancellation(void)
{
    queue_fixture(RECORD_MAX);track_t *t=TSEL;
    /* All 1024 slots on one track, with many overlapping equal pitches and
     * unsorted deadlines. Exercise indexed deletion at every heap depth. */
    uint32_t random=0x0f721a39u;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        random=random*1664525u+1013904223u;
        recording[i].duration=(uint16_t)(1000u+random%60000u);recording_fire(t,i,RECORD_UNIT,0);
    }
    int ok=recording_heap_count[0]==RECORD_MAX && queue_invariants();
    for(uint32_t k=0;k<RECORD_MAX && ok;k++){
        uint32_t i=(k*197u)%RECORD_MAX;
        if(k&1u){recording_flags[i]&=(uint8_t)~4u;recording_fire(t,i,RECORD_UNIT,k%300u);}
        else recording_remove(t,i);
        if(k%31u==0u)recording_reindex();
        recording_queue_tick(t,1);ok=queue_invariants();
    }
    int bad=check("1024 overlapping events survive cancellation, retrigger and reindex",ok);
    t->p[P_SLEN]=1;recording_queue_tick(t,1);
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording_sounding(i) && (recording[i].step&63u)>=1u)ok=0;
    bad+=check("shortening LEN cancels buried releases immediately",ok && queue_invariants());
    t->step[0].flags=0;recording_queue_tick(t,1);
    bad+=check("overwriting an overview cancels all its remaining releases",!recording_heap_count[0] && queue_invariants());
    seq_stop();return bad;
}
static int changed_overview(void)
{
    queue_fixture(2);track_t *t=TSEL;
    recording_fire(t,0,RECORD_UNIT,0);recording_fire(t,1,RECORD_UNIT,0);
    /* Punch-out quantization moves a sounding event without changing its
     * stored gate. Its old overview cell may become a tie or empty cell. */
    recording[0].step=2;t->step[0].flags=0;recording_schedule[0].period=recording_schedule[0].ordered_period=0;
    recording_sync(t,RECORD_UNIT);
    int ok=recording_sounding(0) && recording_watch[0][2]==1 && !recording_watch[0][0] && queue_invariants();
    t->step[2].flags=0;recording_queue_tick(t,1);
    return check("quantization moves the ownership watch; deleting its new cell releases it",ok && !recording_sounding(0) && recording_sounding(1) && queue_invariants());
}
static int independent_tracks(void)
{
    queue_fixture(4);
    for(uint32_t tr=0;tr<NTRK;tr++){
        recorded_note_t r=recording[tr];recording_remove(TSEL,tr);r.owner=(uint8_t)(tr*NPAT);r.duration=(uint16_t)(tr+1u);
        recording_restore_note(&trk[tr],tr,r);recording_clock[tr]=UINT32_MAX-tr;
        recording_fire(&trk[tr],tr,RECORD_UNIT,0);
    }
    int ok=queue_invariants();recording_reindex();
    for(uint32_t tr=0;tr<NTRK;tr++){
        recording_queue_tick(&trk[tr],tr+1u);
        if(recording_sounding(tr) || !queue_invariants())ok=0;
    }
    return check("track clocks and rollover deadlines remain independent across reindex",ok);
}
int main(void)
{
    int bad=rollover_and_retrigger()+dense_cancellation()+changed_overview()+independent_tracks();
    if(!bad)puts("Recording release queue tests passed");return bad!=0;
}
