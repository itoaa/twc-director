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
| GPIO16 | EN (boost) | Valfri 5V boost |
| GPIO19 | ~SHDN | HIGH = enable |
| GPIO17 | RE | HIGH = receive enable |
| 3.3V / GND | VCC / GND | Ström |

A/B på transceivern kopplas till TWC:ns RS-485-terminaler. UART: **9600 8N1**.

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
      # ref: main   # valfritt: pinna branch/tag
```

**OCPP (branch `feature/ocpp-1.6`):**

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/itoaa/twc-director.git
      ref: feature/ocpp-1.6
    components: [twc_director, ocpp_client]
    refresh: 0s   # tip: tvinga omklon efter branch-push
```

Ingen manuell `fetch_deps.sh` behövs i HA — saknade vendor-submoduler hämtas automatiskt vid `ocpp_client.enabled: true`.

Kopiera och anpassa `tesla-director.yaml` (eller `tesla-director-ocpp.yaml` / `examples/ocpp-fragment.yaml` för OCPP).

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
│   ├── twc_*.c / twc_*.h        # C-protokollbibliotek (SLIP + TWC)
│   └── PROTOCOL.md
├── .github/workflows/ci.yml     # Kompilerar mot ESPHome 2026.7.3 + latest
├── tesla-director.yaml          # Exempel-firmwareconfig
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

## OCPP 1.6J experiment

On branch `feature/ocpp-1.6` we explore **native** OCPP 1.6J (MicroOCPP) so the
director can speak to a CSMS over **wss**, report MeterValues/Status from TWC
telemetry, and steer **global + per-connector** amp wishes (hard caps win).
Runtime HA entities can set CSMS URL/id/key and enable/disable without reflash.
Lab RemoteStart/Stop/Reset/Unlock are **DEFAULT OFF**; UpdateFirmware always rejected.

### Out of scope (enforced)

- OCPP 2.0.1 (not enabled)
- Remote firmware path
- Cleartext `ws://` / raising amps above hard caps / committing secrets

- Design + CISO: [`docs/OCPP.md`](docs/OCPP.md), [`docs/SECURITY.md`](docs/SECURITY.md)
- Component: [`components/ocpp_client/`](components/ocpp_client/) (default `enabled: false`)
- Example fragment: [`examples/ocpp-fragment.yaml`](examples/ocpp-fragment.yaml)
- Optional OCPP YAML (CI-verified `enabled: true`): [`tesla-director-ocpp.yaml`](tesla-director-ocpp.yaml)

`main` / default [`tesla-director.yaml`](tesla-director.yaml) stays without OCPP.
Vendor libs are git submodules (auto-fetched in HA if missing). Local: `git submodule update --init --recursive` or `./components/ocpp_client/scripts/fetch_deps.sh` + `ocpp_*` secrets.

