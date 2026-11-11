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

1. **ESP32** – referensuppsättningen är en **Wemos D1 mini ESP32**
2. **RS-485-transceiver** – referensuppsättningen är ett **MAX485-kort på 3,3 V** (MAX13487E/T-CAN485 fungerar också)
3. **Tesla Gen2 Wall Connector(s)** med RS-485 (i Sverige 3-fas, se [docs/TWC-RS485-Sverige.md](docs/TWC-RS485-Sverige.md))

### Inkoppling (D1 mini ESP32 + MAX485, matchar `tesla-director.yaml`)

| ESP32 (D1 mini) | MAX485 | Funktion |
|-----------------|--------|----------|
| GPIO22 | DI | UART TX (`uart: tx_pin`) |
| GPIO21 | RO | UART RX (`uart: rx_pin`) |
| GPIO18 | DE + RE (hopknutna) | `twc_director: flow_control_pin`. HIGH = sänd, LOW = lyssna |
| 3,3 V | VCC | MAX485 matas med **3,3 V** |
| GND | GND | **Gemensam GND** med Wall Connectorerna |

A/B på transceivern kopplas till TWC:ns RS-485-terminaler (A↔A, B↔B). UART: **9600 8N1**. Sätt **120 Ω-terminering** i bussens ändar.

**Använd aldrig GPIO1 eller GPIO3** för RS-485. Det är UART0 (USB-serial/boot-logg) och USB-chippet driver dem – det gav checksummefel och tappade bitar i ramarna. `logger: baud_rate: 0` stänger av loggning på UART0.

**`flow_control_pin` ska ligga under `twc_director:`, inte under `uart:`.** ESPHome:s `uart:` känner inte till nyckeln.

```yaml
uart:
  id: twc_uart
  tx_pin: GPIO22
  rx_pin: GPIO21
  baud_rate: 9600
  stop_bits: 1
  parity: NONE

twc_director:
  id: twc_component
  uart_id: twc_uart
  flow_control_pin: GPIO18   # MAX485 DE+RE
```

**MAX13487E / LilyGO T-CAN485** (auto-direction): utelämna `flow_control_pin` och lägg till de kortspecifika utgångarna som `tesla-director.yaml` *inte* har längre:

```yaml
esphome:
  on_boot:
    priority: 200
    then:
      - output.turn_on: tcan485_boost_en        # GPIO16
      - output.turn_on: tcan485_rs485_shutdown  # GPIO19, HIGH = enable
      - output.turn_on: tcan485_rs485_re        # GPIO17
      - delay: 100ms
  on_shutdown:
    then:
      - output.turn_off: tcan485_boost_en
output:
  - {platform: gpio, id: tcan485_boost_en, pin: GPIO16}
  - {platform: gpio, id: tcan485_rs485_shutdown, pin: GPIO19}
  - {platform: gpio, id: tcan485_rs485_re, pin: GPIO17}
```

Koppla inte GPIO17 till MAX485-RE (motsatt polaritet).

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

`tesla-director.yaml` hämtar komponenten från GitHub (`ref: main`). För utveckling: byt till `type: local` / `path: components` (finns som kommenterat alternativ i filen).

### 2. Från Home Assistant / Device Builder

När forken ligger på GitHub:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/itoaa/twc-director.git
      ref: main
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
| `rs485_debug` | Valfri switch (`diag rs485 logging`): ramloggar på INFO när ON, default OFF |
| `flow_control_pin` | Valfri GPIO till MAX485 DE+RE (HIGH=sänd, LOW=lyssna). Utelämna för auto-direction |
| `global_max_current` | Total maxström för alla TWC (`limit global max current`) |
| `global_twc_max_current` | Max per TWC |
| `master_mode` (`system master mode`) | Switch: director aktiv. **Default PÅ vid boot** (`restore_mode: ALWAYS_ON`; `RESTORE_DEFAULT_ON` = kom ihåg senaste läget). Boxarna startar inte utan masterns heartbeats |
| `evse` | Lista med Wall Connectors / slots (exempel-YAML har två) |

### Namnstandard för entiteter

Alla entitetsnamn är på **engelska** och har formen `<slot-namn> <prefix> <namn>`. Slot-namnet kommer först så att entiteterna grupperas per box. Home Assistant visar enhetsnamnet (`Tesla-Director`) separat och bygger entity_id som `<domän>.tesla_director_<slot>_<prefix>_<namn>` (gemener, understreck). Entiteter som gäller hela Director saknar slot-namn (t.ex. `system master mode`).

