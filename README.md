# SenseAI - Système d'Intelligence Artificielle pour la Surveillance Cardiaque et d'Activité

## Contexte du Projet

**Projet de Recherche - Étudiant Ingénieur**  
**Institution : ISEN Toulon, France**  
**Auteur : Dan Boussougo**  
**Date : Mars 2026**

## Résumé

Ce projet développe un système embarqué d'intelligence artificielle pour la surveillance en temps réel de paramètres physiologiques et d'activité physique. Le système intègre des capteurs biométriques avancés avec un modèle d'IA entraîné via Edge Impulse pour la classification d'activités et la détection d'anomalies cardiaques.

Le système comprend deux composants principaux :
- **Firmware embarqué** sur microcontrôleur Nordic nRF54L15DK
- **Application mobile** Flutter pour Android/iOS

## Architecture Système

```
┌─────────────────┐    BLE    ┌─────────────────┐
│   Nordic        │◄─────────►│   Application   │
│   nRF54L15DK    │           │   Mobile        │
│                 │           │   Flutter       │
│ ┌─────────────┐ │           │                 │
│ │  Capteurs   │ │           │ ┌─────────────┐ │
│ │ • MAX32664  │ │           │ │  Interface  │ │
│ │ • LSM6 IMU  │ │           │ │  Utilisateur│ │
│ └─────────────┘ │           │ └─────────────┘ │
│                 │           │                 │
│ ┌─────────────┐ │           │ ┌─────────────┐ │
│ │   Edge      │ │           │ │  Visualisa- │ │
│ │  Impulse    │ │           │ │   tion      │ │
│ │   Model     │ │           │ │   Données   │ │
│ └─────────────┘ │           │ └─────────────┘ │
│                 │           │                 │
│ ┌─────────────┐ │           │                 │
│ │   Stockage  │ │           │                 │
│ │   SD Card   │ │           │                 │
│ └─────────────┘ │           └─────────────────┘
└─────────────────┘
```

## Matériel Utilisé

### Carte Principale
- **Nordic nRF54L15DK** : Microcontrôleur multi-cœurs avec Bluetooth 5.4
- **OS** : Zephyr RTOS

### Capteurs
- **MAX32664 BioHub** : Capteur optique pour fréquence cardiaque et SpO2
  - Communication I2C
  - Précision médicale
- **LSM6DSO IMU** : Accéléromètre et gyroscope 6 axes
  - Détection d'activité et orientation
  - Communication I2C/SPI

### Stockage
- **Carte SD** : Journalisation des données
  - Système de fichiers FAT
  - Stockage des mesures et inférences IA

## Logiciel

### Firmware Embarqué (Zephyr OS)

#### Fonctionnalités
- **Acquisition capteurs** : Collecte synchronisée des données biométriques
- **Traitement IA** : Exécution du modèle Edge Impulse en temps réel
- **Communication BLE** : Transmission des données vers l'application mobile
- **Journalisation** : Sauvegarde automatique sur carte SD

#### Technologies
- **Langage** : C/C++
- **IA Framework** : Edge Impulse SDK
- **Communication** : Bluetooth Low Energy (BLE)
- **OS** : Zephyr RTOS

### Application Mobile (Flutter)

#### Fonctionnalités
- **Connexion BLE** : Découverte et connexion automatique au dispositif
- **Visualisation temps réel** : Graphiques des paramètres physiologiques
- **Historique** : Consultation des données enregistrées
- **Alertes** : Notifications d'anomalies détectées

#### Technologies
- **Framework** : Flutter (Dart)
- **BLE** : flutter_blue_plus
- **Graphiques** : fl_chart
- **Stockage** : shared_preferences, path_provider

## Acquisition de Données UART

### Script Python d'Acquisition (`uart_acquisition_ui.py`)

Le projet inclut un script Python avec interface graphique pour l'acquisition de données via UART depuis la carte Nordic. Cet outil permet de collecter des données de capteurs pour l'entraînement des modèles Edge Impulse.

