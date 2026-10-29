#pragma once
#include <stdint.h>
#include <stdbool.h>
#define ET_RATE 16000
#define ET_SETTINGS_VERSION 3
#define ET_OPTIONS 24
#define ET_EVENTS 24
#define ET_TAPS 8
#define ET_MODE_COUNT 5
typedef enum { ET_INTERVAL,ET_CHORD,ET_SCALE,ET_NOTE,ET_RHYTHM } et_mode_t;
typedef struct { uint32_t at,duration; uint8_t notes[4],voices; } et_note_t;
typedef struct { et_note_t events[ET_EVENTS]; uint32_t length; uint8_t count; } et_score_t;
typedef struct { uint8_t mode,level,variant,speed,questions,volume; bool fixed; uint32_t custom; int16_t offset_ms; } et_config_t;
typedef struct {
    uint8_t count,answer,ids[ET_OPTIONS],attempts,step,steps,answers[4],root;
    bool done; uint32_t rejected; et_score_t score; uint32_t taps[ET_TAPS];
} et_question_t;
typedef struct { uint32_t total,first,correct,errors[ET_OPTIONS]; } et_result_t;
typedef enum { ET_IGNORED,ET_RETRY,ET_CORRECT,ET_WRONG } et_verdict_t;
et_config_t et_defaults(void);
void et_question(et_question_t *q,const et_config_t *c,uint32_t *seed);
et_verdict_t et_answer(et_result_t *r,et_question_t *q,int option);
bool et_next_step(et_question_t *q);
const char *et_mode_name(int mode);
const char *et_option_name(int mode,int variant,int id);
int et_pool_size(int mode,int variant);
void et_note_name(int midi,char *out,unsigned size);
int et_tap_error_ms(int64_t actual,int64_t expected,int offset_ms);
int et_rhythm_score(int error_ms);

int et_codec_level(unsigned ui_percent);

bool et_upgrade_config(et_config_t *c,unsigned stored_version);
bool et_arpeggiate(const et_score_t *source,et_score_t *out);