| Prefix | Betydelse | Skrivs av | Kategori i HA | Exempel |
|--------|-----------|-----------|---------------|---------|
| `evcc` | det evcc skriver/läser | evcc | normal | `evcc charging enabled`, `evcc current limit` |
| `meter` | uppmätta värden, skrivskyddade | Director (RS-485) | normal | `meter current L1`, `meter voltage L1`, `meter power`, `meter energy total`, `meter energy session` |
| `state` | box-/bilstatus | Director (RS-485) | normal | `state charge` (A/B/C/F), `state charge text`, `state charging`, `state car connected`, `state VIN`, `state online`, `state mode`, `state contactor closed` |
| `limit` | säkerhetstak som alltid gäller | du / YAML vid boot | config | `limit max current`, `limit global max current` |
| `local` | låter Director köra utan evcc | du / YAML vid boot | config | `local start current`, `local current setpoint`, `local increase current`, `local decrease current` |
| `system` | huvudbrytare och buss | du | config (`link ok`/`charging count` är vanliga sensorer) | `system master mode`, `system bus enabled`, `system link ok`, `system charging count` |
| `diag` | felsökning | Director / du | diagnostic | `diag contactor`, `diag rs485 logging`, `diag watchdog ok`, `diag state raw`, `diag state log`, `diag current offered by box`, `diag current setpoint sent`, `diag max current reported by box`, `diag log level` |
| `info` | identitet | Director (RS-485) | diagnostic | `info serial number`, `info firmware` |

Regler: **evcc-entiteter är aldrig `diag`/`config`** (de ska synas som vanliga entiteter). Standardnamnen sätts i komponenten (`_ensure_named_child` i `__init__.py`) och YAML-filerna använder samma namn. Boxarnas slot-namn styrs av substitutionerna `twc_0_name` / `twc_1_name` och är bundna till RS-485-adressen via `twc_N_address`.

> **Byter du namn på en befintlig installation** skapar ESPHome nya entiteter (unique_id följer namnet). Ta bort de gamla (nu `unavailable`) under Inställningar → Enheter → Tesla-Director, annars kan nya entity_id få suffix `_2`.

### Home Assistant → evcc

evcc `template: homeassistant` per Wall Connector. Fullständigt exempel längre ned (”Fungerar direkt efter flash”).

`state charge`: **A** disconnected, **B** connected/waiting, **C** charging, **F** RS-485 nere / TWC offline / error. Klartext ligger i `state charge text`. evcc:s Home Assistant-GUI har bara A/B/C — **F** ger `unknown charge status` där; peka evcc på A/B/C och använd `system link ok` för bussfel.

VIN publiceras som `state VIN`. HA-mallen har inget identify-fält; använd evcc `vehicles.identifiers` eller custom charger `identify`.

Lastbalans: evcc skriver **`evcc current limit`** (`setMaxCurrent`), inte `diag current offered by box` (det är bara avläsning). 0 A = stopp, ≥6 A = tak. Director klampar mot `cap_*` och `limit global max current`.

**evcc-watchdog (valfri, AV som standard):** evcc skriver `evcc current limit` / `evcc charging enabled` bara när värdet *ändras* (inte periodiskt). En tidsgräns skulle därför bryta en pågående laddning med konstant ström, så watchdogen är avstängd (`evcc_watchdog_timeout: 0s`). Sätt t.ex. `evcc_watchdog_timeout: 10min` under `twc_director:` om du ändå vill ha en dödmansgrepp. Löser den ut sätts strömmen till 0 A och `evcc charging enabled` / `evcc current limit` publiceras som AV / 0, så att evcc ser det och skriver om sina värden (`diag watchdog ok` = OFF tills nästa skrivning). Vid omstart av ESP:n startar `evcc charging enabled` AV och `evcc current limit` 0 A (säkert); evcc läser tillbaka `enabled` och skriver om rätt värde.

### Fungerar direkt efter flash (inget i Home Assistant behöver ändras)

Standardvärdena i `tesla-director.yaml` är satta så att evcc kan börja använda Director direkt:

| Inställning | Värde efter flash |
|-------------|-------------------|
| `system master mode` | **ON** (`ALWAYS_ON`) |
| `system bus enabled` (per box) | **ON** (`ALWAYS_ON`) |
| `limit global max current` | `global_max_current` (15 A), sätts i koden |
| `limit max current` per box | `twc_N_max_current` (15 A), sätts vid boot via `on_boot` |
| `local start current` per box | `twc_N_initial_current` (6 A), sätts vid boot via `on_boot` |
| `evcc charging enabled` | **OFF** tills evcc skriver |
| `evcc current limit` | **0 A** tills evcc skriver |
| `diag rs485 logging` | OFF |
| Box → RS-485-adress | fast via `address:` (`twc_0_address` = `0x9983` = ”S22 Garage Inne”, `twc_1_address` = `0x8988` = ”S22 Garage Ute”) – ändra till dina boxars sista fyra serienummersiffror |