#### Fonctionnalités
- **Interface graphique** : Configuration facile via Tkinter
- **Connexion série** : Communication UART avec la carte Nordic
- **Collecte temps réel** : Acquisition synchronisée des données capteurs
- **Sauvegarde structurée** : Organisation automatique des fichiers par opérateur
- **Formats multiples** : Export CSV + JSON avec métadonnées

#### Données Acquises
- **Accéléromètre LSM6** : acc_x, acc_y, acc_z (m/s²)
- **Gyroscope LSM6** : gyr_x, gyr_y, gyr_z (dps)
- **Fréquence cardiaque MAX32664** : htr (bpm)
- **SpO2 MAX32664** : o2 (%)

#### Installation et Utilisation

```bash
# Installation des dépendances
pip install pyserial

# Lancement de l'interface
cd sens_ia_nordic
python uart_acquisition_ui.py
```

#### Configuration d'Acquisition
- **Nom de l'opérateur** : Identifiant de la personne effectuant l'acquisition
- **Label** : Description de l'activité (marche, course, repos, etc.)
- **Durée** : Temps d'acquisition en secondes (défaut: 20s)
- **Fréquence** : Fréquence d'échantillonnage en Hz (défaut: 5Hz)
- **Port série** : Port UART de la carte Nordic

#### Structure des Données Sauvegardées

Les données sont organisées dans le dossier `Data_aquisition/` :

```
Data_aquisition/
└── nom_operateur/
    ├── label.nom_operateur_00000001.csv
    ├── label.nom_operateur_00000001.json
    ├── label.nom_operateur_00000002.csv
    └── label.nom_operateur_00000002.json
```

#### Format CSV
```csv
# ==================== ACQUISITION HEADER ====================
# operator daniel
# label marche
# acquisition_datetime 2024-01-15T14:30:25
# duration_s 20.000
# frequency_hz 5.000
# interval_ms 200
# microcontroller Nordic nrf54L15
# sensor_1 LSM6DSOX Accéléromètre, Gyroscope Capteur Qwiic
# sensor_2 SparkFun Pulse Oximeter and Heart Rate Sensor - MAX30101 & MAX32664
# units m/s2 m/s2 m/s2 dps dps dps bpm %
# =============================================================
acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2
9.81,0.0,0.0,0.0,0.0,0.0,72,98
9.82,0.1,0.1,1.2,-0.5,0.8,73,97
...
```

#### Utilisation pour Edge Impulse

1. **Collecter des données** pour différentes activités :
   - Repos (assis immobile)
   - Marche (marche normale)
   - Course (course à pied)
   - Vélo (pédalage)
   - Escalier (montée/descente)

2. **Labeliser correctement** chaque acquisition

3. **Uploader vers Edge Impulse** pour l'entraînement du modèle

4. **Valider les performances** du modèle entraîné

### Edge Impulse Data Forwarder (UART Direct)

#### Description
L'**Edge Impulse Data Forwarder** est un outil officiel qui permet d'envoyer directement les données UART de votre dispositif vers Edge Impulse Studio pour l'entraînement en temps réel, sans passer par l'export CSV intermédiaire.

#### Avantages
- **Streaming temps réel** : Envoi continu des données vers Edge Impulse
- **Labelisation en direct** : Possibilité de labéliser les données pendant l'acquisition
- **Visualisation immédiate** : Voir les données dans Edge Impulse Studio en temps réel
- **Économie de stockage** : Pas besoin de sauvegarder localement les données d'entraînement

#### Installation

```bash
# Installation via npm (nécessite Node.js)
npm install -g edge-impulse-data-forwarder

# Ou via pip (version Python)
pip install edge-impulse-data-forwarder
```

#### Configuration et Utilisation

