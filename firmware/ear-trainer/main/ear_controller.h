#pragma once
#include "ear_model.h"
typedef enum { ET_HOME,ET_SETUP,ET_PRACTICE,ET_SUMMARY,ET_STATS,ET_POOL } et_page_t;
typedef struct {
    et_config_t config; et_question_t question; et_result_t session,history[ET_MODE_COUNT*3];
    et_page_t page; int focus,round,battery; uint32_t seed,generation;
    bool audio_ok,dirty,play,save,ok_down,held,rhythm_done,arpeggio,session_saved;
    int64_t pressed_at,rhythm_at; int taps,errors[ET_TAPS],rhythm_percent;
    int axis_key;uint8_t axis_volume;uint32_t axis_generation;
    int64_t axis_at,axis_steps,volume_until;bool volume_changed;
    et_verdict_t verdict;
} et_app_t;
void et_app_init(et_app_t *a);
void et_app_key(et_app_t *a,int key,bool down,int64_t now);
void et_app_tick(et_app_t *a,int64_t now);
void et_app_audio_start(et_app_t *a,uint32_t generation,int64_t at);
void et_app_next(et_app_t *a);
int et_setting_count(void);
void et_setting_text(const et_app_t *a,int row,char *out,unsigned size);
