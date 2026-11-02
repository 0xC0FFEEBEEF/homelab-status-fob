// Easing curves, value tweening and a scrolling sample ring.
#pragma once
#include <Arduino.h>

inline float clamp01(float t) { return t < 0.f ? 0.f : (t > 1.f ? 1.f : t); }

inline float easeInOutCubic(float t) {
  t = clamp01(t);
  if (t < 0.5f) return 4.f * t * t * t;
  const float u = -2.f * t + 2.f;
  return 1.f - (u * u * u) * 0.5f;
}

inline float easeOutCubic(float t) {
  t = clamp01(t);
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

// Slight overshoot, for things that should feel like they snap into place.
inline float easeOutBack(float t) {
  t = clamp01(t);
  const float c1 = 1.70158f, c3 = c1 + 1.f, u = t - 1.f;
  return 1.f + c3 * u * u * u + c1 * u * u;
}

// A 0..1 triangle wave, for pulses and breathing.
inline float pulse(float ms, float periodMs) {
  float p = fmodf(ms, periodMs) / periodMs;
  return p < 0.5f ? p * 2.f : 2.f - p * 2.f;
}

// Exponential approach: numbers and bars glide to their target instead of
// jumping, which is most of what makes the UI read as smooth.
struct Tween {
  float v = 0.f, target = 0.f, rate = 8.f;
  void set(float t)  { target = t; }
  void snap(float t) { v = target = t; }
  void update(float dt) {
    float k = rate * dt;
    if (k > 1.f) k = 1.f;
    v += (target - v) * k;
  }
};

// Fixed ring of samples for sparklines. at(0) is the oldest.
template <uint8_t N>
struct Spark {
  uint8_t d[N];
  uint8_t head = 0;
  bool    filled = false;

  Spark() { memset(d, 0, N); }
  void push(uint8_t v) {
    d[head] = v;
    head = (uint8_t)((head + 1) % N);
    if (head == 0) filled = true;
  }
  uint8_t count() const { return filled ? N : head; }
  uint8_t at(uint8_t i) const {
    const uint8_t start = filled ? head : 0;
    return d[(uint8_t)((start + i) % N)];
  }
};
