// Roost - small helpers (no hardware dependencies).
#pragma once
#include <stddef.h>
#include <string.h>

// Shift a fixed-size history buffer left by one and append `value` at the end.
template <size_t N>
inline void pushSample(float (&buf)[N], float value) {
   memmove(buf, buf + 1, (N - 1) * sizeof(float));
   buf[N - 1] = value;
}

// Floating-point version of Arduino's map() (which truncates to long).
inline float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
   const float span = in_max - in_min;
   if (span == 0.0f) return out_min;
   return (x - in_min) * (out_max - out_min) / span + out_min;
}

// map() that clamps the result to [out_min, out_max] (either order) and tolerates NaN.
inline int mapClip(float x, float in_min, float in_max, float out_min, float out_max) {
   const float r  = mapf(x, in_min, in_max, out_min, out_max);
   const float lo = out_min < out_max ? out_min : out_max;
   const float hi = out_min < out_max ? out_max : out_min;
   if (r != r) return (int)lo;          // NaN
   if (r < lo) return (int)lo;
   if (r > hi) return (int)hi;
   return (int)r;
}
