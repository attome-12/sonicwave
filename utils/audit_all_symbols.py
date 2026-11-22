import os
import re

# Dossiers à ignorer absolument
IGNORED_DIRS = {".git", ".pio", ".vscode", "build", ".pioenvs"}

# --- RÈGLES DE VALIDATION DE CASSE ---
def check_pascal(name):
    return bool(re.match(r'^[A-Z][a-zA-Z0-9]*$', name)) and '_' not in name

def check_camel(name):
    # Autorise camelCase (ex: maVariable) ou lower_case (ex: ma_variable)
    if name.isupper() and '_' in name:
        return False
    return bool(re.match(r'^[a-z][a-zA-Z0-9_]*$', name))

def check_upper_snake(name):
    return bool(re.match(r'^[A-Z0-9_]+$', name)) and len(name) > 1

def clean_code_block(content):
    """Retire les commentaires et les chaînes de caractères pour éviter les faux positifs."""
    # Supprime les commentaires multilignes
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # Supprime les commentaires monolignes
    content = re.sub(r'//.*', '', content)
    return content

def audit_file(file_path):
    issues = []
    
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        raw_content = f.read()
        
    content = clean_code_block(raw_content)
    lines = content.splitlines()
    
    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            continue
            
        # 1. Classes / Structs (Attendu : PascalCase)
        cls_match = re.search(r'\b(?:class|struct)\s+([a-zA-Z0-9_]+)', line)
        if cls_match:
            name = cls_match.group(1)
            if not check_pascal(name):
                issues.append((line_num, f"Classe/Struct mal formattée (PascalCase requis) : '{name}'"))
            continue

        # 2. Fonctions / Méthodes (Attendu : camelCase ou lower_case)
        # Détection type: [type] [nom](...) hors structures de contrôle (if, for, while, switch)
        func_match = re.search(r'\b[a-zA-Z0-9_<>]+\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^;]*\)\s*(?:const)?\s*(?:\{|\;)', line)
        if func_match:
            name = func_match.group(1)
            if name not in ['if', 'for', 'while', 'switch', 'return', 'sizeof']:
                if not check_camel(name):
                    issues.append((line_num, f"Fonction/Méthode mal formattée (camelCase/lower_case requis) : '{name}'"))
            continue

        # 3. Variables membres / globales / locales / paramètres (Attendu : camelCase ou lower_case, ou UPPER_SNAKE_CASE si constante)
        # Recherche des déclarations typées de variables : int maVar = ...; ou const int MON_VAL = ...;
        var_match = re.search(r'\b(const\s+)?[a-zA-Z0-9_<>]+(?:\s+|\s*\*\s*|\s*&\s*)([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:=|;|,|\))', line)
        if var_match:
            is_const = var_match.group(1) is not None
            name = var_match.group(2)
            
            # Filtrage des mots-clés parasites
            if name not in ['return', 'sizeof', 'delete', 'new', 'if', 'for', 'while', 'switch', 'class', 'struct']:
                if is_const:
                    if not check_upper_snake(name) and not check_camel(name):
                        issues.append((line_num, f"Constante mal formattée (UPPER_SNAKE_CASE ou camelCase requis) : '{name}'"))
                else:
                    if not check_camel(name):
                        issues.append((line_num, f"Variable/Paramètre mal formatté(e) (camelCase/lower_case requis) : '{name}'"))

    return issues

def main():
    print("=== Audit complet de tous les symboles C++ ===")
    total_issues = 0
    
    for dirpath, dirnames, filenames in os.walk(".", topdown=True):
        dirnames[:] = [d for d in dirnames if d not in IGNORED_DIRS]
        
        for filename in filenames:
            if not filename.endswith(('.cpp', '.h', '.c', '.hpp')):
                continue
                
            file_path = os.path.join(dirpath, filename)
            issues = audit_file(file_path)
            
            if issues:
                print(f"\nFichier : {file_path}")
                for line_num, msg in issues:
                    print(f"  Ligne {line_num}: {msg}")
                    total_issues += 1

    print(f"\n--- Analyse terminée : {total_issues} anomalie(s) potentielle(s) détectée(s) ---")

if __name__ == "__main__":
    main()