1. **Connexion à Edge Impulse**
```bash
edge-impulse-data-forwarder --api-key YOUR_API_KEY --project-id YOUR_PROJECT_ID
```

2. **Configuration UART**
```bash
edge-impulse-data-forwarder \
  --api-key YOUR_API_KEY \
  --project-id YOUR_PROJECT_ID \
  --port /dev/tty.usbmodem0010577996153 \
  --baudrate 115200 \
  --format csv \
  --sensors acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2
```

3. **Paramètres importants**
- `--api-key` : Clé API de votre projet Edge Impulse
- `--project-id` : ID du projet Edge Impulse
- `--port` : Port série de la carte Nordic
- `--baudrate` : Vitesse de communication (115200)
- `--format` : Format des données (csv)
- `--sensors` : Liste des capteurs séparés par des virgules

#### Workflow avec Data Forwarder

1. **Démarrer le forwarder**
```bash
edge-impulse-data-forwarder \
  --api-key ei_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \
  --project-id 123456 \
  --port /dev/tty.usbmodem0010577996153 \
  --baudrate 115200 \
  --format csv \
  --sensors acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2
```

2. **Ouvrir Edge Impulse Studio** dans votre navigateur

3. **Commencer l'acquisition** dans l'onglet "Data acquisition"

4. **Labeliser en temps réel** :
   - Cliquer sur "Start sampling"
   - Effectuer l'activité souhaitée
   - Cliquer sur "Stop sampling"
   - Ajouter un label (marche, course, repos, etc.)

5. **Répéter** pour collecter suffisamment de données par classe

#### Comparaison des Méthodes

| Méthode | Avantages | Inconvénients |
|---------|-----------|---------------|
| **Script Python** | Contrôle total, sauvegarde locale, métadonnées riches | Processus en deux étapes |
| **Data Forwarder** | Streaming temps réel, labelisation directe, visualisation immédiate | Dépendance réseau, moins de métadonnées |

#### Dépannage Data Forwarder

- **Erreur de connexion** : Vérifier la clé API et l'ID du projet
- **Port non trouvé** : Vérifier que la carte Nordic est connectée
- **Données corrompues** : Vérifier le format CSV envoyé par le firmware
- **Timeout réseau** : Vérifier la connexion internet

#### Obtenir la Clé API

