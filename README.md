# AutoMaster — plugin de mastering adaptatif (VST3)

Le plugin **écoute ton morceau**, puis règle tout seul :
- un **EQ adaptatif** (9 bandes) qui rapproche le spectre d'une courbe de référence de mastering ;
- une **compression douce** de liant (avec compensation automatique du volume) ;
- la **largeur stéréo** et la **tonalité** (curseurs manuels) ;
- un **gain automatique** pour atteindre le volume visé (en LUFS, par défaut -14) ;
- un **limiteur à anticipation** (plafond réglable, -1 dB par défaut). Latence déclarée au DAW (compensation automatique).

> Honnêteté : c'est un **assistant de mastering**, pas un remplaçant d'Ozone ou d'un ingénieur. Il donne un master propre et cohérent, à affiner à l'oreille.
> Le moteur a été testé hors-ligne (plafond respecté, latence exacte, silence, 44.1/48/96 kHz). **L'interface et le chargement dans FL Studio n'ont pas pu être testés de mon côté** : c'est à toi de faire le premier essai.

---

## TUTO 1 — Obtenir le fichier .vst3 (sans rien installer)

Le plugin est compilé gratuitement par GitHub, sur un PC Windows dans le cloud.

1. Crée un compte sur **github.com** (gratuit) si tu n'en as pas.
2. En haut à droite : **+ → New repository**. Nom : `automaster`. Laisse **Public**. Clique **Create repository**.
3. Sur la page du dépôt, clique **uploading an existing file**.
4. **Dézippe** le ZIP sur ton PC. Ouvre le dossier `automaster` puis **sélectionne tout son contenu** (dossiers `Source`, `tests`, `.github`, et les fichiers `CMakeLists.txt`, `README.md`) et **glisse-le** dans la page GitHub.
   - Le dossier `.github` est indispensable : c'est lui qui lance la compilation. S'il n'apparaît pas dans l'explorateur, active *Affichage → Éléments masqués*.
5. En bas, clique **Commit changes**.
6. Va dans l'onglet **Actions**. Si GitHub demande d'activer les workflows, accepte (*I understand my workflows, go ahead and enable them*).
7. Le build « **Build VST3 (Windows)** » démarre tout seul (sinon : clique dessus → **Run workflow**). Compte **10 à 20 minutes** la première fois.
8. Quand il est **vert ✅**, clique sur le build, descends tout en bas à **Artifacts** et télécharge **AutoMaster-VST3-Windows**.
9. Dézippe : tu obtiens un dossier **`AutoMaster.vst3`**.

Si le build est **rouge ❌** : clique dessus, copie les dernières lignes d'erreur et envoie-les-moi, je corrige.

## TUTO 2 — L'installer dans FL Studio

1. Copie le dossier **`AutoMaster.vst3`** (le dossier entier) dans :
   `C:\Program Files\Common Files\VST3\`
   (Windows demande l'autorisation administrateur : accepte.)
2. Ouvre **FL Studio** → menu **Options → Manage plugins**.
3. Vérifie que `C:\Program Files\Common Files\VST3` est dans la liste des dossiers, puis clique **Find installed plugins** (Start scan).
4. **AutoMaster** apparaît dans la liste : clique l'étoile pour le mettre en favori.
5. Ouvre le **Mixer**, sélectionne la piste **Master**, clique un slot d'effet vide (en bas à droite) → choisis **AutoMaster**.
   Place-le **en dernier** sur le Master, et enlève tout autre limiteur.

## TUTO 3 — Utilisation

1. **Baisse le volume de ton casque/enceintes avant le premier essai.**
2. Lance la lecture sur la partie **la plus forte** du morceau (le refrain).
3. Clique le gros bouton **« 1. Apprendre le morceau »** (il devient orange) et laisse tourner **20 à 30 secondes**.
4. Clique à nouveau pour l'arrêter : le plugin affiche *« Morceau appris : … LUFS »* et règle tout.
5. Relance la lecture et ajuste à l'oreille :
   - **Intensité de l'EQ automatique** : 0 % = aucune correction, 100 % = correction complète (70 % par défaut).
   - **Tonalité** : plus sombre / plus clair.
   - **Compression douce**, **Largeur stéréo** (garde 100 % si tu hésites).
   - **Volume visé** : -14 LUFS pour le streaming, -9 à -11 pour plus fort.
   - **Plafond** : -1 dB convient à tout le streaming.
6. En bas de la fenêtre : *Sortie* (volume réel en LUFS), *Limiteur* (réduction en dB, vise moins de 3 dB), *Gain auto*.
7. Les réglages appris sont **enregistrés dans ton projet FL**.

Pour exporter : rends ton morceau normalement (WAV 24 bits), plugin activé sur le Master.

## Limites connues
- L'analyse porte sur ce que tu joues pendant l'apprentissage : fais-la sur un passage représentatif.
- Si le morceau est très pointu (peu de « corps »), le gain est volontairement limité à ~5 dB de limitation pour ne pas l'écraser : le volume visé peut ne pas être atteint. Travaille alors le mix.
- Le limiteur fixe le plafond sur les échantillons (pas sur les « true peaks » sur-échantillonnés) : garde -1 dB de plafond.
- Stéréo uniquement. Windows (VST3) pour l'instant.

## Pour les développeurs
- `Source/AutoMasterEngine.h` : tout le traitement (indépendant de l'interface).
- `tests/engine_test.cpp` : test hors-ligne du moteur (`-DAUTOMASTER_BUILD_TESTS=ON`).
- Compilation locale : `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target AutoMaster_VST3`
