#include <iostream>
#include <string>
#include <serial_cpp/serial.h>

int main() {
    // Remplacez par votre port série (ex: /dev/ttyUSB0 ou /dev/ttyACM0)
    std::string port = "/dev/ttyUSB0";
    unsigned int baudrate = 115200;

    std::cout << "Connexion à l'ESP8266 sur " << port << " à " << baudrate << " bauds..." << std::endl;

    try {
        // Initialisation et ouverture du port avec un timeout de 1 seconde (1000 ms)
        serial_cpp::Serial my_serial(port, baudrate, serial_cpp::Timeout::simpleTimeout(1000));

        if (my_serial.isOpen()) {
            std::cout << "Port série ouvert avec succès !" << std::endl;
        }

        std::cout << "---------------------------------------------------" << std::endl;
        std::cout << "Tapez un message à envoyer à l'ESP8266 (ou 'exit' pour quitter) :" << std::endl;
        std::cout << "---------------------------------------------------" << std::endl;

        std::string input;
        while (true) {
            std::cout << "> ";
            std::getline(std::cin, input);

            // Condition pour quitter la boucle
            if (input == "exit") {
                std::cout << "Fermeture de la connexion..." << std::endl;
                break;
            }

            // Ignorer les lignes vides
            if (input.empty()) {
                continue;
            }

            // Ajouter un retour à la ligne '\n' pour que l'ESP8266 (Serial.readStringUntil('\n')) valide la fin de chaîne
            input += "\n";

            // Écriture sur le port série
            my_serial.write(input);

            // Lecture de la réponse renvoyée par l'ESP8266
            // serial_cpp utilise le timeout configuré pour s'arrêter si l'ESP ne répond rien
            std::string response = my_serial.readline();

            if (!response.empty()) {
                // Nettoyage optionnel des retours chariots superflus pour un affichage propre
                if (!response.empty() && response.back() == '\n') {
                    response.pop_back();
                }
                std::cout << "Réponse ESP8266 : " << response << std::endl;
            } else {
                std::cout << "[Attention] Aucune réponse reçue de l'ESP8266 (timeout)." << std::endl;
            }
        }

        // La fermeture du port se fait automatiquement à la destruction de l'objet my_serial,
        // mais on peut aussi appeler my_serial.close() explicitement.
        my_serial.close();

    } catch (const serial_cpp::IOException& e) {
        std::cerr << "Erreur I/O série : " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Erreur inattendue : " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Programme terminé." << std::endl;
    return 0;
}