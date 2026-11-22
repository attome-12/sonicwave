import os
import re

# Dossiers à ignorer absolument pour ne pas corrompre le build PlatformIO / Git
IGNORED_DIRS = {".git", ".pio", ".vscode", "build", ".pioenvs"}

def to_snake_case(name):
    """Convertit un nom en snake_case (gère PascalCase et camelCase)."""
    s1 = re.sub(r'(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

def refactor_project(root_dir):
    file_renames = {}  # Dictionnaire pour stocker { "AncienNomDeBase": "nouveau_nom_de_base" }

    print("--- Étape 1 : Renommage physique des fichiers ---")
    # 1. Première passe : Renommer les fichiers en snake_case et mémoriser les changements
    for dirpath, dirnames, filenames in os.walk(root_dir, topdown=False):
        dirnames[:] = [d for d in dirnames if d not in IGNORED_DIRS]
        
        for filename in filenames:
            name, ext = os.path.splitext(filename)
            
            if ext.lower() not in ['.c', '.cpp', '.h', '.hpp']:
                continue
                
            new_name_base = to_snake_case(name)
            new_filename = new_name_base + ext.lower()
            
            if filename != new_filename:
                old_path = os.path.join(dirpath, filename)
                new_path = os.path.join(dirpath, new_filename)
                
                os.rename(old_path, new_path)
                print(f"[FICHIER RENOMMÉ] {filename} -> {new_filename}")
            
            # On enregistre la modification si le nom de base a changé
            if name != new_name_base:
                file_renames[name] = new_name_base

    print("\n--- Étape 2 : Mise à jour automatique des #include ---")
    # 2. Deuxième passe : Parcourir à nouveau le code pour remplacer les includes
    for dirpath, dirnames, filenames in os.walk(root_dir, topdown=False):
        dirnames[:] = [d for d in dirnames if d not in IGNORED_DIRS]
        
        for filename in filenames:
            ext = os.path.splitext(filename)[1]
            if ext.lower() not in ['.c', '.cpp', '.h', '.hpp']:
                continue
                
            file_path = os.path.join(dirpath, filename)
            
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()

            modified = False
            
            # Pour chaque fichier renommé, on cherche s'il est inclus sous son ancien nom
            for old_base, new_base in file_renames.items():
                # Motif pour trouver #include "AncienNom.h" ou #include <AncienNom.h>
                pattern = rf'(#include\s+["<]){old_base}(\.h|\.hpp)([>"])'
                
                # On remplace par le nouveau nom en conservant les guillemets ou chevrons d'origine
                replacement = rf'\1{new_base}\2\3'
                
                new_content, count = re.subn(pattern, replacement, content)
                if count > 0:
                    content = new_content
                    modified = True
                    print(f"[INCLUDE MAJ dans {filename}] {old_base}.h -> {new_base}.h ({count} occurrence(s))")

            # Réécriture du fichier uniquement s'il y a eu des modifications
            if modified:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(content)

if __name__ == "__main__":
    project_root = "."  # Dossier courant
    print("Lancement du scan, du renommage et de la mise à jour des includes...")
    refactor_project(project_root)
    print("\nOpération terminée avec succès !")