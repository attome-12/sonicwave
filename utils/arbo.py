from pathlib import Path
import argparse


def afficher_arborescence(
	racine: Path,
	prefixe: str = "",
	dossiers_ignores: set[str] | None = None,
	uniquement_dossiers: bool = False,
) -> None:
	"""Affiche récursivement l'arborescence à partir de *racine*."""
	dossiers_ignores = dossiers_ignores or set()
	elements = [
		element
		for element in racine.iterdir()
		if element.name not in dossiers_ignores
		and (not uniquement_dossiers or element.is_dir())
	]
	elements.sort(key=lambda element: (element.is_file(), element.name.lower()))

	for index, element in enumerate(elements):
		dernier = index == len(elements) - 1
		branche = "└── " if dernier else "├── "
		print(f"{prefixe}{branche}{element.name}")

		if element.is_dir():
			afficher_arborescence(
				element,
				prefixe + ("    " if dernier else "│   "),
				dossiers_ignores,
				uniquement_dossiers,
			)


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="Affiche l'arborescence d'un dossier.")
	parser.add_argument("racine", nargs="?", type=Path, default=Path.cwd())
	parser.add_argument(
		"-i",
		"--ignorer",
		dest="dossiers_ignores",
		action="append",
		default=[],
		help="Nom d'un dossier à ignorer (option répétable).",
	)
	parser.add_argument(
		"-d",
		"--dossiers-uniquement",
		action="store_true",
		help="N'affiche que les dossiers.",
	)
	arguments = parser.parse_args()
	dossier_courant = arguments.racine
	print(dossier_courant.name or str(dossier_courant))
	afficher_arborescence(
		dossier_courant,
		dossiers_ignores=set(arguments.dossiers_ignores),
		uniquement_dossiers=arguments.dossiers_uniquement,
	)
