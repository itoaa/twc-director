# TWC Director (ESPHome)

ESPHome-komponent för att styra **Tesla Wall Connector Gen2** (HPWC) via RS-485.  
Detta är en **egen fork** baserad på [Wired-Square/esphome-twc-director](https://github.com/Wired-Square/esphome-twc-director) (MIT), avsedd för att:

- fungera med nyare ESPHome (t.ex. **2026.7.x**)
- kunna anpassas efter egna behov
- utvecklas och lagras på vår GitHub

## Bakgrund / status

Upstream-projektet (`Wired-Square/esphome-twc-director`) rapporteras inte fungera med ESPHome **2026.7.3**.  
Den här forken är arbetsytan där vi kan fixa kompatibilitet och lägga till egna ändringar.

| | |
|---|---|
| Upstream | https://github.com/Wired-Square/esphome-twc-director |
| Licens | MIT (behåller copyright från Wired Square + egna ändringar) |
| Mål | Tesla Gen2 Wall Connector (inte Gen3) |

## Översikt

Komponenten agerar **TWC Director** (master) på RS-485-bussen och ger:

- **Load sharing** över flera Wall Connectors (upp till 4: 1 master + 3 peripherals)
- **Övervakning** av ström, spänning, energi, VIN, status
- **Styrning** av contactor, max/session-ström m.m. från Home Assistant
- Automatisk discovery/bindning av EVSE:er

## Hårdvara

1. **ESP32** (t.ex. ESP32-WROOM)
2. **RS-485-transceiver** (MAX13487E, MAX485 eller liknande)
3. **Tesla Gen2 Wall Connector(s)** med RS-485

### Typisk inkoppling (exempel-YAML)

| ESP32 | RS-485 | Funktion |
|-------|--------|----------|
| GPIO22 | TX/DI | UART TX |
| GPIO21 | RX/RO | UART RX |
| GPIO16 | EN (boost) | Valfri 5V boost |
| GPIO19 | ~SHDN | HIGH = enable |
| GPIO17 | RE | HIGH = receive enable |
| 3.3V / GND | VCC / GND | Ström |

A/B på transceivern kopplas till TWC:ns RS-485-terminaler. UART: **9600 8N1**.

## Installation

### 1. Lokalt i det här repot (utveckling)

```bash
# Klona din fork (när den ligger på GitHub)
git clone https://github.com/<DITT-GITHUB-ANVANDARNAMN>/twc-director.git
cd twc-director

# Hemligheter
cp secrets.yaml.example secrets.yaml
# Redigera secrets.yaml

# Kompilera (kräver ESPHome ≥ 2026.x / Python ≥ 3.12)
esphome compile tesla-director.yaml
esphome run tesla-director.yaml
```

Exempel-YAML:n pekar som standard på **lokal** `components/`-katalog (bra för utveckling).

### 2. Från Home Assistant / Device Builder

När forken ligger på GitHub:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/<DITT-GITHUB-ANVANDARNAMN>/twc-director.git
      # ref: main   # valfritt: pinna branch/tag
```

Kopiera och anpassa `tesla-director.yaml`.

### ESPHome 2026.7.x

I ESPHome **2026.7** är native ESP-IDF standard-toolchain för ESP32. Exempel-configen använder redan `framework: type: esp-idf`.

Om bygget faller på toolchain-relaterade fel, prova att tvinga PlatformIO-toolchain (se kommenterad rad i `tesla-director.yaml`):

```yaml
esp32:
  board: esp32dev
  framework:
    type: esp-idf
  toolchain: platformio   # fallback om native toolchain strular
```

**Krav:** Python **3.12+** (ESPHome 2026.7 droppade 3.11).

## Konfiguration (kort)

Se `tesla-director.yaml` för full exempel-setup. Viktiga nycklar under `twc_director:`:

| Nyckel | Betydelse |
|--------|-----------|
| `addr` | Director-adress på bussen (t.ex. `0xF00D`) |
| `global_max_current` | Total maxström för alla TWC |
| `global_twc_max_current` | Max per TWC |
| `master_mode` | Switch: director aktiv |
| `evse` | Lista med Wall Connectors / slots |

## Utveckling

```
twc-director/
├── components/twc_director/     # ESPHome external component
│   ├── __init__.py
│   ├── twc_director_component.*
│   └── twc/                     # C-protokollbibliotek (SLIP + TWC)
├── tesla-director.yaml          # Exempel-firmwareconfig
├── secrets.yaml.example
└── README.md
```

### Git remotes

Efter setup:

```bash
git remote -v
# origin    -> din GitHub-fork (lägg till när repot skapats)
# upstream  -> https://github.com/Wired-Square/esphome-twc-director.git
```

Hämta upstream-ändringar:

```bash
git fetch upstream
git merge upstream/main   # eller rebase
```

## Felsökning

1. **Ingen kommunikation** – A/B, GND, enable-pinnar, 9600 8N1
2. **Offline** – `logger: level: VERBOSE`, kolla `link_ok`
3. **Strömgränser** – master mode ON, globala gränser
4. **Kompileringsfel efter ESPHome-uppgradering** – clean build, se sektion om 2026.7 ovan

## Licens

MIT. Upstream copyright: **Wired Square** (2025).  
Egna ändringar i denna fork: se git-historik.

## Tack

- [Wired-Square/esphome-twc-director](https://github.com/Wired-Square/esphome-twc-director)
- Tesla Motors Club / protokoll-reverse-engineering-communityn
- ESPHome-teamet