**Entiteter evcc ska peka på** (per box; exemplet använder slot-namnet `S22 Garage Inne` → `s22_garage_inne`, enhetsnamn `Tesla-Director`; byter du namn i HA ändras id:t):

| evcc-fält | Entitet | Riktning |
|-----------|---------|----------|
| `status` | `sensor.tesla_director_s22_garage_inne_state_charge` (A/B/C/F) | Director → evcc |
| `enabled` och `enable` | `switch.tesla_director_s22_garage_inne_evcc_charging_enabled` | evcc skriver, läser tillbaka |
| `setMaxCurrent` (→ `maxcurrent`) | `number.tesla_director_s22_garage_inne_evcc_current_limit` | evcc skriver |
| `power` | `sensor.tesla_director_s22_garage_inne_meter_power` | Director → evcc |
| `energy` | `sensor.tesla_director_s22_garage_inne_meter_energy_total` (kWh) | Director → evcc |
| `currentL1–L3` | `sensor.tesla_director_s22_garage_inne_meter_current_l1` … `_l3` | Director → evcc |
| `voltageL1–L3` | `sensor.tesla_director_s22_garage_inne_meter_voltage_l1` … `_l3` | Director → evcc |

Den andra boxen: byt `s22_garage_inne` mot `s22_garage_ute`. Exempel (`evcc.yaml`, byt URI mot din Home Assistant):

```yaml
chargers:
  - name: twc_s22_garage_inne
    type: template
    template: homeassistant
    uri: http://homeassistant.local:8123
    status: sensor.tesla_director_s22_garage_inne_state_charge
    enabled: switch.tesla_director_s22_garage_inne_evcc_charging_enabled
    enable: switch.tesla_director_s22_garage_inne_evcc_charging_enabled
    setMaxCurrent: number.tesla_director_s22_garage_inne_evcc_current_limit
    power: sensor.tesla_director_s22_garage_inne_meter_power
    energy: sensor.tesla_director_s22_garage_inne_meter_energy_total
    currentL1: sensor.tesla_director_s22_garage_inne_meter_current_l1
    currentL2: sensor.tesla_director_s22_garage_inne_meter_current_l2
    currentL3: sensor.tesla_director_s22_garage_inne_meter_current_l3
    voltageL1: sensor.tesla_director_s22_garage_inne_meter_voltage_l1
    voltageL2: sensor.tesla_director_s22_garage_inne_meter_voltage_l2
    voltageL3: sensor.tesla_director_s22_garage_inne_meter_voltage_l3
  - name: twc_s22_garage_ute
    type: template
    template: homeassistant
    uri: http://homeassistant.local:8123
    status: sensor.tesla_director_s22_garage_ute_state_charge
    enabled: switch.tesla_director_s22_garage_ute_evcc_charging_enabled
    enable: switch.tesla_director_s22_garage_ute_evcc_charging_enabled
    setMaxCurrent: number.tesla_director_s22_garage_ute_evcc_current_limit
    power: sensor.tesla_director_s22_garage_ute_meter_power
    energy: sensor.tesla_director_s22_garage_ute_meter_energy_total
    currentL1: sensor.tesla_director_s22_garage_ute_meter_current_l1
    currentL2: sensor.tesla_director_s22_garage_ute_meter_current_l2
    currentL3: sensor.tesla_director_s22_garage_ute_meter_current_l3
    voltageL1: sensor.tesla_director_s22_garage_ute_meter_voltage_l1
    voltageL2: sensor.tesla_director_s22_garage_ute_meter_voltage_l2
    voltageL3: sensor.tesla_director_s22_garage_ute_meter_voltage_l3

loadpoints:
  - title: S22 Garage Inne
    charger: twc_s22_garage_inne
    phases: 3
    mincurrent: 6
    maxcurrent: 15
  - title: S22 Garage Ute
    charger: twc_s22_garage_ute
    phases: 3
    mincurrent: 6
    maxcurrent: 15
```

> **Autentisering:** nyare evcc använder inte längre `token:` i YAML utan godkänner Home Assistant via OAuth i evcc:s webbgränssnitt (Konfiguration → Laddare → Home Assistant). Äldre evcc kräver `token: <long-lived access token>` (skapa i HA-profilen, lägg aldrig i git).

**Vilka entiteter är inställningar och vilka sätter evcc?** Bara `evcc charging enabled` och `evcc current limit` skrivs av evcc. `limit …` och `local …` är Directors egna tak/börvärden och sätts av YAML vid boot. `system …` och `diag …` är Directors egna reglage och diagnostik – de är **inte** värden som andra enheter ska skriva, och evcc ska inte peka på dem:

