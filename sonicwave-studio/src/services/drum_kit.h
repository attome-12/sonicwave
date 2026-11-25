/* --- DrumKit.h --- */

/* ------------------------------------------
Author: Matt
Date: 25/11/2026
------------------------------------------ */

#ifndef DRUMKIT_H
#define DRUMKIT_H

#include "miniaudio.h"
#include <cstdio>
#include <iostream>

// Un instrument de batterie : 3 samples, Kick Snare HiHat
// chacun pouvant être joué simultanément plusieurs fois
enum class DrumSound { Kick, Snare, HiHat };

// Sample : fichier audio instrument
// ne pas confondre avec "échantillon"
// décodé une seule fois au démarrage
// en stéréo float32 entrelacé
//
// c'est donc un tableau de float
struct Sample {
  float *pcm = nullptr; // Pulse Code Modulation
  ma_uint64 frames = 0;
};

// Une voix = une lecture d'un sample
struct Voice {
  const float *pcm = nullptr;
  ma_uint64 frames = 0;
  ma_uint64 position = 0;
  float gain = 1.0f;
  bool active = false;
};

class DrumKit {
public:
  static constexpr int maxVoiceCount = 16;

  // Constructeur par défaut
  DrumKit() = default;

  // Interdiction de copier ou d'assigner un DrumKit
  // Cela empêche d'allouer de la mémoire pour rien
  // et évite des problèmes de double libération de mémoire
  DrumKit(const DrumKit &) = delete;
  DrumKit &operator=(const DrumKit &) = delete;

  ~DrumKit() {
    unload(kick_);
    unload(snare_);
    unload(hihat_);
  }

  // À appeler avant le démarrage du device, avec son sample rate réel
  bool load(const char *kickPath, const char *snarePath, const char *hihatPath,
            ma_uint32 sampleRate) {
    return loadSample(kickPath, sampleRate, kick_) &&
           loadSample(snarePath, sampleRate, snare_) &&
           loadSample(hihatPath, sampleRate, hihat_);
  }

  // Appelé uniquement dans le callback audio
  void trigger(DrumSound drum, float gain = 1.0f) {
    const Sample &s = sampleFor(drum);

    if (!s.pcm) return; // Si le sample n'est pas chargé, on ne fait rien

    Voice &v = voices_[nextVoice_]; // vol de voix : on écrase la plus ancienne
    nextVoice_ = (nextVoice_ + 1) % maxVoiceCount;
    v.pcm = s.pcm;
    v.frames = s.frames;
    v.position = 0;
    v.gain = gain;
    v.active = true;
  }

  // Ajoute la contribution de toutes les voix actives à la frame courante
  void renderFrame(float &left, float &right) {
    for (Voice &v : voices_) {
      if (!v.active)
        continue;
      if (v.position >= v.frames) {
        v.active = false;
        continue;
      }
      const ma_uint64 index = v.position * 2;
      left += v.pcm[index] * v.gain;
      right += v.pcm[index + 1] * v.gain;
      ++v.position;
    }
  }

private:
  static bool loadSample(const char *path, ma_uint32 sampleRate, Sample &out) {
    ma_decoder_config config =
        ma_decoder_config_init(ma_format_f32, 2, sampleRate);
    void *pcm = nullptr;
    ma_uint64 frames = 0;
    if (ma_decode_file(path, &config, &frames, &pcm) != MA_SUCCESS) {
      std::fprintf(stderr, "Impossible de charger %s\n", path);
      return false;
    }
    out.pcm = static_cast<float *>(pcm);
    out.frames = frames;
    return true;
  }

  static void unload(Sample &s) {
    if (s.pcm)
      ma_free(s.pcm, NULL);
    s.pcm = nullptr;
    s.frames = 0;
  }

  const Sample &sampleFor(DrumSound drum) const {
    switch (drum) {
    case DrumSound::Kick:
      return kick_;
    case DrumSound::Snare:
      return snare_;
    default:
      return hihat_;
    }
  }

  Sample kick_;
  Sample snare_;
  Sample hihat_;
  Voice voices_[maxVoiceCount];
  int nextVoice_ = 0;
};

#endif // DRUMKIT_H
