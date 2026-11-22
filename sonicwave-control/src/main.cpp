#include <Arduino.h>

void setup() {
  // Initialisation de la liaison série à 115200 bauds
  Serial.begin(115200);
  delay(3000);

  // LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  // Vérifie si des données sont disponibles sur le port série
  if (Serial.available() > 0) {
    // Lit la chaîne de caractères jusqu'à un retour à la ligne
    String receivedString = Serial.readStringUntil('\n');
    receivedString.trim(); // Nettoie les espaces/retours chariot

    // Répond au PC
    digitalWrite(LED_BUILTIN, LOW); // Allume la LED
    Serial.print("Bien reçu::: ");
    Serial.println(receivedString);
    delay(100); // Attente
    digitalWrite(LED_BUILTIN, HIGH); // Éteint la LED
  }
}