- `system master mode` – huvudbrytare för busmastern. ON = Director skickar heartbeats. OFF = fail-safe: 0 A, kontaktorer öppnas, sedan tyst.
- `system bus enabled` (per box) – om Director pratar med just den boxen. ON som standard.
- `diag contactor` (per box) – manuell kontaktorstyrning (FC B1/B2), endast i master mode; för felsökning.
- `diag watchdog ok` – diagnostik: ON om evcc skrivit nyligen (alltid ON när watchdogen är avstängd).
- `diag rs485 logging` – slår på/av ramloggning (se ovan).

**Hur strömmen når Wall Connectorn:** när sessionsströmmen ändras skickar mastern ett `0x09`-kommando (”Setting limit”) i heartbeat-ramen. Tidigare skickades det bara en gång. Nu skickar mastern om det applicerade värdet så länge Wall Connectorn laddar och rapporterar en annan tillgänglig ström än den vi vill ha (>0,25 A skillnad, högst var 3:e sekund, därefter var 10:e sekund om boxen fortsätter rapportera något annat), samt som periodisk uppdatering var 10:e sekund. 0 A (evcc av / watchdog) skickas på samma sätt så länge boxen laddar. På slots med evcc-entiteter skriver laddstarten inte längre över evcc:s värde med boxens egen ström.

**Mätvärden:** spänning/ström/effekt avkodas från 3-fas-mätramen (`FD EB`, 16-bitars spänningar och strömmar på byte 10–12). Tidigare visades L2-strömmen felaktigt som ~114 A. Se [PROTOCOL.md](components/twc_director/PROTOCOL.md) och [docs/TWC-RS485-Sverige.md](docs/TWC-RS485-Sverige.md).

## Utveckling

```
twc-director/
├── components/twc_director/     # ESPHome external component
│   ├── __init__.py
│   ├── twc_director_component.*
│   ├── twc_*.c / twc_*.h        # C-protokollbibliotek (SLIP + TWC)
│   └── PROTOCOL.md
├── tests/                       # Enhetstester för C-kärnan (make -C tests test)
├── .github/workflows/ci.yml     # Enhetstester + kompilerar mot ESPHome 2026.7.3 + latest
├── tesla-director.yaml          # Referens: D1 mini ESP32 + MAX485 (GPIO22/21/18), två slots, evcc
├── tesla-director-safe.yaml     # Produktion: samma hårdvara, ingen web_server
├── docs/SECURITY.md
├── docs/TWC-RS485-Sverige.md    # Hur svenska Gen2-boxar pratar RS-485 (alla ramar)
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

### Logga RS-485-ramar (runtime)

Per-ram-loggarna (`RX: …`, `TX: …`, `Sending E0 to …`) är **av som standard**. Lägg till `rs485_debug:` under `twc_director:` så får du en switch i HA, **`diag rs485 logging`** (default OFF, kommer ihåg läget efter omstart). ON = ramloggarna skrivs på **INFO**, OFF = de skrivs inte. `RX stats`/`TX stats` ligger kvar på DEBUG. Övrig loggnivå styrs med select-entiteten **diag log level**:

```yaml
logger:
  level: VERBOSE      # tak vid kompilering
  initial_level: INFO
  baud_rate: 0

select:
  - platform: logger
    name: "diag log level"

twc_director:
  rs485_debug:
    name: "diag rs485 logging"
```

### Tester

```bash
make -C tests test
```

Testerna kör C-kärnan på värddatorn: avkodning av riktiga `FD EB`-ramar och att `0x09` (sessionsström) skickas om / 0 A skickas / global max respekteras.

## Safety profile (P0/P1)

Software hardening in this fork (see [docs/SECURITY.md](docs/SECURITY.md)):

- **`global_max_current` is required** and must be `> 0`
- Runtime global-max number **updates the C core** (session reconciliation)
- Contactor commands require **master mode** + EVSE enabled; UI follows bus current
- **Master mode ON at boot** (`restore_mode: ALWAYS_ON`; Wall Connectors need heartbeats to start). evcc current limit starts at 0 A / charge enable OFF
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
3. **Strömgränser** – master mode ON (default), globala gränser
4. **Boxarna startar inte** – kontrollera att `system master mode` är ON; utan master skickas inga heartbeats
5. **Kompileringsfel efter ESPHome-uppgradering** – clean build, se sektion om 2026.7 ovan

## Licens

MIT. Upstream copyright: **Wired Square** (2025).  
Egna ändringar i denna fork: se git-historik.

## Tack

- [Wired-Square/esphome-twc-director](https://github.com/Wired-Square/esphome-twc-director)
- Tesla Motors Club / protokoll-reverse-engineering-communityn
- ESPHome-teamet