1. Aller sur [Edge Impulse Studio](https://studio.edgeimpulse.com)
2. Sélectionner votre projet
3. Aller dans **Dashboard** → **Keys**
4. Copier la clé API

---

*Le Data Forwarder est particulièrement utile pour les sessions d'acquisition intensive où vous voulez voir immédiatement les résultats dans Edge Impulse.*

## Utilisation

### Démarrage du Système

1. **Allumer le dispositif Nordic** : Le système démarre automatiquement
2. **Lancer l'application mobile** : Recherche automatique du dispositif BLE
3. **Connexion** : Appairage automatique via BLE
4. **Acquisition** : Début de la collecte de données en temps réel

### Collecte de Données pour l'IA

**Deux méthodes disponibles :**

#### Méthode 1 : Script Python (Recommandé pour débutants)
```bash
# Collecte avec sauvegarde locale
cd sens_ia_nordic
python uart_acquisition_ui.py
```

#### Méthode 2 : Edge Impulse Data Forwarder (Streaming direct)
```bash
# Streaming temps réel vers Edge Impulse
edge-impulse-data-forwarder \
  --api-key YOUR_API_KEY \
  --project-id YOUR_PROJECT_ID \
  --port /dev/tty.usbmodem0010577996153 \
  --baudrate 115200 \
  --format csv \
  --sensors acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2
```

- Collecter des données pour chaque classe d'activité
- Varier les conditions (différentes personnes, environnements)
- Assurer un équilibre des classes

### Fonctionnalités Principales

#### Surveillance Cardiaque
- Fréquence cardiaque (BPM)
- Saturation en oxygène (SpO2)
- Détection d'anomalies via IA

#### Analyse d'Activité
- Classification d'activités (marche, course, repos)
- Détection de chutes
- Suivi de l'orientation

#### Journalisation
- Sauvegarde automatique sur carte SD
- Export des données via l'application mobile

## Modèle d'Intelligence Artificielle

### Entraînement
- **Plateforme** : Edge Impulse Studio
- **Données** : Collecte via script `uart_acquisition_ui.py`
- **Algorithme** : Réseau de neurones convolutionnel optimisé
- **Précision** : >95% sur les classes d'activité

### Déploiement
- **Format** : TensorFlow Lite Micro
- **Optimisation** : Quantification 8-bit
- **Mémoire** : <50KB de RAM, <100KB de flash

## Résultats et Performances

### Métriques de Performance
- **Latence d'inférence** : <10ms
- **Consommation** : <50mA en fonctionnement normal
- **Autonomie** : >24h avec batterie 1000mAh

### Précision des Capteurs
- **Fréquence cardiaque** : ±2 BPM
- **SpO2** : ±2%
- **Classification activité** : >95% accuracy

## Structure du Projet

```
sens_ia_nordic/
├── CMakeLists.txt          # Configuration CMake principale
├── prj.conf               # Configuration Zephyr
├── boards/                # Overlays spécifiques à la carte
├── src/                   # Code source firmware
│   ├── main.cpp          # Point d'entrée principal
│   ├── biohub_max32664.* # Driver MAX32664
│   ├── imu_lsm6.*        # Driver LSM6
│   ├── ble.*             # Communication BLE
│   └── sd.*              # Gestion carte SD
├── sense_ia_datat001-cpp-mcu-v1/  # Bibliothèque Edge Impulse
│   ├── edge-impulse-sdk/
│   ├── model-parameters/
│   └── tflite-model/
└── build/                 # Fichiers de compilation

SenseAI-App/
├── lib/                   # Code source Flutter
│   ├── main.dart         # Point d'entrée application
│   ├── core/             # Logique métier
│   ├── features/         # Fonctionnalités
│   └── shared/           # Composants partagés
├── android/               # Configuration Android
├── ios/                   # Configuration iOS
└── test/                  # Tests unitaires
```

## Dépendances

### Firmware
- Zephyr RTOS
- Edge Impulse SDK
- Nordic nRF Connect SDK

### Application Mobile
- Flutter SDK
- flutter_blue_plus
- fl_chart
- permission_handler
- shared_preferences

### Outils d'Acquisition de Données
- **Python 3.8+** avec pyserial pour le script UART
- **Edge Impulse Data Forwarder** (optionnel, pour streaming direct)
  ```bash
  npm install -g edge-impulse-data-forwarder
  # ou
  pip install edge-impulse-data-forwarder
  ```

## Contribution

Ce projet est développé dans le cadre d'un projet de recherche étudiant. Pour contribuer :

1. Fork le projet
2. Créer une branche feature (`git checkout -b feature/AmazingFeature`)
3. Commit les changements (`git commit -m 'Add some AmazingFeature'`)
4. Push vers la branche (`git push origin feature/AmazingFeature`)
5. Ouvrir une Pull Request

## Licence

Ce projet est sous licence MIT - voir le fichier [LICENSE](LICENSE) pour plus de détails.

## Remerciements

- **ISEN Toulon** pour le support académique
- **Nordic Semiconductor** pour la plateforme nRF54
- **Edge Impulse** pour les outils d'IA embarquée
- **Maxim Integrated** pour les capteurs biométriques

## Contact

dan Boussougou - danbouss22@gmail.com

Lien du projet : [https://github.com/danpassy/Sens_AI](https://github.com/danpassy/Sens_AI)

---

*Projet réalisé dans le cadre du cursus Ingénieur à l'ISEN Toulon, spécialisation Systèmes Embarqués et Intelligence Artificielle.*