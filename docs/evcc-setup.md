# Sätta upp evcc mot TWC Director

Den här guiden kopplar evcc till TWC Director via Home Assistant-mallen
(`template: homeassistant`). Den gäller den aktuella namnstandarden i
[`tesla-director.yaml`](../tesla-director.yaml) (entitetsnamn på formen
`<box> <prefix> <name>`). Ett komplett YAML-exempel ligger i
[`examples/evcc.yaml`](../examples/evcc.yaml).

Officiell evcc-dokumentation: [Home Assistant i evcc](https://docs.evcc.io/en/smarthome/home-assistant),
[konfiguration](https://docs.evcc.io/en/installation/configuration) och
[evcc som Home Assistant-app](https://docs.evcc.io/en/installation/home-assistant).

## 1. Förutsättningar

1. **Director är flashad** med aktuell `tesla-director.yaml` (eller en fil
   som använder samma entitetsnamn och inte själv skriver över dem med
   `name:`). Enhetens `friendly_name` är **Tesla-Director**, ESPHome-namnet
   är `tesla-director`.
2. **ESPHome-enheten är tillagd i Home Assistant** och entiteterna är
   tillgängliga.
3. Boxarna i referens-YAML:n är bundna till RS-485-adressen:

   | Slot | Namn | Adress | Serie (exempel) |
   |------|------|--------|-----------------|
   | `twc_0` | S22 Garage Inne | `0x9983` | A20J0149983 |
   | `twc_1` | S22 Garage Ute | `0x8988` | A19E0108988 |

   Har du andra boxar: ändra `twc_N_address` / `twc_N_name` i YAML:n och
   byt entity-id:n i evcc så att de följer dina namn.

**Hur entity_id bildas.** Home Assistant visar enhetsnamnet
(`Tesla-Director`) separat. Entity-id blir

```text
<domän>.tesla_director_<box-slug>_<prefix>_<namn>
```

med gemener och understreck. `S22 Garage Inne` blir till exempel
`s22_garage_inne`. Exempel:

```text
sensor.tesla_director_s22_garage_ute_state_charge
switch.tesla_director_s22_garage_ute_evcc_charging_enabled
number.tesla_director_s22_garage_ute_evcc_current_limit
```

Entiteter som gäller hela Director (utan slot-namn), till exempel
`system master mode`, får id utan box-slug:
`switch.tesla_director_system_master_mode`.

**Byter du namn på enheten eller en entitet i Home Assistant ändras
entity_id.** Då måste evcc peka på de nya id:na.

**Ta bort gamla, otillgängliga entiteter** under Inställningar → Enheter
och tjänster → Tesla-Director efter en namnändring. ESPHome:s unique_id
följer namnet, så nya entiteter skapas vid sidan av de gamla. Lämnar du
kvar de otillgängliga kan de nya id:na få suffix `_2`, och evcc hittar
dem inte.

**Standard efter boot** (inget behöver ändras i Home Assistant):

- `system master mode` är **ON** (`restore_mode: ALWAYS_ON`). Boxarna
  startar inte utan masterns heartbeats.
- `evcc charging enabled` är **OFF** och `evcc current limit` är **0 A**
  tills evcc skriver.
- evcc-watchdogen är **av** (`evcc_watchdog_timeout: 0s`). evcc skriver
  bara när värdet ändras, inte periodiskt, så en tidsgräns skulle kunna
  avbryta en pågående laddning med konstant ström.

## 2. Entitetsmappning

Båda fälten **Enabled status sensor** (`enabled`) och **Enable switch**
(`enable`) ska peka på **samma** switch: `evcc charging enabled`. evcc
läser den för att se om laddningen är på, och skriver till den för att
slå på eller av.

Status-sensorn (`state charge`) ger bokstäverna **A** (frånkopplad),
**B** (bil ansluten / väntar) och **C** (laddar). Director kan också
rapportera **F** (RS-485 nere, TWC offline eller fel). evcc:s
Home Assistant-mall känner som standard bara igen A/B/C; ett **F** kan
därför visas som okänd status. Använd `system link ok` för bussfel, peka
inte evcc:s statusfält på F-hantering.

I evcc:s mall heter fältet för maxström `setMaxCurrent` (i
webbgränssnittet *Maximum current entity \[A\]*). Internt renderas det
som `maxcurrent`.

Effekt, energi och fasvärden är i evcc:s mall märkta som avancerade fält
och kan därför ligga under avancerade alternativ i gränssnittet. De är
ändå rekommenderade, så att evcc får riktiga mätvärden från boxen.

Fältetiketterna nedan är de officiella engelska och tyska namnen från
evcc:s Home Assistant-mall
([mallkällan](https://github.com/evcc-io/evcc/blob/master/templates/definition/charger/homeassistant.yaml)
och [docs.evcc.io](https://docs.evcc.io/en/smarthome/home-assistant)).
Svenska etiketter, om evcc visar sådana, kan skilja sig — använd
YAML-namnet om du är osäker.

| evcc-fält (YAML) | Etikett i evcc (en / de) | Entitet (exempel: S22 Garage Ute) | Varför |
|------------------|--------------------------|-----------------------------------|--------|
| `status` | Charging status sensor / Ladestatus-Sensor | `sensor.tesla_director_s22_garage_ute_state_charge` | evcc behöver A/B/C för att veta om bilen är urkopplad, ansluten eller laddar. |
| `enabled` | Enabled status sensor / Aktivierungsstatus-Sensor | `switch.tesla_director_s22_garage_ute_evcc_charging_enabled` | evcc läser om laddningen är tillåten (`on`/`off`). Samma switch som `enable`. |
| `enable` | Enable switch / Aktivierungsschalter | `switch.tesla_director_s22_garage_ute_evcc_charging_enabled` | evcc slår på och av laddningen. Samma switch som `enabled`. |
| `setMaxCurrent` | Maximum current entity \[A\] / Maximale Stromstärke-Entität \[A\] | `number.tesla_director_s22_garage_ute_evcc_current_limit` | evcc sätter lastbalanserat strömtak i ampere. 0 A = stopp, ≥ 6 A = tak. |
| `power` | Power entity / Leistungsentität | `sensor.tesla_director_s22_garage_ute_meter_power` | Ögonblicklig effekt (W) från boxens mätare. |
| `energy` | Energy entity / Energieentität | `sensor.tesla_director_s22_garage_ute_meter_energy_total` | Ackumulerad energi (kWh), totalräknaren — inte sessionsenergin. |
| `currentL1` | L1 current entity / L1 Stromentität | `sensor.tesla_director_s22_garage_ute_meter_current_l1` | Fasström L1 (A). |
| `currentL2` | L2 current entity / L2 Stromentität | `sensor.tesla_director_s22_garage_ute_meter_current_l2` | Fasström L2 (A). |
| `currentL3` | L3 current entity / L3 Stromentität | `sensor.tesla_director_s22_garage_ute_meter_current_l3` | Fasström L3 (A). |
| `voltageL1` | L1 voltage entity / L1 Spannungsentität | `sensor.tesla_director_s22_garage_ute_meter_voltage_l1` | Fasspänning L1 (V). |
| `voltageL2` | L2 voltage entity / L2 Spannungsentität | `sensor.tesla_director_s22_garage_ute_meter_voltage_l2` | Fasspänning L2 (V). |
| `voltageL3` | L3 voltage entity / L3 Spannungsentität | `sensor.tesla_director_s22_garage_ute_meter_voltage_l3` | Fasspänning L3 (V). |

För **S22 Garage Inne** byter du `s22_garage_ute` mot `s22_garage_inne` i
alla id:n.

Valfritt avancerat fält i nyare evcc: *Vehicle identification sensor*
(`identify`) kan peka på `sensor.tesla_director_s22_garage_ute_state_vin`.
Det behövs inte för att starta och styra laddningen.

### Entiteter evcc inte ska använda

Bara `evcc charging enabled` och `evcc current limit` är avsedda för evcc
att skriva till. Övriga prefix är Directors egna och ska inte kopplas in
som evcc:s styr- eller statusfält:

- **`diag`** — diagnostik under kategorin Diagnostik i HA. Bland annat
  manuell kontaktor (`diag contactor`), ramloggning
  (`diag rs485 logging`), watchdog-status (`diag watchdog ok`),
  råstatus (`diag state raw` / `diag state log`), strömmen boxen
  rapporterar som tillgänglig (`diag current offered by box`),
  strömmen Director senast skickade (`diag current setpoint sent`) och
  boxens rapporterade maxtak (`diag max current reported by box`).
  Använd dem för felsökning, inte som evcc:s `setMaxCurrent` eller
  `status`.
- **`info`** — identitet från boxen (`info serial number`,
  `info firmware`). Ren information, inte styrvärden.
- **`system`** — Directors egna brytare och bussstatus:
  `system master mode` (huvudbrytare för mastern),
  `system bus enabled` (om Director pratar med just den boxen),
  `system link ok` (bussen lever) och `system charging count`.
  evcc ska inte slå av mastern eller bussen.
- **`limit`** — säkerhetstak som alltid gäller
  (`limit max current` per box, `limit global max current` för slingan).
  De sätts i YAML vid boot. evcc skriver sitt tak till
  `evcc current limit`; Director klampar sedan mot de här taken.
- **`local`** — Directors egna börvärden när den körs utan evcc
  (`local start current`, `local current setpoint` med mera). evcc ska
  inte skriva dit.

## 3. Alternativ A: via webbgränssnittet

evcc rekommenderar konfiguration i webbgränssnittet. Inställningar som
du gör där sparas i evcc:s databas, inte i `evcc.yaml`. Stegen nedan
följer [evcc:s Home Assistant-guide](https://docs.evcc.io/en/smarthome/home-assistant)
och [konfigurationsguiden](https://docs.evcc.io/en/installation/configuration).
Exakta knappnamn kan skilja mellan evcc-versioner och språk; där vi är
osäkra står det ungefär.

1. Öppna evcc i webbläsaren (som Home Assistant-app typiskt via sidopanelen,
   annars ungefär `http://<värd>:7070`). Första gången får du sätta ett
   administratörslösenord.
2. Gå till **Configuration** / **Konfiguration**. Lägg till en enhet —
   ungefär under laddpunkter / wallbox / *Add charger* eller motsvarande.
   Välj tillverkaren **Home Assistant**.
3. Välj din Home Assistant-instans.
   - Instanser på samma nät upptäcks oftast automatiskt.
   - Hittas ingen: ange adressen manuellt, till exempel
     `http://homeassistant.local:8123` (byt mot din egen HA-adress).
   - Kör du evcc som **Home Assistant-app** ska du enligt evcc välja
     instansen `http://supervisor/core`. Den är redan auktoriserad, så
     nästa steg hoppas över.
4. **Nyare evcc** godkänner åtkomsten med **OAuth** i webbläsaren: Home
   Assistant öppnas i en ny flik, du loggar in och bekräftar begäran.
   Tokenet sparas av evcc och förnyas automatiskt. Du fyller inte i någon
   token i formuläret.
   **Äldre evcc** (utan OAuth-flöde) kan i stället be om ett
   *long-lived access token* som du skapar i Home Assistant under din
   profil. Det tokenet ska inte läggas i git.
5. Fyll i fälten enligt tabellen ovan. evcc föreslår entiteter som passar
   fältet (auto-complete). Kom ihåg:
   - *Enabled status sensor* och *Enable switch* = samma
     `evcc charging enabled`-switch.
   - *Charging status sensor* = `state charge` (bokstäverna A/B/C).
   - *Maximum current entity \[A\]* = `evcc current limit`.
   - Effekt, energi och L1–L3 kan ligga under avancerade alternativ.
6. Ungefär **Validate & save** / **Überprüfen & speichern** testar att
   entiteterna går att läsa, och sparar sedan laddaren.
7. Lägg till en **laddpunkt** (loadpoint) per box, knuten till den
   laddare du just skapade. Sätt ungefär **3 faser**, **min 6 A** och
   **max 15 A** (samma värden som i exemplet). Exakta fältnamn i
   gränssnittet är inte verifierade här — titta efter faser och min-/maxström.
8. Upprepa för den andra boxen. Spara. Om evcc ber dig starta om, gör det.

## 4. Alternativ B: via fil (`evcc.yaml`)

Använd den här vägen om du vill hålla laddarna i en YAML-fil. Ett färdigt
exempel finns i [`examples/evcc.yaml`](../examples/evcc.yaml). Filen är
bara avsnitten `chargers` och `loadpoints`; slå ihop dem med resten av
din evcc-konfiguration (site, mätare, tariffer med mera).

### Var ligger filen i evcc-appen för Home Assistant?

Titta under tilläggets flik **Configuration** på alternativet
`config_file`. Officiell evcc-dokumentation (från app-version 0.200.1,
16 februari 2025) säger att du sätter

```text
config_file: /config/evcc.yaml
```

Inne i appen pekar `/config/` på Home Assistant-sökvägen
`/addon_configs/<id>_evcc/`. För den vanliga evcc-appen är `<id>` enligt
dokumentationen `49686a9f`. Nightly-appen har ett annat id. Äldre
installationer kan fortfarande ha filen i Home Assistants vanliga
`/config/evcc.yaml` (sambasharen `config`). Lita på vad `config_file`
visar hos dig.

Du redigerar filen till exempel med:

- **Samba**, sharen `addon_configs` (eller `config` på äldre setup)
- **Studio Code Server** / Visual Studio Code: öppna mappen
  `/addon_configs/<id>_evcc`
- **File editor**: stäng av *Enforce Basepath* i tilläggets inställningar,
  starta om tillägget och gå till samma mapp

Starta om evcc-tillägget efter att du sparat filen.

### Autentisering i YAML

Nyare evcc använder **inte** `token:` i YAML för Home Assistant-mallen.
Godkänn i stället åtkomsten via OAuth i webbgränssnittet, som i
alternativ A, eller använd `http://supervisor/core` när evcc körs som
HA-app.

Äldre evcc kan kräva en rad

```yaml
token: <long-lived access token>
```

under varje laddare. Skapa tokenet i Home Assistant-profilen. I
exempelfilen finns raden utkommenterad som platshållare. Lägg aldrig ett
riktigt token i git.

### Blanda inte samma laddare i UI och YAML

evcc kan använda båda metoderna parallellt för *olika* enheter: poster i
`evcc.yaml` syns i gränssnittet men är inte redigerbara där, och
UI-konfigurerade enheter ligger i databasen (`evcc.db`). Konfigurera
**inte samma laddare eller laddpunkt på båda ställena** — då riskerar du
dubbletter eller krockar. Vill du flytta en YAML-laddare till
gränssnittet: skapa den i UI:t och ta sedan bort motsvarande post ur
filen ([evcc FAQ](https://docs.evcc.io/en/faq/)).

## 5. Exempel-YAML

Se [`examples/evcc.yaml`](../examples/evcc.yaml). Byt
`http://homeassistant.local:8123` mot din egen Home Assistant-adress.
Inga hemligheter och inga IP-adresser från hemmanätet ska ligga i filen.

## 6. Verifiering

1. I evcc ska laddpunkten visa att bilen är **ansluten** (status B) eller
   **laddar** (status C) när så är fallet — inte bara vänta i evighet när
   evcc har slagit på laddningen och satt ett tak på minst 6 A.
2. I Home Assistant: när evcc sätter till exempel 12 A ska
   **`diag current setpoint sent`** följa evcc:s `evcc current limit`,
   och **`diag current offered by box`** ska närma sig samma värde när
   boxen har tagit emot kommandot.
3. Slå på **`diag rs485 logging`**. I Director-loggen ska ett
   0x09-kommando synas när taket sätts, ungefär
   `state=0x09` och `avail=1200` för 12,00 A (värdet är i centiampere).

Mer om 0x09 och mätramen finns i
[docs/TWC-RS485-Sverige.md](TWC-RS485-Sverige.md).

## 7. Felsökning

| Symptom | Vanlig orsak |
|---------|----------------|
| evcc står på *waiting* / väntar, trots inkopplad bil | `evcc charging enabled` är OFF tills evcc skriver (så är det vid boot). Kontrollera att evcc faktiskt slår på switchen och att `state charge` är B eller C, inte A eller F. |
| Alltid 0 A, ingen laddning | `evcc current limit` startar på 0 A. evcc måste skriva ett tak ≥ 6 A. Kontrollera också att `diag current setpoint sent` följer, och med `diag rs485 logging` att 0x09 går ut på bussen. |
| Entity-id slutar på `_2` | Gamla entiteter efter namnbyte är kvar som otillgängliga. Ta bort dem under enheten Tesla-Director och peka evcc på id:t *utan* `_2`. |
| Fel box (Inne och Ute ihopblandade) | `twc_0` / `0x9983` är S22 Garage Inne, `twc_1` / `0x8988` är S22 Garage Ute. Kontrollera entity-id och slot mot serienumret i `info serial number`. |
| Checksummefel, trasiga ramar (`Checksum FAILED`) | RO/DI sitter på GPIO1/GPIO3 (UART0 / USB). Flytta dem till GPIO21/GPIO22 och sätt `rx_pin: GPIO21`, `tx_pin: GPIO22`. |
| Okänd laddstatus i evcc | `state charge` är troligen **F** (buss/box offline). evcc:s mall mappar A/B/C som standard. Kolla `system link ok` och RS-485. |
| evcc hittar inte entiteterna | Fel entity-id, enheten inte tillagd i HA, eller namn bytta i HA. Jämför med Utvecklarverktyg → Tillstånd. |

Namnstandarden i övrigt beskrivs i [README](../README.md#namnstandard-för-entiteter).

## Lathund

Kopiera värdena rakt in i evcc. Etiketterna är de officiella engelska från
evcc:s Home Assistant-mall, med YAML-nyckeln i parentes. Fält märkta
**(ungefär)** är inte verifierade mot evcc:s gränssnitt (främst
laddpunkten). Effekt, energi och L1–L3 kan ligga under avancerade
alternativ.

Anslutning till Home Assistant (`uri`, *Home Assistant URI*):
`http://homeassistant.local:8123` (byt mot din egen HA-adress). Kör du
evcc som Home Assistant-app: `http://supervisor/core`.

Statusbokstäver från `state charge`: **A** frånkopplad, **B** ansluten,
**C** laddar. **F** = fel / TWC offline (mappa inte till A/B/C). **D**
och **E** används inte. evcc känner redan igen A/B/C, så *States for
status A/B/C* (`statusA`/`statusB`/`statusC`) lämnas tomma om evcc inte
frågar.

### S22 Garage Ute (`0x8988`, `twc_1`)

| Fält i evcc (en, YAML) | Värde |
|------------------------|-------|
| Home Assistant URI (`uri`) | `http://homeassistant.local:8123` (byt mot din HA-adress); som HA-app: `http://supervisor/core` |
| Charging status sensor (`status`) | `sensor.tesla_director_s22_garage_ute_state_charge` |
| Enabled status sensor (`enabled`) | `switch.tesla_director_s22_garage_ute_evcc_charging_enabled` |
| Enable switch (`enable`) | `switch.tesla_director_s22_garage_ute_evcc_charging_enabled` |
| Maximum current entity [A] (`setMaxCurrent`) | `number.tesla_director_s22_garage_ute_evcc_current_limit` |
| Power entity (`power`) | `sensor.tesla_director_s22_garage_ute_meter_power` |
| Energy entity (`energy`) | `sensor.tesla_director_s22_garage_ute_meter_energy_total` |
| L1 current entity (`currentL1`) | `sensor.tesla_director_s22_garage_ute_meter_current_l1` |
| L2 current entity (`currentL2`) | `sensor.tesla_director_s22_garage_ute_meter_current_l2` |
| L3 current entity (`currentL3`) | `sensor.tesla_director_s22_garage_ute_meter_current_l3` |
| L1 voltage entity (`voltageL1`) | `sensor.tesla_director_s22_garage_ute_meter_voltage_l1` |
| L2 voltage entity (`voltageL2`) | `sensor.tesla_director_s22_garage_ute_meter_voltage_l2` |
| L3 voltage entity (`voltageL3`) | `sensor.tesla_director_s22_garage_ute_meter_voltage_l3` |
| States for status A (`statusA`) | `A` bara om evcc frågar, annars tomt. F = fel, D/E oanvända. |
| States for status B (`statusB`) | `B` bara om evcc frågar, annars tomt. |
| States for status C (`statusC`) | `C` bara om evcc frågar, annars tomt. |
| Title (`title`) **(ungefär)** | S22 Garage Ute |
| Charger (`charger`) **(ungefär)** | `twc_s22_garage_ute` |
| Phases (`phases`) **(ungefär)** | `3` |
| Min current (`mincurrent`) **(ungefär)** | `6` (6 A) |
| Max current (`maxcurrent`) **(ungefär)** | `15` (15 A) |

### S22 Garage Inne (`0x9983`, `twc_0`)

| Fält i evcc (en, YAML) | Värde |
|------------------------|-------|
| Home Assistant URI (`uri`) | `http://homeassistant.local:8123` (byt mot din HA-adress); som HA-app: `http://supervisor/core` |
| Charging status sensor (`status`) | `sensor.tesla_director_s22_garage_inne_state_charge` |
| Enabled status sensor (`enabled`) | `switch.tesla_director_s22_garage_inne_evcc_charging_enabled` |
| Enable switch (`enable`) | `switch.tesla_director_s22_garage_inne_evcc_charging_enabled` |
| Maximum current entity [A] (`setMaxCurrent`) | `number.tesla_director_s22_garage_inne_evcc_current_limit` |
| Power entity (`power`) | `sensor.tesla_director_s22_garage_inne_meter_power` |
| Energy entity (`energy`) | `sensor.tesla_director_s22_garage_inne_meter_energy_total` |
| L1 current entity (`currentL1`) | `sensor.tesla_director_s22_garage_inne_meter_current_l1` |
| L2 current entity (`currentL2`) | `sensor.tesla_director_s22_garage_inne_meter_current_l2` |
| L3 current entity (`currentL3`) | `sensor.tesla_director_s22_garage_inne_meter_current_l3` |
| L1 voltage entity (`voltageL1`) | `sensor.tesla_director_s22_garage_inne_meter_voltage_l1` |
| L2 voltage entity (`voltageL2`) | `sensor.tesla_director_s22_garage_inne_meter_voltage_l2` |
| L3 voltage entity (`voltageL3`) | `sensor.tesla_director_s22_garage_inne_meter_voltage_l3` |
| States for status A (`statusA`) | `A` bara om evcc frågar, annars tomt. F = fel, D/E oanvända. |
| States for status B (`statusB`) | `B` bara om evcc frågar, annars tomt. |
| States for status C (`statusC`) | `C` bara om evcc frågar, annars tomt. |
| Title (`title`) **(ungefär)** | S22 Garage Inne |
| Charger (`charger`) **(ungefär)** | `twc_s22_garage_inne` |
| Phases (`phases`) **(ungefär)** | `3` |
| Min current (`mincurrent`) **(ungefär)** | `6` (6 A) |
| Max current (`maxcurrent`) **(ungefär)** | `15` (15 A) |
