#pragma once
#include "ear_model.h"
typedef struct { uint32_t phase,increment,age,duration; bool active; } et_voice_t;
typedef struct { et_score_t score; et_voice_t voices[8]; uint32_t cursor; uint8_t next,mix_divisor; } et_synth_t;
void et_synth_init(et_synth_t *s,const et_score_t *score);
void et_synth_render(et_synth_t *s,int16_t *out,unsigned count);

void et_synth_output(et_synth_t *s,int16_t *out,unsigned count,bool muted);
