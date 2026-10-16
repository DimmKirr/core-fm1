/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Deterministic scheduling accounting, including dropped partial workers. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define __attribute__(x)
#define CTL 32u
#define MELODEE_DUAL_CORE 1
#define MELODEE_AUDIO_PROFILE 1
static uint32_t ticks;
#define AUDIO_PROFILE_TICKS() (++ticks)
#include "../firmware/src/audio_profile.h"
static int online=1, join_ok=1, audio_worker_online=1;
static void (*pending)(void *);
static int audio_worker_submit(void (*fn)(void *),void *context)
{
    assert(!context);if(!online)return 0;pending=fn;return 1;
}
static int audio_worker_join(void);
#include "../firmware/src/audio_batch.h"
static int audio_worker_join(void) { assert(pending);pending(0);pending=0;return join_ok; }
static uint32_t advances;
static void render(const void *data,int32_t *pcm)
{
    advances++;for(uint32_t i=0;i<CTL;i++)pcm[i]+=(int32_t)*(const uint32_t *)data;
}
static void scenario(int worker,int success)
{
    int32_t out[CTL]={0};uint32_t value=7;
    memset(audio_profile,0,sizeof audio_profile);online=worker;join_ok=success;advances=0;
    for(uint32_t j=0;j<3;j++)audio_batch_push(render,&value,sizeof value,out,CTL,AP_FM6);
    assert(AB_LOAD(&audio_batch_worker_mask)==3u);
    audio_batch_flush();assert(advances==3 && !audio_batch_count && !audio_batch_started);
    for(uint32_t i=0;i<CTL;i++)assert(out[i]==(worker&&!success?7:21));
    assert(audio_profile[AP_BATCH_PREPARE].calls==3);
    assert(audio_profile[AP_BATCH_MERGE].calls==(worker&&!success?1:3));
    assert(audio_profile[AP_BATCH_CPU0].calls==(worker?1:3));
    assert(audio_profile[AP_BATCH_CPU0].ticks==(worker?1:3));
    assert(audio_profile[AP_BATCH_CPU1].calls==(worker&&success?2:0));
    assert(audio_profile[AP_BATCH_CPU1].ticks==(worker&&success?2:0));
}
int main(void)
{
    scenario(1,1);scenario(0,1);scenario(1,0);
    audio_batch_jobs[0].n=0;audio_batch_jobs[0].elapsed=12345;
    audio_batch_worker(&audio_batch_jobs[0]);assert(!audio_batch_jobs[0].elapsed);
    puts("batch profile: producer/worker/merge counts, serial fallback and failed-worker drop exact");
}
