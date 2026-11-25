/* --- EventQueue.h --- */

/* ------------------------------------------
Author: Matt
Date: 25/11/2026
------------------------------------------ */

#ifndef EVENTQUEUE_H
#define EVENTQUEUE_H

#include <atomic>
#include <cstddef>

// Événement envoyé du callback audio vers le thread de log
struct BeatEvent {
  unsigned long long
      frame;  // position exacte du pas, en frames depuis le départ
  int bar;    // numéro de mesure (à partir de 1)
  int step;   // pas dans la mesure (1 à 16)
  double bpm; // tempo appliqué à ce pas
};

// File circulaire à un producteur (callback) et un consommateur (thread de
// log). Classe template de la lib atomic
template <size_t N> class SpscQueue {
public:
  bool push(const BeatEvent &e) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next = (head + 1) % N;

    if (next == tail_.load(std::memory_order_acquire))
      return false; // file pleine

    buffer_[head] = e;
    head_.store(next, std::memory_order_release);

    return true;
  }

  bool pop(BeatEvent &out) {
    const size_t tail = tail_.load(std::memory_order_relaxed);

    if (tail == head_.load(std::memory_order_acquire))
      return false; // vide

    out = buffer_[tail];
    tail_.store((tail + 1) % N, std::memory_order_release);

    return true;
  }

private:
  // Tableau buffer de BeatEvent : la file
  BeatEvent buffer_[N];

  // variable atomique pour le début et la fin de la queue,
  // s'utilisent avec load et store, et consignes de synchro :
  // + memory_order_release : à l'écriture, "tout ce que j'ai écrit avant est
  // prêt"
  // + memory_order_acquire : à la lecture, "je reçois ce qui a été marqué prêt"
  // + memory_order_relaxed : "pas besoin de garantie d'ordre, je lis ma propre
  // variable"
  std::atomic<size_t> head_{0}; // pointe sur la tête, case libre
  std::atomic<size_t> tail_{0}; // pointe sur la queue, valeur la plus ancienne
};

// Alias de type pour la file d'événements,
// avec une taille de 256 éléments
using EventQueue = SpscQueue<256>;

#endif // EVENTQUEUE_H
