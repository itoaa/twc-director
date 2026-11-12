# Tesla Wall Connector Gen2 – RS-485 i Sverige

Hur två svenska 3-fas Gen2 Wall Connectors (HPWC, firmware 4.5.10.2) pratar med en master (TWC Director) över RS-485, med alla ramar som koden använder. Allt här är avlyssnat/kontrollerat mot koden i `components/twc_director/` och riktiga loggar från en D1 mini ESP32 + MAX485 mot två boxar. Det som **inte** är verifierat är markerat **(ej verifierat)**.

Se även [PROTOCOL.md](../components/twc_director/PROTOCOL.md) (generell protokollbeskrivning).

## 1. Förutsättningar i Sverige

| | |
|---|---|
| Nät | 3-fas, 400 V mellan faser, **230 V fas–nolla** (mätramen visar 228–230 V per fas) |
| Strömenhet i mätramen | 0,5 A per steg, en byte per fas |
| Effekt | `P = V1·I1 + V2·I2 + V3·I3` (t.ex. 3 × 228 V × ~11,2 A ≈ 7,6 kW) |
| Roller | En **master** (TWC Director, adress `0xF00D`) och upp till tre **peripherals** (Wall Connectors). Boxarna sätts som peripheral med vridomkopplaren (position **F**, enligt Teslas installationsmanual – **ej verifierat här**) |
| Boxarnas adresser | `0x8988` (serie A19E0108988) och `0x9983` (serie A20J0149983). Adressen är de fyra sista siffrorna i serienumret |
| Master-adress | `0xF00D` (`addr:` i YAML) |

## 2. Fysiskt lager

