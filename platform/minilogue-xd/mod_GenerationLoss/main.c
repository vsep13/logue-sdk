#include "userosc.h"
#include <math.h>
#include <stdint.h>

// VHS-style wow/flutter + gentle LPF + noise sprinkle.
// TIME = wow rate (0.1..3Hz), DEPTH = wow depth (0..8ms), SHIFT_DEPTH = mix (0..1)

#define SR 48000
#define MAX_DELAY_SAMPLES  (SR/30) // ~33 ms
static float dl[MAX_DELAY_SAMPLES*2];
static uint32_t widx;
static float phase_wow, phase_flutter;
static float inc_wow, inc_flutter;
static float depth_samp;
static float mix;
static float lpL, lpR; // simple one-pole lowpass
static float tone = 0.2f; // fixed cutoff blend

static inline float frand(uint32_t *state){
  *state = (*state * 1664525u) + 1013904223u;
  return ((*state >> 9) & 0x007FFFFF) / 8388607.0f * 2.f - 1.f;
}

static inline float lerp(float a, float b, float t){ return a + (b-a)*t; }

void MODFX_INIT(uint32_t platform, uint32_t api){
  (void)platform; (void)api;
  for(uint32_t i=0;i<MAX_DELAY_SAMPLES*2;i++) dl[i]=0.f;
  widx=0; phase_wow=phase_flutter=0.f;
  inc_wow = 2.f*(float)M_PI * 0.5f / (float)SR;
  inc_flutter = 2.f*(float)M_PI * 6.f / (float)SR;
  depth_samp = 0.003f * SR; // 3 ms
  mix = 0.5f;
  lpL = lpR = 0.f;
}

void MODFX_PROCESS(const float *main_xn, float *main_yn,
                   const float *sub_xn,  float *sub_yn,
                   uint32_t frames){
  (void)sub_xn; (void)sub_yn;
  static uint32_t rng=1234567u;
  const float alpha = 0.15f; // LPF smoothing
  for(uint32_t i=0;i<frames;i++){
    for(int ch=0; ch<2; ++ch){
      float x = main_xn[2*i + ch];
      dl[widx + ch] = x;

      float wow = sinf(phase_wow);
      float flutter = sinf(phase_flutter)*0.25f;
      float mod = (wow*0.8f + flutter*0.2f + 1.f)*0.5f * depth_samp;

      float rpos = (float)widx - mod*2.f;
      while(rpos < 0.f) rpos += (float)(MAX_DELAY_SAMPLES*2);
      int i0 = (int)rpos;
      int i1 = (i0 + 2) % (MAX_DELAY_SAMPLES*2);
      float frac = rpos - (float)i0;
      float ydl = lerp(dl[i0], dl[i1], frac);

      // gentle lowpass to tame highs
      float *lp = (ch==0)? &lpL : &lpR;
      *lp = *lp + alpha * (ydl - *lp);
      float ytilt = lerp(ydl, *lp, tone);

      // tiny noise
      float y = (1.f - mix)*x + mix*(ytilt + 0.002f*frand(&rng));
      main_yn[2*i + ch] = y;
    }
    widx += 2; if(widx >= MAX_DELAY_SAMPLES*2) widx = 0;
    phase_wow += inc_wow; if(phase_wow > 2.f*(float)M_PI) phase_wow -= 2.f*(float)M_PI;
    phase_flutter += inc_flutter; if(phase_flutter > 2.f*(float)M_PI) phase_flutter -= 2.f*(float)M_PI;
  }
}

void MODFX_PARAM(uint8_t index, int32_t value){
  const float valf = q31_to_f32(value);
  switch(index){
    case k_user_modfx_param_time: { // wow rate 0.1..3Hz
      const float rate = 0.1f + valf * 2.9f;
      inc_wow = 2.f*(float)M_PI * rate / (float)SR;
      inc_flutter = 2.f*(float)M_PI * (rate*12.f) / (float)SR;
    } break;
    case k_user_modfx_param_depth: { // 0..8ms
      depth_samp = valf * (0.008f * SR);
    } break;
    case k_user_modfx_param_shift_depth: { // mix
      mix = valf;
    } break;
  }
}

void MODFX_RESUME(void){}
void MODFX_SUSPEND(void){}