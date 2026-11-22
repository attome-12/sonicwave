cmake_minimum_required(VERSION 3.14)
project(EspSerialProject LANGUAGES CXX)

# Définir le standard C++ (C++17 minimum conseillé pour les libs modernes)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Déclarer la bibliothèque externe (ex: serial_cpp depuis GitHub)
include(FetchContent)
FetchContent_Declare(
    serial_cpp
    GIT_REPOSITORY https://github.com/ami-iit/serial_cpp.git
    GIT_TAG        v1.3.4  # Remplacez par le tag ou la branche souhaitée
)

# 2. Rendre la bibliothèque disponible pour le projet
FetchContent_MakeAvailable(serial_cpp)

# 3. Créer votre exécutable
add_executable(sw-studio src/main.cpp)

# 4. Lier votre exécutable à la bibliothèque externe
target_link_libraries(sw-studio PRIVATE serial_cpp::serial_cpp)