- RS-485, 2 ledare (A/B), **9600 baud, 8N1**, halv duplex.
- Referensuppsättning: D1 mini ESP32 + MAX485 på **3,3 V**. TX = **GPIO22** → DI, RX = **GPIO21** ← RO, **GPIO18** → DE+RE (hopknutna) som `twc_director: flow_control_pin` (HIGH = sänd, LOW = lyssna). Se [README](../README.md#hårdvara).
- **Gemensam GND** mellan MAX485 och Wall Connectorerna, och **120 Ω-terminering** i bussens ändar.
- **Använd inte GPIO1/GPIO3** (UART0/USB-chip). Det gav checksummefel och tappade bitar.
- Mastern väntar tills bussen varit tyst ≥ 10 ms (`BUS_IDLE_GUARD_MS`) och lämnar ≥ 5 ms mellan egna ramar (`TX_INTERFRAME_GAP_MS`). Före sändning sätts DE/RE hög, efter `flush()` låg.

## 3. Ramformat (SLIP)

På tråden:

```text
C0  <ram, escapad>  C0  FC
│                   │   └─ "end type" (FC; även FE/F8/FA har setts). Ingår inte i checksumman
│                   └───── SLIP END
└───────────────────────── SLIP END
```

Escapes inuti ramen:

| Byte i ram | Skickas som |
|-----------|-------------|
| `C0` | `DB DC` |
| `DB` | `DB DD` |

Ett `DB` följt av något annat än `DC`/`DD` är ogiltigt och ramen kastas (`dropped_invalid_esc`). Ramar längre än 256 byte kastas (`dropped_overflow`).

Ram efter avescapning:

```text
TT  CC  SS SS  <payload>  KK
│   │   │  │      │       └─ checksum
│   │   └──┴─ avsändaradress (hög, låg)
│   └─ kommando
└─ marker
```

- **Marker:** `FB` förfrågan (master → peripheral, svar förväntas), `FC` kommando/broadcast (inget svar förväntas), `FD` svar/data (peripheral → master).
- **Checksum** = summan av alla bytes **efter** markerbyten (kommando, adress, payload) modulo 256. Verifierat i `twc_decode_frame` och mot loggarna nedan.
- Master → peripheral har **11 bytes payload** (ram = 16 byte): de två första bytena är mottagarens adress. Svar med data (`EB`, `ED`, `EE`, `EF`, `F1`, `EC` m.fl.) har **15 bytes payload** (ram = 20 byte; `EC` och `E0`-svar är 16 byte).
- Alla flerbytestal är big-endian.
- Strömmar i E0/E2 anges i **centiampere** (0,01 A): `05 DC` = 1500 = 15,00 A.
- Tvåbytesmeddelanden (t.ex. `7C 00`) är broadcast och inte TWC-ramar. `7C 00` tolkas som "för många förfrågningar" (misstänkt, **ej verifierat**).

## 4. Alla kommandokoder (`twc_protocol.h`)

| Kod | Namn i koden | Marker / riktning | Betydelse | Används av Director |
|-----|--------------|-------------------|-----------|---------------------|
| `0x19` | `WRITE_ID_DATE` | `FC` | **Farlig: skriver ID/datum till TWC:ns flash.** Blockeras (`twc_cmd_is_dangerous`) | Nej, aldrig |
| `0x1A` | `WRITE_MODEL_NUMBER` | `FC` | **Farlig: skriver modellnummer till flash.** Blockeras | Nej, aldrig |
| `0x1D` | `IDLE` | – | Tomgångs-/keepalive (ingen funktion) – **ej verifierat** | Nej |
| `0xB1` | `CLOSE_CONTACTORS` | `FC` master → peripheral | Stäng kontaktorer (tillåt laddning) | Ja, via contactor-switchen i master mode |
| `0xB2` | `OPEN_CONTACTORS` | `FC` master → peripheral | Öppna kontaktorer (stoppa laddning) | Ja, vid master mode av och via contactor-switchen |
| `0xB4` | `PLUG_STATE` | – | Fråga plugg-/bilstatus – **ej verifierat** | Nej |
| `0xE0` | `HEARTBEAT` | `FB` master → peripheral, `FD` svar | Heartbeat och strömkommandon (kap. 6) | Ja, varje sekund |
| `0xE1` | `CONTROLLER_NEGOTIATION` | `FC` master → alla | Master annonserar sig (kap. 5) | Ja, vid start |
| `0xE2` | `PERIPHERAL_NEGOTIATION` | `FB` master → alla (paus), `FD` peripheral → master (linkready) | Peripheral annonserar sig och sin maxström; master pausar | Ja |
| `0xEB` | `METER` | `FB` / `FD` | Energi, spänning, ström (kap. 8) | Ja |
| `0xEC` | `VERSION` | `FB` / `FD` | Firmware-version | Ja |
| `0xED` | `SERIAL` | `FB` / `FD` | Serienummer (ASCII) | Ja |
| `0xEE` | `VIN_HI` | `FB` / `FD` | VIN, första 7 tecknen | Ja |
| `0xEF` | `VIN_MID` | `FB` / `FD` | VIN, mellersta 7 tecknen | Ja |
| `0xF1` | `VIN_LO` | `FB` / `FD` | VIN, sista tecknen | Ja |

Markers: `0xFB` `TWC_MARKER_REQUEST`, `0xFC` `TWC_MARKER_ANNOUNCE`, `0xFD` `TWC_MARKER_RESPONSE`.

## 5. Master/peripheral-förhandling (E1/E2)

Adresser: master `F00D`, peripherals `8988` och `9983`.

**Peripheral utan master** skickar `FD E2` ("linkready"): sin adress, ett engångsbyte (i koden kallat *session*) och sin maxström. Verkliga ramar:

```text
FD E2 89 88 | 79 | 0C 80 | 00 00 00 00 00 00 00 00 | F8
FD E2 99 83 | 43 | 0C 80 | 00 00 00 00 00 00 00 00 | CD   (checksum beräknad, ej avlyssnad)
            │    └─ 0x0C80 = 3200 = 32,00 A max (boxens hårdvarutak)
            └─ 0x79 / 0x43: olika per box. Teslas dokumentation kallar byten "sign"; koden behandlar den som session-id.
```

Payload `FD E2`: `[0]` session/sign, `[1..2]` tillgänglig ström (centiampere), resten 0.

**Master vid start** (`handle_startup_burst`, 200 ms mellan ramarna):

1. **5 × `FC E1`**, payload `[0]` = masterns slumpade session-id (1–254), resten 0. Exempel (session `0x11`, **konstruerad, ej avlyssnad**):
   `FC E1 F0 0D 11 00 00 00 00 00 00 00 00 00 00 EF`
2. **4 × `FB E2`** ("peripheral pause"), samma payload-form. Exempel:
   `FB E2 F0 0D 11 00 00 00 00 00 00 00 00 00 00 F0`  (**konstruerad**; checksumma `0xF0`)

**Master mode är PÅ vid boot** (`master_mode: restore_mode: ALWAYS_ON`): boxarna startar inte utan masterns heartbeats. evcc-börvärdet startar ändå på 0 A / `evcc charging enabled` AV, så ingen ström erbjuds förrän evcc skriver. Stängs `system master mode` av öppnas kontaktorerna och sändningen upphör.

Efter bursten tar heartbeats (E0) över. En peripheral som fortfarande annonserar sig (`UNCONF_PERIPHERAL`) "claimas" av mastern med en `FB E0` med state `00` och 0 A, varefter den blir en konfigurerad peripheral och binds till en slot i YAML.

Lägen som koden håller reda på per adress: `UNKNOWN`, `PERIPHERAL`, `UNCONF_PERIPHERAL`, `FOREIGN_MASTER` (annan master hörs på bussen), `FOREIGN_PERIPHERAL`.

## 6. Heartbeat E0

### Master → peripheral (`FB E0`)

```text
FB E0 F0 0D | 89 88 | ST | AA AA | DD DD | 00 00 00 00 | KK
 marker/cmd   mottagare state avail   deliv   padding
```

- `AA AA` = ström master tillåter (centiampere), `DD DD` = levererad ström (Director skickar alltid 0).
- Mastern skickar **en E0 per sekund** (`TWC_HEARTBEAT_INTERVAL_MS = 1000`), runt bland boxarna. Med två boxar blir det en E0 var 2:a sekund per box (så ser det ut i loggen: 9983, 8988, 9983 …).

| State | Namn | Betydelse | Payload |
|-------|------|-----------|---------|
| `00` | normal heartbeat | Ingen ny order (`avail=0 deliv=0`) | |
| `05` | sätt **initial**-ström | Ström som boxen får starta med (`local start current`) | `AA AA` = ström |
| `06` | öka ström | Be boxen öka (PROTOCOL.md: ca +2 A) | 0 |
| `07` | minska ström | Be boxen minska (ca −2 A) | 0 |
| `09` | sätt **session**-ström | Aktuell gräns under pågående laddning (evcc `offered current`) | `AA AA` = ström |

Exempel (konstruerade, checksumma beräknad):

```text
FB E0 F0 0D 89 88 00 00 00 00 00 00 00 00 00 EE   normal heartbeat till 0x8988        (avlyssnad)
FB E0 F0 0D 89 88 05 03 E8 00 00 00 00 00 00 DE   initial 10,00 A
FB E0 F0 0D 89 88 09 04 B0 00 00 00 00 00 00 AB   session 12,00 A
FB E0 F0 0D 89 88 09 00 00 00 00 00 00 00 00 F7   session 0 A (stopp)
```

### Peripheral → master (`FD E0`)

```text
FD E0 89 88 | F0 0D | ST | AA AA | DD DD | 00 00 00 00 | KK
 avsändare    master  state  boxens   levererad
                             gräns
```

Verkliga ramar från boxarna:

```text
FD E0 99 83 F0 0D 00 00 00 00 00 00 00 00 00 F9   0x9983: Ready, 0 A (ingen bil)
FD E0 89 88 F0 0D 03 00 00 00 00 00 00 00 00 F1   0x8988: Waiting (bil ansluten, laddar inte)
FD E0 89 88 F0 0D 08 05 DC 02 2C 00 00 00 00 05   0x8988: Charge started, avail 15,00 A, levererar 5,56 A
FD E0 89 88 F0 0D 01 05 DC 04 92 00 00 00 00 66   0x8988: Charging, avail 15,00 A, levererar 11,70 A
FD E0 89 88 F0 0D 01 05 DC 04 FE 00 00 00 00 D2   0x8988: Charging, levererar 12,78 A
```

| State (svar) | Namn | Observerat? |
|--------------|------|-------------|
| `00` | Ready | Ja |
| `01` | Charging | Ja |
| `02` | Error | Nej |
| `03` | Waiting | Ja |
| `04` | Negotiating | Nej |
| `05` | Max charge | Nej |
| `06` | Ack ökning | Nej |
| `07` | Ack minskning | Nej |
| `08` | Charge started | Ja |
| `09` | Setting limit | Ja (syns som status-logg i HA) |
| `0A` | Adjustment complete | Nej |

`levererad` stämmer med mätramen: 11,70 A i E0 mot 11,0/11,0/11,5 A per fas i EB.

**Om tillgänglig ström i svaret:** boxen rapporterar 15,00 A (`05 DC`) även när Directorn vill ha 12 A. **Ej verifierat** om boxen speglar senast skickade `09`-värde eller rapporterar sitt eget tak. Därför skickar Directorn om `09` (se kap. 9) och litar inte på ekot.

## 7. Informationsförfrågningar

Mastern frågar varje peripheral i tur och ordning, högst en förfrågan var 2:a sekund per box (`TWC_INFO_PROBE_INTERVAL_MS = 2000`), tidigast 500 ms efter en heartbeat och först efter minst två heartbeats sedan förra förfrågan. Ordning: **EE → EF → F1 → ED → EB → EC → (om från EE)**.

Förfrågan (alla likadana, 16 byte, mottagaradress + nollor):

```text
FB EB F0 0D 89 88 00 00 00 00 00 00 00 00 00 F9   meter
FB EC F0 0D 89 88 00 00 00 00 00 00 00 00 00 FA   version
FB ED F0 0D 89 88 00 00 00 00 00 00 00 00 00 FB   serienummer   (konstruerad)
FB EE F0 0D 89 88 00 00 00 00 00 00 00 00 00 FC   VIN del 1
FB EF F0 0D 89 88 00 00 00 00 00 00 00 00 00 FD   VIN del 2
FB F1 F0 0D 89 88 00 00 00 00 00 00 00 00 00 FF   VIN del 3
```

### EC – version

```text
FD EC 89 88 | 04 05 0A 02 | 00 00 00 00 00 00 00 | 12   -> 4.5.10.2
```

Payload: `[0]` major, `[1]` minor, `[2]` patch, `[3]` build (visas som `4.5.10.2`). EC-svaret är 16 byte.

### EE / EF / F1 – VIN

```text
FD EE 89 88 | 35 59 4A 53 41 37 45 | 00 00 00 00 00 00 00 00 | E7   "5YJSA7E"
FD EF 89 88 | 36 33 50 46 35 30 38 | 00 00 00 00 00 00 00 00 | 9C   "63PF508"
FD F1 89 88 | 30 35 31             | 00 00 00 00 00 00 00 00 00 00 00 00 | 98   "051"
```

VIN = EE + EF + F1 = **5YJSA7E63PF508051**. Payload är ASCII, avslutad med `00`. Alla tre delarna måste finnas för att "vehicle connected" ska bli sant; en all-noll-del (ingen text) betyder att bilen kopplats ur och VIN rensas.

### ED – serienummer

Format: ASCII i payload, nollterminerad, 15 byte. Serienumren är `A19E0108988` (0x8988) och `A20J0149983` (0x9983). Dessa ED-ramar finns **inte** i de loggar som använts här; ramarna nedan är **konstruerade från koden, ej avlyssnade**:

```text
FD ED 89 88 41 31 39 45 30 31 30 38 39 38 38 00 00 00 00 60   "A19E0108988"
FD ED 99 83 41 32 30 4A 30 31 34 39 39 38 33 00 00 00 00 68   "A20J0149983"
```

## 8. EB – mätvärden (3-fas, verifierat)

Payload, 15 byte:

| Offset | Fält | Typ | Skala |
|--------|------|-----|-------|
| 0..3 | total energi | uint32 | kWh |
| 4..5 | spänning L1 | uint16 | V |
| 6..7 | spänning L2 | uint16 | V |
| 8..9 | spänning L3 | uint16 | V |
| 10 | ström L1 | uint8 | 0,5 A |
| 11 | ström L2 | uint8 | 0,5 A |
| 12 | ström L3 | uint8 | 0,5 A |
| 13..14 | padding | | 00 00 |

```text
FD EB 89 88 | 00 00 87 1A | 00 E4 | 00 E4 | 00 E4 | 16 16 17 | 00 00 | 8C   laddar
            | 34586 kWh   | 228 V | 228 V | 228 V | 11,0 / 11,0 / 11,5 A      (≈ 7,6 kW)
FD EB 99 83 | 00 00 1D 8F | 00 E6 | 00 E6 | 00 E5 | 00 00 00 | 00 00 | 64   vilar
            | 7567 kWh    | 230 V | 230 V | 229 V | 0 A
```

Tidigare versioner av koden läste en byte per spänning på offset 4/5/6 och strömmar på 9/10/11. Då tolkades låg-byten av L2-spänningen (`E4` = 228) som ström och delades med 2, vilket gav **114 A** på L2. Rättat. Enfas-boxar (t.ex. USA) antas använda samma layout med bara L1 ifylld – **ej verifierat**.

## 9. Hur evcc-börvärdet når boxen

1. evcc skriver `evcc current limit` (och `evcc charging enabled`) i Home Assistant → ESPHome-entiteterna.
2. Directorn klampar värdet mot `limit max current`, `limit global max current` och 6 A-minimum (0 A = stopp), skalar vid behov ner så att summan över boxarna ryms i `global_max_current`, och får fram ett **applicerat** värde.
3. Mastern skickar `FB E0 … 09 <ström>`. Så länge boxen laddar skickas `09` om när boxens rapporterade `avail` skiljer sig > 0,25 A från det applicerade värdet (högst var 3:e sekund, efter tre försök var 10:e sekund) samt som uppdatering var 10:e sekund.
4. **evcc-watchdog (valfri, AV som standard):** evcc skriver bara vid ändring, så en tidsgräns skulle avbryta en stabil laddning. Med `evcc_watchdog_timeout` satt (t.ex. `10min`) sätts börvärdet till **0 A** om evcc inte skrivit inom tiden (`diag watchdog ok` = OFF) och `evcc charging enabled`/`evcc current limit` publiceras som AV/0 så att evcc skriver om dem.
5. **Stopp:** 0 A skickas som `09` med `00 00` och upprepas så länge boxen laddar. Kontaktorerna öppnas med `FC B2` bara när master mode stängs av eller contactor-switchen används. Hur boxen/bilen reagerar på `09 0 A` (avbryter direkt eller sänker till minimum) är **ej verifierat** i loggarna.
6. Laddstart: initial-strömmen (`05`, `local start current`) sätts oberoende av evcc. Standard i `tesla-director.yaml` är **0 A**. Directorn skickar `05` med `00 00` när det applicerade värdet blir 0 A eller när en bil ansluts. På slots med evcc-entiteter skriver Directorn inte längre över evcc:s värde med boxens egen ström vid laddstart. Hur TWC:n reagerar på `05` med 0 A — om den väntar, eller faller tillbaka på sitt eget värde — är **overifierat**; Ola testar det på bussen.

## 10. Tider och gränser (från koden)

| Värde | Konstant | Standard |
|-------|----------|----------|
| Heartbeat (alla boxar tillsammans) | `TWC_HEARTBEAT_INTERVAL_MS` | 1000 ms |
| Info-förfrågan per box | `TWC_INFO_PROBE_INTERVAL_MS` | 2000 ms |
| Paus efter heartbeat före förfrågan | `TWC_POST_HEARTBEAT_DELAY_MS` | 500 ms |
| Startburst: E1 × / E2 × | `TWC_STARTUP_BURST_*` | 5 × / 4 × med 200 ms |
| Box offline utan ram | `TWC_DEFAULT_ONLINE_TIMEOUT_MS` | 15 s |
| Länk nere utan giltig ram | `LINK_TIMEOUT_MS` | 15 s |
| Omsändning av `09` vid avvikelse | `TWC_SESSION_RESEND_INTERVAL_MS` | 3 s (3 försök) |
| Uppdatering av `09` | `TWC_SESSION_REFRESH_INTERVAL_MS` | 10 s |
| evcc-watchdog | `evcc_watchdog_timeout` | 0 s (av) |
| Max ström per enhet (säkerhetsklamp) | `TWC_MAX_DEVICE_CURRENT_A` | 32 A |

## 11. Felsökning

- **Checksummefel / ramar med saknade bitar:** kontrollera att RS-485 inte ligger på GPIO1/GPIO3, att GND är gemensam och att bussen är termineras (120 Ω).
- **`system link ok` av:** inga giltiga ramar på 15 s. Kontrollera A/B (byt vid behov), 3,3 V på MAX485 och `flow_control_pin`.
- **Boxarna startar inte alls:** kontrollera att `system master mode` är ON (default vid boot). Utan master skickas inga heartbeats.
- **Ingen laddning trots master mode:** `diag watchdog ok` ska vara ON, `evcc charging enabled` ON och `evcc current limit` ≥ 6 A på **rätt slot** (den med VIN/bil).

### Logga bussen vid felsökning

Per-ram-loggarna (`RX: …`, `TX: …`, `Sending E0 to …`) är avstängda som standard. Slå på switchen **`diag rs485 logging`** (`rs485_debug:` under `twc_director:`) i Home Assistant, så skrivs de på **INFO** i loggen. Av = de skrivs inte alls. Statusraderna (`RX stats`, `TX stats`) ligger kvar på DEBUG. Switchen kommer ihåg sitt läge efter omstart (standard: av). Övrig loggnivå ändras med select-entiteten **diag log level** (`logger: level: VERBOSE`, `initial_level: INFO`, `baud_rate: 0`).

## 12. Ej verifierat

- Vridomkopplarens läge F som peripheral-läge (Teslas manual, inte kontrollerat här).
- Betydelsen av `0x1D` (idle), `0xB4` (plug state) och `0x06`/`0x07` (±2 A) på 4.5.10.2.
- Om boxens `avail` i `FD E0` speglar senast skickade `09`, eller är boxens eget tak.
- Hur boxen beter sig på `09 0 A` (stopp) utan `B2`.
- ED-ramarna ovan (konstruerade) och E1/E2-burstramarna (konstruerade från koden).
- Enfasvarianten av mätramen.
