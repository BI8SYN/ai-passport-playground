#include "hm_spectrum.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static hm_spectrum_t analyzer;
static int16_t pcm[256];
static void tone(double hz,double second){for(unsigned i=0;i<256;i++)pcm[i]=(int16_t)(10000*sin(6.283185307179586*hz*i/8000)+second*sin(6.283185307179586*2500*i/8000));}
static unsigned maximum(const uint8_t *v){unsigned k=0;for(unsigned i=1;i<16;i++)if(v[i]>v[k])k=i;return k;}
int main(void){
 uint8_t target[16],out[16],peaks[16];hm_spectrum_init(&analyzer);
 hm_spectrum_analyze(&analyzer,pcm,target);for(unsigned i=0;i<16;i++)assert(target[i]==0);
 tone(250,0);hm_spectrum_analyze(&analyzer,pcm,target);unsigned low=maximum(target);assert(low>=4&&low<=6&&target[low]>160);
 tone(2500,0);hm_spectrum_analyze(&analyzer,pcm,target);unsigned high=maximum(target);assert(high>=13&&high>low);
 tone(1000,0);hm_spectrum_analyze(&analyzer,pcm,target);assert(maximum(target)==10);
 tone(250,9000);hm_spectrum_analyze(&analyzer,pcm,target);assert(target[5]>160&&target[14]>150);
 memset(pcm,0,sizeof pcm);for(unsigned i=0;i<256;i++)pcm[i]=1234;hm_spectrum_analyze(&analyzer,pcm,target);for(unsigned i=0;i<16;i++)assert(target[i]==0);
 memset(target,0,sizeof target);target[3]=200;hm_spectrum_step(&analyzer,target,out,peaks);assert(out[3]>=150&&peaks[3]>=out[3]);
 memset(target,0,sizeof target);hm_spectrum_step(&analyzer,target,out,peaks);assert(out[3]>0&&out[3]<200);
 for(unsigned k=0;k<40;k++){hm_spectrum_step(&analyzer,target,out,peaks);}
 for(unsigned i=0;i<16;i++)assert(out[i]==0&&peaks[i]==0);
 target[0]=255;hm_spectrum_step(&analyzer,target,out,peaks);hm_spectrum_reset(&analyzer);memset(target,0,sizeof target);hm_spectrum_step(&analyzer,target,out,peaks);for(unsigned i=0;i<16;i++)assert(out[i]==0&&peaks[i]==0);
 puts("Real spectrum: silence/DC, frequency separation, mixed tones, attack/decay/reset PASS");return 0;
}
