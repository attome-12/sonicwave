// ===== fichier : event_queue.hpp =====


// ===== fichier : drums.hpp =====

// ===== fichier : sequencer.hpp =====
#pragma once

#include "drums.hpp"
#include "event_queue.hpp"

#include <atomic>

constexpr int kStepsPerBar = 16; // 4 temps x 4 pas (doubles croches)

// Motif de batterie : true = frappe sur ce pas (0 à 15)
struct DrumPattern {
  bool kick[kStepsPerBar];
  bool snare[kStepsPerBar];
  bool hihat[kStepsPerBar];
};

// Motif simple : kick sur 1 et 3, snare sur 2 et 4, hihat en croches
inline DrumPattern makeBasicPattern() {
  DrumPattern p{};
  for (int step = 0; step < kStepsPerBar; ++step) {
    p.hihat[step] = (step % 2 == 0);
  }
  p.kick[0] = true;   // temps 1
  p.snare[4] = true;  // temps 2
  p.kick[8] = true;   // temps 3
  p.snare[12] = true; // temps 4
  return p;
}

class Sequencer {
public:
  std::atomic<double> bpm{120.0}; // écrit par le thread principal
  DrumPattern pattern = makeBasicPattern();

  // Utilisé uniquement dans le callback audio
  double nextStepFrame = 0.0;
  int stepCounter = 0; // pas absolu depuis le démarrage

  bool isStepDue(double framePosition) const {
    return framePosition >= nextStepFrame;
  }

  // Déclenche les sons du pas courant, planifie le suivant, enregistre
  // l'événement de log
  void fireStep(double sampleRate, DrumKit &drums, EventQueue &logQueue) {
    const double currentBpm = bpm.load();
    const double framesPerStep = sampleRate * 60.0 / currentBpm / 4.0;

    const double stepFrame = nextStepFrame;
    nextStepFrame += framesPerStep; // on ajoute à la cible : pas de dérive

    const int stepInBar = stepCounter % kStepsPerBar;
    const int bar = stepCounter / kStepsPerBar + 1;
    ++stepCounter;

    if (pattern.kick[stepInBar])
      drums.trigger(DrumSound::Kick);
    if (pattern.snare[stepInBar])
      drums.trigger(DrumSound::Snare);
    if (pattern.hihat[stepInBar])
      drums.trigger(DrumSound::HiHat);

    logQueue.push(BeatEvent{static_cast<unsigned long long>(stepFrame), bar,
                            stepInBar + 1, currentBpm});
  }
};

// ===== fichier : audio_engine.hpp =====
#pragma once

#include "miniaudio.h"

#include "drums.hpp"
#include "event_queue.hpp"
#include "sequencer.hpp"

#include <atomic>
#include <thread>

class AudioEngine {
public:
  AudioEngine() = default;
  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;
  ~AudioEngine() { stop(); }

  bool start(const char *kickPath, const char *snarePath,
             const char *hihatPath);
  void stop();
  void setBpm(double bpm) { sequencer_.bpm.store(bpm); }

private:
  static void dataCallback(ma_device *device, void *output, const void *input,
                           ma_uint32 frameCount);
  void process(float *out, ma_uint32 frameCount);
  void loggerLoop();

  ma_device device_{};
  bool deviceReady_ = false;
  double sampleRate_ = 48000.0;
  double framePosition_ = 0.0; // horloge audio, en frames

  DrumKit drums_;
  Sequencer sequencer_;
  EventQueue logQueue_;

  std::thread logger_;
  std::atomic<bool> running_{false};
};

// ===== fichier : audio_engine.cpp =====
// Compiler ce fichier avec main.cpp : il contient l'implémentation de miniaudio
#define MINIAUDIO_IMPLEMENTATION
#include "audio_engine.hpp"

#include <chrono>
#include <cstdio>

namespace {
void printEvent(const BeatEvent &e, double sampleRate) {
  std::printf("[%9.3f s] mesure %d, beat %d, pas %2d/16, %.1f BPM\n",
              e.frame / sampleRate, e.bar, (e.step - 1) / 4 + 1, e.step, e.bpm);
}
} // namespace

bool AudioEngine::start(const char *kickPath, const char *snarePath,
                        const char *hihatPath) {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = 48000;
  config.dataCallback = &AudioEngine::dataCallback;
  config.pUserData = this;

  if (ma_device_init(NULL, &config, &device_) != MA_SUCCESS) {
    std::fprintf(stderr, "Impossible d'initialiser le périphérique audio\n");
    return false;
  }
  deviceReady_ = true;
  sampleRate_ = static_cast<double>(device_.sampleRate);

  // Chargement des WAV au sample rate réel du device, avant le démarrage
  if (!drums_.load(kickPath, snarePath, hihatPath, device_.sampleRate)) {
    stop();
    return false;
  }

  if (ma_device_start(&device_) != MA_SUCCESS) {
    std::fprintf(stderr, "Impossible de démarrer le périphérique audio\n");
    stop();
    return false;
  }

  running_ = true;
  logger_ = std::thread(&AudioEngine::loggerLoop, this);
  return true;
}

void AudioEngine::stop() {
  if (deviceReady_) {
    ma_device_uninit(&device_); // arrête le callback avant de quitter le logger
    deviceReady_ = false;
  }
  running_ = false;
  if (logger_.joinable())
    logger_.join();
}

void AudioEngine::dataCallback(ma_device *device, void *output,
                               const void * /*input*/, ma_uint32 frameCount) {
  auto *self = static_cast<AudioEngine *>(device->pUserData);
  self->process(static_cast<float *>(output), frameCount);
}

// Exécuté dans le thread audio : aucune allocation, aucun print, aucun verrou
void AudioEngine::process(float *out, ma_uint32 frameCount) {
  for (ma_uint32 i = 0; i < frameCount; ++i) {
    if (sequencer_.isStepDue(framePosition_)) {
      sequencer_.fireStep(sampleRate_, drums_, logQueue_);
    }

    float left = 0.0f;
    float right = 0.0f;
    drums_.renderFrame(left, right);
    out[i * 2] = left;
    out[i * 2 + 1] = right;

    framePosition_ += 1.0;
  }
}

// Thread de log : lit la file et affiche, hors du thread audio
void AudioEngine::loggerLoop() {
  BeatEvent e;
  while (running_.load()) {
    while (logQueue_.pop(e))
      printEvent(e, sampleRate_);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  while (logQueue_.pop(e))
    printEvent(e, sampleRate_); // vider le reste
}

// ===== fichier : main.cpp =====
#include "audio_engine.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
  AudioEngine engine;
  if (!engine.start("kick.wav", "snare.wav", "hihat.wav")) {
    return 1;
  }

  engine.setBpm(120.0);
  std::puts("Tapez un BPM (20 à 300) puis Entrée. Tapez q pour quitter.");

  std::string line;
  while (std::getline(std::cin, line) && line != "q") {
    try {
      const double bpm = std::stod(line);
      if (bpm >= 20.0 && bpm <= 300.0) {
        engine.setBpm(bpm);
        std::printf("BPM réglé à %.1f\n", bpm);
      } else {
        std::puts("BPM hors limites (20 à 300)");
      }
    } catch (const std::exception &) {
      std::puts("Entrée invalide");
    }
  }

  engine.stop();
  return 0;
}

// ===== compilation (Ubuntu, depuis le dossier contenant les fichiers et
// kick.wav, snare.wav, hihat.wav) ===== g++ -std=c++17 -O2 -Wall -Wextra
// main.cpp audio_engine.cpp -o drum_seq -lpthread -lm -ldl
// ./drum_seq