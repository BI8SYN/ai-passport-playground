#include "hm_spectrum.h"
#include <math.h>
#include <string.h>
#define TAU 6.2831853071795864769f
void hm_spectrum_reset(hm_spectrum_t *s){memset(s->levels,0,sizeof s->levels);memset(s->peaks,0,sizeof s->peaks);memset(s->hold,0,sizeof s->hold);}
void hm_spectrum_init(hm_spectrum_t *s){
 memset(s,0,sizeof *s);
 for(unsigned i=0;i<256;i++)s->window[i]=0.5f-0.5f*cosf(TAU*i/255.0f);
 for(unsigned i=0;i<128;i++){s->twiddle_re[i]=cosf(TAU*i/256);s->twiddle_im[i]=-sinf(TAU*i/256);}
}
void hm_spectrum_analyze(hm_spectrum_t *s,const int16_t pcm[256],uint8_t target[16]){
 float mean=0;for(unsigned i=0;i<256;i++)mean+=pcm[i];mean/=256;
 for(unsigned i=0;i<256;i++){
  unsigned r=0;for(unsigned k=0;k<8;k++)r=(r<<1)|((i>>k)&1);
  s->re[r]=(pcm[i]-mean)*s->window[i];s->im[r]=0;
 }
 for(unsigned span=2;span<=256;span*=2){unsigned half=span/2,step=256/span;
  for(unsigned base=0;base<256;base+=span)for(unsigned j=0;j<half;j++){
   unsigned a=base+j,b=a+half,w=j*step;
   float re=s->re[b]*s->twiddle_re[w]-s->im[b]*s->twiddle_im[w];
   float im=s->re[b]*s->twiddle_im[w]+s->im[b]*s->twiddle_re[w];
   s->re[b]=s->re[a]-re;s->im[b]=s->im[a]-im;s->re[a]+=re;s->im[a]+=im;
  }
 }
 static const unsigned edges[17]={2,3,4,5,6,8,10,13,17,22,28,36,46,59,76,98,128};
 for(unsigned band=0;band<16;band++){
  float power=0;for(unsigned bin=edges[band];bin<edges[band+1];bin++){float p=s->re[bin]*s->re[bin]+s->im[bin]*s->im[bin];if(p>power)power=p;}
  /* Hann coherent gain ~0.5; 64 maps bin magnitude to source amplitude.
   * Fixed -60dBFS floor means true silence never produces random motion. */
  float db=power>0 ? 10.0f*log10f(power/(64.0f*64*32768*32768)) : -60;
  int value=(int)((db+60)*(255.0f/60));if(value<0)value=0;if(value>255)value=255;target[band]=(uint8_t)value;
 }
}
void hm_spectrum_step(hm_spectrum_t *s,const uint8_t target[16],uint8_t levels[16],uint8_t peaks[16]){
 for(unsigned i=0;i<16;i++){
  unsigned old=s->levels[i],goal=target[i];
  s->levels[i]=goal>=old?(old+3*goal+3)/4:(old>18?(old-18>goal?old-18:goal):goal);
  if(s->levels[i]>=s->peaks[i]){s->peaks[i]=s->levels[i];s->hold[i]=3;}
  else if(s->hold[i])s->hold[i]--;
  else s->peaks[i]=s->peaks[i]>10?s->peaks[i]-10:0;
  if(s->peaks[i]<s->levels[i])s->peaks[i]=s->levels[i];
  levels[i]=s->levels[i];peaks[i]=s->peaks[i];
 }
}
