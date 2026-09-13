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
| CI | GitHub Actions kompilerar `tesla-director.yaml` mot ESPHome **2026.7.3** och `latest` |

[![CI](https://github.com/itoaa/twc-director/actions/workflows/ci.yml/badge.svg)](https://github.com/itoaa/twc-director/actions/workflows/ci.yml)

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
| GPIO18 | DE+RE | MAX485: knyt DE och RE hit (`flow_control_pin`). HIGH=sänd, LOW=lyssna |
| GPIO16 | EN (boost) | Valfri 5V boost (TCAN485) |
| GPIO19 | ~SHDN | HIGH = enable (TCAN485) |
| GPIO17 | RE | MAX13487E: HIGH = receive enable. Används inte med MAX485 |
| 3.3V / GND | VCC / GND | Ström |

A/B på transceivern kopplas till TWC:ns RS-485-terminaler. UART: **9600 8N1**.

**MAX485 (DI/DE/RE/RO):** knyt **DE och RE ihop** till `flow_control_pin` (GPIO18 i exempel-YAML). Koppla **inte** GPIO17 till MAX485-RE — den pinnen är för MAX13487E (aktiv hög) och har motsatt polaritet. GPIO16/19 är TCAN485-boost/shutdown och behövs inte på ett vanligt MAX485-kort.

**MAX13487E / TCAN485:** utelämna `flow_control_pin` (auto-direction). Behåll GPIO16/17/19 enligt YAML.

## Installation

### 1. Lokalt i det här repot (utveckling)

```bash
# Klona din fork (när den ligger på GitHub)
git clone https://github.com/itoaa/twc-director.git
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
      url: https://github.com/itoaa/twc-director.git
      ref: evcc-ha-adapter
    components: [twc_director]
    refresh: 0s
```

Kopiera och anpassa `tesla-director.yaml`. Använd **inte** `Wired-Square/esphome-twc-director` — den branchen finns bara på `itoaa/twc-director`.

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
| `flow_control_pin` | Valfri GPIO till MAX485 DE+RE (HIGH=sänd, LOW=lyssna). Utelämna för auto-direction |
| `global_max_current` | Total maxström för alla TWC (`cap global max`) |
| `global_twc_max_current` | Max per TWC |
| `master_mode` (`td master mode`) | Switch: director aktiv |
| `evse` | Lista med Wall Connectors / slots (exempel-YAML har två) |

Entitetsnamn visar vem som **skriver** värdet:

| Prefix | Skriver | Exempel |
|--------|---------|---------|
| `twc_` | Tesla-director från RS-485 | `twc charge status`, `twc vin`, `twc power` |
| `evcc_` | evcc via Home Assistant | `evcc charge enable`, `evcc offered current` |
| `cap_` | du / YAML, hårda tak | `cap max current`, `cap global max` |
| `td_` | Tesla-director lokalt | `td master mode`, `td bus enable`, `td evcc watchdog ok` |

### Home Assistant → evcc

evcc `template: homeassistant` per TWC (Slot 0 visad). Status är `A`/`B`/`C`/`F`.

```yaml
chargers:
  - name: twc_slot_0
    type: template
    template: homeassistant
    uri: http://homeassistant.local:8123
    status: sensor.tesla_director_slot_0_twc_charge_status
    enabled: switch.tesla_director_slot_0_evcc_charge_enable
    enable: switch.tesla_director_slot_0_evcc_charge_enable
    setMaxCurrent: number.tesla_director_slot_0_evcc_offered_current
    power: sensor.tesla_director_slot_0_twc_power
    energy: sensor.tesla_director_slot_0_twc_energy_total
    currentL1: sensor.tesla_director_slot_0_twc_current_l1
    currentL2: sensor.tesla_director_slot_0_twc_current_l2
    currentL3: sensor.tesla_director_slot_0_twc_current_l3
    voltageL1: sensor.tesla_director_slot_0_twc_voltage_l1
    voltageL2: sensor.tesla_director_slot_0_twc_voltage_l2
    voltageL3: sensor.tesla_director_slot_0_twc_voltage_l3
```

`twc charge status`: **A** disconnected, **B** connected/waiting, **C** charging, **F** RS-485 nere / TWC offline / error. Klartext ligger i `twc charge status text`. evcc:s Home Assistant-GUI har bara A/B/C — **F** ger `unknown charge status` där; peka evcc på A/B/C och använd `twc link ok` för bussfel.

VIN publiceras som `twc vin`. HA-mallen har inget identify-fält; använd evcc `vehicles.identifiers` eller custom charger `identify`.

Lastbalans: evcc skriver **`evcc offered current`** (`setMaxCurrent`), inte `twc available current` (det är bara avläsning). 0 A = stopp, ≥6 A = tak. Director klampar mot `cap_*`. Om evcc slutar skriva i 60 s sänks session till 0 A (`td evcc watchdog ok` blir av).

## Utveckling

```
twc-director/
├── components/twc_director/     # ESPHome external component
│   ├── __init__.py
│   ├── twc_director_component.*
│   ├── twc_*.c / twc_*.h        # C-protokollbibliotek (SLIP + TWC)
│   └── PROTOCOL.md
├── .github/workflows/ci.yml     # Kompilerar mot ESPHome 2026.7.3 + latest
├── tesla-director.yaml          # Lab-exempel (två slots, evcc-entiteter, MAX485 GPIO18)
├── tesla-director-safe.yaml     # Produktion: ingen web_server
├── docs/SECURITY.md
├── secrets.yaml.example
└── README.md
```

> **OBS (ESPHome 2026.7):** External components kopierar inte undermappar till bygget.
> Protokollkällorna ligger därför i komponentroten (inte under `twc/`).

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

## Safety profile (P0/P1)

Software hardening in this fork (see [docs/SECURITY.md](docs/SECURITY.md)):

- **`global_max_current` is required** and must be `> 0`
- Runtime global-max number **updates the C core** (session reconciliation)
- Contactor commands require **master mode** + EVSE enabled; UI follows bus current
- **Master mode off at boot** (enable explicitly in HA)
- Master disable → fail-safe **0 A session + open contactors**, then stop TX
- Production example: **`tesla-director-safe.yaml`** (no `web_server`)

## Web UI security (HTTP vs HTTPS)

ESPHomes inbyggda `web_server` lyssnar som standard på **HTTP port 80**. Det finns **inget officiellt stöd för TLS/HTTPS direkt på enheten** (öppen feature request: [esphome/feature-requests#2432](https://github.com/esphome/feature-requests/issues/2432)).

### Policy i den här forken

**Aktivera inte `web_server.auth` över ren HTTP.**  
Lösenord (Basic) eller auth-trafik skickas då på en okrypterad länk. Vi anser att det är bättre med **ingen web-auth** och att lita på nätverkssegmentering än att lura sig själv med “lösenordsskydd” i klartext.

Exempel-configen (`tesla-director.yaml`) har därför **ingen** `auth:` under `web_server:`.

### Praktiska alternativ

| Alternativ | Kryptering | Auth | Kommentar |
|------------|------------|------|-----------|
| **Ingen auth + IoT-VLAN** (default här) | Nej | Nej | Enklast. Styr via Home Assistant API (som *är* krypterad). Web-UI bara på betrodd LAN. |
| **HTTP Digest-auth** | Nej (hela UI:t är fortfarande HTTP) | Ja | Lösenordet skickas *inte* i klartext (bara challenge/hash). UI och REST-trafik är fortfarande synliga. Bättre än Basic, sämre än HTTPS. |
| **HTTP Basic-auth** | Nej | Ja | **Undvik.** Lösenordet går Base64-kodat (lätt att avkoda) på varje request. |
| **Reverse proxy med HTTPS** (nginx, Caddy, Traefik, HA proxy) | Ja (till klienten) | På proxyn | Rätt sätt om du vill ha lösenord + webbläsare. Enheten bakom proxyn kan fortsätta prata HTTP internt på isolerat nät. |
| **Stäng av web_server** | — | — | Bäst om du bara behöver HA. Kommentera bort `web_server:` / `captive_portal:` om du inte behöver dem. |

### Reverse proxy (rekommenderat om du vill ha lösenord)

1. Ge enheten fast IP på IoT-nätet.
2. Terminera TLS på t.ex. Caddy/nginx med giltigt cert (Let’s Encrypt eller intern CA).
3. Proxyn kräver auth (eller mTLS) och proxar till `http://tesla-director:80`.
4. Lämna **ingen** `auth:` på ESPHome-enheten (undvik dubbel-auth och credential-läckage på sista hoppet om det inte är isolerat).

Exempel (Caddy, förenklat):

```text
tesla-director.example.com {
    reverse_proxy 192.168.30.50:80
    basicauth {
        admin $2a$14$...hashed...
    }
}
```

### Om du ändå vill ha auth direkt på enheten (HTTP)

Använd **digest**, aldrig basic:

```yaml
web_server:
  port: 80
  auth:
    type: digest   # lösenord skickas inte i klartext; UI är fortfarande HTTP
    username: !secret web_username
    password: !secret web_password
```

Det skyddar **inte** mot avlyssning av sensorvärden/kommandon i UI:t — bara mot att själva lösenordssträngen läcker.

### Vad som redan är krypterat

- **Home Assistant API** (`api.encryption`) – den vanliga styrvägen.
- **OTA** (lösenordsskyddad native OTA) – separat från web-UI.

Web-UI:t är främst för felsökning; produktionsstyrning bör gå via HA.

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
