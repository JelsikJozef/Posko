# semPraca – Random Walk (client/server)

Konzolová (terminal) client/server aplikácia pre simuláciu random-walk s agregáciou výsledkov na mriežke.
Komunikácia prebieha cez Unix-domain socket (AF_UNIX) – vhodné pre Linux server (bez GUI).

---

## Obsah

- [Požiadavky](#požiadavky)
- [Build](#build)
- [Spustenie na Linuxe](#spustenie-na-linuxe)
  - [Server](#server)
  - [Klient (menu)](#klient-menu)
- [Menu klienta (C9) + vstupy (C10)](#menu-klienta-c9--vstupy-c10)
  - [1) New simulation](#1-new-simulation)
  - [2) Join existing simulation](#2-join-existing-simulation)
  - [5) Start simulation](#5-start-simulation)
  - [4) Request snapshot](#4-request-snapshot)
  - [8) Re-render last snapshot](#8-re-render-last-snapshot)
  - [9) Dump cell from last snapshot](#9-dump-cell-from-last-snapshot)
  - [6) Save results](#6-save-results)
  - [7) Stop simulation](#7-stop-simulation)
  - [3) Restart finished simulation](#3-restart-finished-simulation)
  - [0) Quit](#0-quit)
- [Typický workflow (odporúčané použitie)](#typický-workflow-odporúčané-použitie)
- [Formát súboru RWRES (uloženie/načítanie)](#formát-súboru-rwres-uloženienaćítanie)
- [Troubleshooting](#troubleshooting)
- [Štruktúra projektu](#štruktúra-projektu)
- [Protokol správ (IPC)](#protokol-správ-ipc)
- [Konfigurácia a validácia vstupov](#konfigurácia-a-validácia-vstupov)
- [Multi-user a owner model (správanie)](#multi-user-a-owner-model-správanie)
- [Ukážkové runy (copy‑paste) + očakávané výstupy](#ukážkové-runy-copy-paste-vrátane-očakávaných-logov-a-tipov)

---

## Požiadavky

- Linux alebo macOS (kvôli Unix-domain socketom).
- `gcc` a `make`.
- Na Linuxe je dôležité, aby `Makefile` používal TABy v príkazoch (GNU make vyžaduje TAB na začiatku recipe riadkov).

---

## Build

V koreňovom priečinku projektu:

```sh
make clean
make
```

Binárky sa vytvoria do `build/`:

- `build/server`
- `build/client`

Poznámka: Makefile kompiluje automaticky všetky `.c` súbory v:
- `src/common/*.c`
- `src/client/*.c`
- `src/server/*.c`

Takže pri pridaní nových modulov netreba manuálne upravovať zoznam zdrojákov.

---

## Spustenie na Linuxe

### Server

Server počúva na Unix socket path:

- `/tmp/rw_test.sock`

Spustenie:

```sh
./build/server
```

Server ostane bežať v "lobby" a čaká na klientov.
Simulácia sa nespustí automaticky — spúšťa sa cez menu klienta (voľba **Start simulation**).

### Klient (menu)

Klient sa pripája na socket path ako parameter:

```sh
./build/client /tmp/rw_test.sock
```

Klient po pripojení spraví `JOIN`, načíta `WELCOME` a zobrazí menu.

---

## Menu klienta (C9) + vstupy (C10)

V menu sa pravidelne zobrazuje aj `STATUS` zo servera:
- stav simulácie: `LOBBY` / `RUNNING` / `FINISHED`
- či si v multi-user móde
- či má tento klient právo ovládať simuláciu (`can_control`)

### 1) New simulation

Táto voľba slúži na založenie novej simulácie.

Ponúkne dve cesty:

1) **Load world from file? = yes**
- zadáš cestu k súboru (RWRES), z ktorého sa načíta world (a prípadne aj konfigurácia)

2) **Load world from file? = no**
- zadáš parametre novej simulácie (C10):
  - rozmery sveta: `width`, `height`
  - typ sveta:
    - wrap (torus)
    - obstacles
  - počet replikácií
  - `K` (max krokov)
  - pravdepodobnosti pohybu:
    - `p_up`, `p_down`, `p_left`, `p_right`
    - na serveri sa kontroluje, že súčet ≈ 1

Následne klient pošle na server `CREATE_SIM` alebo `LOAD_WORLD`.

1Poznámka: momentálne server generuje prekážky deterministicky (seed + percent) pri obstacles móde a po generovaní garantuje, že každá voľná bunka je dosiahnuteľná z (0,0).

#### Reachability guarantee pri obstacle svete

- Po náhodnom rozmiestnení prekážok server spraví flood-fill od (0,0).
- Každá voľná bunka, ktorú flood-fill nezasiahol, dostane vyčistený koridor smerom k osi X,Y (t.j. „vyrežú“ sa prekážky na ceste k nule).
- Výsledkom je, že žiadna voľná bunka nezostane izolovaná a vždy existuje aspoň Manhattan cesta z (0,0) do ľubovoľnej voľnej bunky.

### 2) Join existing simulation

- Klient už je pripojený, takže je to prakticky „no-op“ položka.
- Používa sa ako logická možnosť: zostať pripojený a sledovať stav simulácie.
- Poznámka: aby sa nerozbíjalo interaktívne menu, klient **nevypisuje** priebežné
  `PROGRESS`/`END` notifikácie do konzoly. Priebeh vieš sledovať cez `STATUS`
  (pole `progress=current_rep`) alebo v logoch servera.

### 5) Start simulation

- Použi, keď je server v stave `LOBBY`.
- Klient pošle `START_SIM`.
- Server začne počítať replikácie.
  - Server loguje `Replication X/Y completed`.
  - Server môže posielať `PROGRESS` notifikácie, ale klient ich v menu režime
    len konzumuje (kvôli stabilite promptu).

### 4) Request snapshot

- Klient pošle `REQUEST_SNAPSHOT`.
- Server odošle snapshot stream:
  - `SNAPSHOT_BEGIN`
  - 0..N `SNAPSHOT_CHUNK`
  - `SNAPSHOT_END`
- Po dokončení príjmu klient vytlačí **radial summary** + kompaktný **grid preview** (ľavý horný roh, max 24x12), aby bolo vidieť aj per-cell vzory.
- Legend pre grid:
  - `' '` : bunka bez trialov
  - `..@` : rastúca pravdepodobnosť úspechu v rámci K ('.' nízka → '@' vysoká)
  - `##` : prekážka (obstacle)

### 5) Start simulation

- Použi, keď je server v stave `LOBBY`.
- Klient pošle `START_SIM`.
- Server začne počítať replikácie.
  - Server loguje `Replication X/Y completed`.
  - Server môže posielať `PROGRESS` notifikácie, ale klient ich v menu režime
    len konzumuje (kvôli stabilite promptu).

### 8) Re-render last snapshot

- Znovu zobrazí posledný prijatý snapshot (radial summary + grid preview + legenda).
- Už nežiada server o nový snapshot.

### 9) Dump cell from last snapshot

- Vypýta si súradnice `x`, `y` a vypíše údaje z posledného snapshotu pre konkrétnu bunku:
  - obstacle áno/nie
  - trials, succ<=K
  - priemerné kroky pri úspechu (ak existujú)
  - p(success<=K)
- Funguje iba po tom, čo bol prijatý snapshot.

### 6) Save results

- Zadáš cestu k výstupnému súboru.
- Klient pošle `SAVE_RESULTS`.
- Server uloží výsledky v binárnom formáte RWRES.

### 7) Stop simulation

- Pošle `STOP_SIM`.
- Stop je kooperatívny (worker pool dobehne aktuálnu prácu a simulácia skončí).

### 3) Restart finished simulation

Toto je presne C10 "opätovné spustenie":

- zadáš:
  - súbor, z ktorého sa načíta (RWRES)
  - nový počet replikácií
  - súbor, do ktorého sa uloží výsledok

Flow:
1) `LOAD_RESULTS` (server načíta world+results)
2) `RESTART_SIM` (server naštartuje nové replikácie)
3) po `END` klient zavolá `SAVE_RESULTS`

### 0) Quit

- Klient pošle `QUIT` a korektne ukončí spojenie.
- Ak bežíš interaktívne v TTY, menu sa spýta, či má klient (ak je owner) zastaviť simuláciu pri odchode.
- Ak stdin nie je TTY (napr. pipe), klient sa na nič nepýta, aby sa dal skriptovať.

---

## Typický workflow (odporúčané použitie)

### Scenár A: Nová simulácia od nuly

1. Spusti server:
   ```sh
   ./build/server
   ```
2. Spusti klienta:
   ```sh
   ./build/client /tmp/rw_test.sock
   ```
3. V menu zvoľ:
   - `1) New simulation`
   - vyplň parametre (rozmery, p_*, K, replikácie)
4. Zvoľ:
   - `5) Start simulation`
5. Počas behu môžeš:
   - `4) Request snapshot`
6. Po skončení (`END`):
   - `6) Save results`

### Scenár B: Opätovné spustenie už ukončenej simulácie

1. V klientovi zvoľ:
   - `3) Restart finished simulation`
2. Zadaj:
   - RWRES input súbor
   - nové replikácie
   - RWRES output súbor

---

## Formát súboru RWRES (uloženie/načítanie)

Ukladanie/načítanie je implementované v:
- `src/server/persist.c`
- `src/server/persist.h`

Je to jednoduchý binárny formát:
- magic: `RWRES` (8 bytes vrátane NUL paddingu)
- verzia (momentálne 1)
- world kind
- width/height
- probabilities (double)
- K
- total_reps
- obstacles + výsledkové polia (trials, sum_steps, success_leq_k)

Poznámka: Formát je určený primárne pre interné použitie v projekte.

---

## Troubleshooting

### 1) `make: *** No rule to make target 'src/client.c' ...`

To znamená, že používaš **starý Makefile**, ktorý očakáva `src/client.c`.
Aktuálna štruktúra je `src/client/*.c` (napr. `client_main.c`, `ui_menu.c`, ...).

Riešenie:
- uisti sa, že na serveri máš aktuálny `Makefile` z tohto repozitára,
- alebo sprav `git pull` / znovu nakopíruj projekt.

### 2) Server nespustí socket / klient sa nevie pripojiť

- Over, že server beží.
- Over socket súbor:
  ```sh
  ls -la /tmp/rw_test.sock
  ```
- Ak zostal "stale" socket po páde, server ho pri štarte unlinkne, ale ak nemá práva do `/tmp`, môže to zlyhať.

### 3) `Permission denied` pri menu akciách

- Server má koncept "owner" (prvý pripojený klient).
- Ak nie si owner, server môže odmietnuť `CREATE/LOAD/START/STOP/SAVE/RESTART`.

---

## Štruktúra projektu

```
src/
  common/   # protokol, util, typy
  client/   # client IPC + konzolové menu
  server/   # server IPC + simulácia + perzistencia
```

Dôležité súbory:
- `src/common/protocol.h` – definície správ
- `src/client/ui_menu.c` – menu (C9/C10)
- `src/server/server_ipc.c` – obsluha menu správ na serveri
- `src/server/sim_manager.c` – simulácia (worker pool)
- `src/server/persist.c` – RWRES save/load

---

## Protokol správ (IPC)

Komunikácia klient–server prebieha cez Unix-domain socket a vlastný binárny protokol definovaný v `src/common/protocol.h`.
Každá správa má hlavičku `rw_msg_hdr_t` a následne payload s dĺžkou `payload_len`.

### Základný handshake

#### `RW_MSG_JOIN` (client → server)
- Payload: `rw_join_t`
- Účel: klient oznámi svoju identitu (PID) a začne session.

#### `RW_MSG_WELCOME` (server → client)
- Payload: `rw_welcome_t`
- Účel: server pošle klientovi aktuálnu konfiguráciu sveta a režim.

### Status / kontrolné správy (menu)

#### `RW_MSG_QUERY_STATUS` (client → server)
- Payload: `rw_query_status_t`
- Odpoveď: `RW_MSG_STATUS`

#### `RW_MSG_STATUS` (server → client)
- Payload: `rw_status_t`
- Obsahuje:
  - `state`: `LOBBY | RUNNING | FINISHED`
  - `multi_user`, `can_control`
  - world config (kind, size)
  - `K`, `total_reps`, `current_rep`

#### `RW_MSG_CREATE_SIM` (client → server)
- Payload: `rw_create_sim_t`
- Účel: vytvoriť novú simuláciu “od nuly” (rozmery, p_*, K, reps, world kind, multi-user).
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_LOAD_WORLD` (client → server)
- Payload: `rw_load_world_t` (`path`, `multi_user`)
- Účel: načítať world zo súboru RWRES (bez výsledkov).
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_START_SIM` (client → server)
- Payload: žiadny
- Účel: spustiť simuláciu zo stavu `LOBBY`.
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_RESTART_SIM` (client → server)
- Payload: `rw_restart_sim_t` (`total_reps`)
- Účel: spustiť nové replikácie pri už pripravenom world/results (typicky po `LOAD_RESULTS`).
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_STOP_SIM` (client → server)
- Payload: `rw_stop_sim_t` (`pid`)
- Účel: kooperatívne zastaviť bežiacu simuláciu.
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_SAVE_RESULTS` (client → server)
- Payload: `rw_save_results_t` (`path`)
- Účel: uložiť world + výsledky do RWRES súboru.
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_LOAD_RESULTS` (client → server)
- Payload: `rw_load_results_t` (`path`)
- Účel: načítať world + výsledky z RWRES súboru a nastaviť server do stavu `FINISHED`.
- Odpoveď: `RW_MSG_ACK` alebo `RW_MSG_ERROR`

#### `RW_MSG_QUIT` (client → server)
- Payload: `rw_quit_t` (`pid`, `stop_if_owner`)
- Účel: korektné ukončenie klienta.
- Odpoveď: `RW_MSG_ACK` (best-effort)

### Notifikácie a streamy

#### `RW_MSG_PROGRESS` (server → client)
- Payload: `rw_progress_t`
- Účel: informácia o priebehu: `current_rep/total_reps`.
- Poznámka: klient v menu režime tieto správy neprintuje (aby sa nerozbíjal
  prompt), ale stále ich musí čítať.

#### `RW_MSG_END` (server → client)
- Payload: `rw_end_t` (reason)
- Účel: signalizuje koniec simulácie (normálny koniec alebo stop).
- Poznámka: klient v menu režime tieto správy neprintuje (aby sa nerozbíjal
  prompt).

#### Snapshot stream

- `RW_MSG_REQUEST_SNAPSHOT` (client → server), payload: `rw_request_snapshot_t`
- následne server pošle:
  1) `RW_MSG_SNAPSHOT_BEGIN` payload: `rw_snapshot_begin_t`
  2) `RW_MSG_SNAPSHOT_CHUNK` payload: `rw_snapshot_chunk_t` (chunky dát)
  3) `RW_MSG_SNAPSHOT_END` payload: (0 bytes)

### ACK / ERROR

#### `RW_MSG_ACK` (server → client)
- Payload: `rw_ack_t`
- Obsah: `request_type` (na ktorú požiadavku odpovedá) a `status` (0 = OK).

#### `RW_MSG_ERROR` (server → client)
- Payload: `rw_error_t`
- Obsah: `error_code` + `error_msg`.

---

## Konfigurácia a validácia vstupov

### Validácia na serveri

Pri `CREATE_SIM` server kontroluje minimálne:
- `width > 0`, `height > 0`
- `total_reps > 0`
- `K > 0`
- pravdepodobnosti `p_up+p_down+p_left+p_right ≈ 1` (tolerancia ~0.001)
- nesmie byť `RUNNING` (ak je, vráti `ERROR`)

Pri `RESTART_SIM` server kontroluje:
- musí byť **mimo RUNNING** (t.j. `LOBBY` alebo `FINISHED`)
- `total_reps > 0`

Pri `LOAD_WORLD` / `LOAD_RESULTS`:
- súbor musí byť validný RWRES (magic+verzia)

### Poznámky k číselným vstupom

- `K`: používa sa v metrikách úspech do K krokov a v random-walk limitoch.
- Pravdepodobnosti odporúčanie: používaj hodnoty typu `0.25` a kontroluj súčet.

---

## Multi-user a owner model (správanie)

Aplikácia podporuje mód "multi-user" v zmysle:
- môže sa pripojiť viac klientov naraz,
- všetci môžu sledovať `STATUS`, `PROGRESS`, `END` a pýtať snapshot,
- **kontrolné operácie** (create/load/start/stop/restart/save) sú zjednodušene viazané na "owner".

### Kto je owner

- Owner je **prvý pripojený klient** (server si uloží `owner_fd`).
- Ak owner odíde, server owner vymaže (`owner_fd = -1`) a ďalší klient, ktorý sa pripojí, sa môže stať owner.

### Prečo je to tak

- Je to bezpečné/deterministické: nevzniká konflikt, keď 2 klienti naraz pošlú `START_SIM` alebo `STOP_SIM`.

### Čo môže robiť ne-owner klient

- `QUERY_STATUS`
- prijímať notifikácie (PROGRESS/END)
- `REQUEST_SNAPSHOT`

### Čo vyžaduje owner

- `CREATE_SIM`, `LOAD_WORLD`, `START_SIM`, `STOP_SIM`
- `SAVE_RESULTS`, `LOAD_RESULTS`, `RESTART_SIM`

---

## Ukážkové runy (copy‑paste) + očakávané výstupy

### 0) Build na Linuxe

```sh
make clean
make
```

Očakávané:
- vznikne `build/client` a `build/server`

### 1) Spustenie servera

```sh
./build/server
```

Očakávané logy (príklad):
- `Server listening on socket: /tmp/rw_test.sock`
- `Server running (lobby). Ctrl+C to stop.`

### 2) Spustenie klienta

```sh
./build/client /tmp/rw_test.sock
```

Očakávané:
- klient vypíše `WELCOME` a zobrazí menu
- server zaloguje pripojenie klienta (`Client joined`, `WELCOME`)

### 3) Kompletný scenár: nová simulácia + start + save

1. Klient: `1) New simulation`
   - Load world? **n**
   - Multi-user? **n**
   - width: `20`
   - height: `20`
   - obstacles? `n` (wrap)
   - reps: `50`
   - K: `200`
   - p_up: `0.25`
   - p_down: `0.25`
   - p_left: `0.25`
   - p_right: `0.25`
2. Klient: `5) Start simulation`
3. Počas behu sleduj priebeh buď:
   - na serveri (`Replication X/Y completed`), alebo
   - v klientovi cez pravidelne vypisovaný `STATUS` (`progress=current_rep`).
4. Po konci: `6) Save results`
   - path: napr. `out.rwres`

### 4) Kompletný scenár: restart z výsledku

1. Klient: `3) Restart finished simulation`
   - load: `out.rwres`
   - new reps: `100`
   - save: `out2.rwres`

Očakávané:
- klient čaká na `END` a potom uloží výsledok.

### 5) Skriptovanie (non-interactive)

Klient sa dá aspoň čiastočne skriptovať tým, že `Quit` sa nepýta na ďalšie otázky, ak stdin nie je TTY.

Príklad rýchleho pripojenia a odchodu:

```sh
printf '0\n' | ./build/client /tmp/rw_test.sock
```
