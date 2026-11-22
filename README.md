# SonicWave

**SonicWave** est une **Groovebox** (boîte à rythmes et synthétiseur combinés, séquenceur-synthétiseur) développée en langage C++.
Le projet permet de programmer des boucles musicales en combinant une piste rythmique (*drum*) et une piste mélodique (*synth*), et de déclencher les sons correspondants en rythme.

L'architecture du projet est divisée en deux composants distincts communiquant ensemble : **SonicWave Control** (IHM) et **SonicWave Studio** (Application PC).

---

## 🏗️ Architecture du projet

### 1. `SonicWave-Control` (Firmware ESP8266)
La partie embarquée qui sert d'interface physique et de cœur rythmique.
* **Interface utilisateur :** Gestion de l'écran, des boutons et des contrôles physiques pour permettre au musicien de programmer ses séquences en temps réel.
* **Communication :** Envoi des événements de lecture et des paramètres vers le PC.

### 2. `SonicWave-Studio` (Application PC)
La partie logicielle qui agit comme moteur sonore.
* **Séquenceur :** Gestion de l'horloge (tempo/BPM), de la grille de pas (*step sequencer*) et du déclenchement précis des événements pour la piste de batterie et la piste de synthétiseur.
* **Synthèse et Audio :** Réception des commandes envoyées par l'ESP8266 et génération des sons en temps réel (échantillons de batterie et synthèse sonore).
* * **Communication :** Envoi des événements liés au séquenceur vers l'ESP8266.


---

## 🚀 Fonctionnement global

1. Le musicien programme sa boucle directement sur le boîtier via les boutons et l'écran.
2. À chaque pas du séquenceur, l'ordinateur détermine quels instruments doivent jouer (kick, snare, note de synthé, etc.).

---

## 🛠️ Technologies
* **Langage :** C++
* **Microcontrôleur :** ESP8266

## Outils de développement
* **IDE :** PlatformIO, pour ESP8266 (extension de Visual Studio Code)
* **CMake :** Utilisé pour la gestion de la compilation et des dépendances.
* **Makefile :** Utilisé pour automatiser les tâches de compilation et de construction du projet (géré par CMake).
* 


## 📁 Structure des projets
* **src** : Le répertoire `src` est destiné aux fichiers sources du projet (implémentations des classes, fonctions, etc.).
* **include** : Le répertoire `include` est destiné aux fichiers d'en-tête globaux du projet (constantes, configuration, types de données, etc.).
* **lib** : Le répertoire `lib` est inutilisé dans ce projet, mais il est destiné aux bibliothèques spécifiques au projet (librairies privées). PlatformIO les compilera en bibliothèques statiques et les liera au fichier exécutable.