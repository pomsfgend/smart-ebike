# Связь с обманкой FGMDCharge: архитектура «кубик + контроллер», протокол, правки прошивки кубика

> Ревизия 2. Основано на коде <https://github.com/pomsfgend/FGMDCharge-FD-ROUND-V2> (клон только для чтения,
> `81bcd8c`). Номера строк — для этого коммита. Код кубика сюда **не копируется** (у репозитория нет MIT, внутри
> заводской дамп); новые файлы для кубика написаны с нуля и вызывают только его публичный API `app_state_*`.

## 1. Что кубик уже делает и чего не хватает

| Есть в кубике | Чего не хватает для быстрой зарядки |
|---|---|
| Полный диалог GB/T 2011 / 2015 / 2023-B: CHM/BHM, CRM/BRM (BAM), BCP, CML, BRO/CRO, BCL/BCS/BSM, CST/BST/BSD, BEM (`sm_relay.c`, `can_gbt.c`, `gbt_b.c`) | данных **реальной** батареи: в BCS/BSM/BSD зашиты 4,20 В ячейки, 25/20 °C, «заряд разрешён» всегда |
| Запрос BCL берётся из `app_state` (`bcl_requested_*`), задаётся вручную с экрана/веба (`/api/setpoints`) | автоматического запроса U/I по состоянию батареи |
| Реле A (GPIO6, «подготовка»), реле B (GPIO7, «нагрузка», через 3 с стабильного U CCS ≥ 50 В) | условия «батарея готова» для BRO 0xAA (сейчас 0xAA всегда через 2 с) |
| Остановка по CST/CEM, по тишине CAN 3 с, с экрана | остановки по причине батареи (полная, перегрев, BMS) с правильным BST |
| BHM/BCP max = 450 В (`CUBE_DEFAULT_MAX_VOLTAGE_0P1V`) | ограничения 275 В (иначе станция может подняться выше, чем выдерживают блоки) |
| Wi-Fi SoftAP «Cube», канал 6, веб; BLE (выключен `CUBE_BLE_ENABLE 0`) | связи с контроллером зарядки |
| — | управления преобразователем, контакторами батареи, чтения JBD — **это всё теперь в контроллере**, не в кубике |

## 2. Архитектура

```
  Станция ══CAN 250k══ КУБИК ))) ESP-NOW 10 Гц ((( КОНТРОЛЛЕР ═UART═ JBD×2
                        │ STATION ─────────────►     │ ═CAN 125k═ Flatpack2
                        │ ◄───────────── REQUEST     │ ─ PS_ON ─ CRPS
                        реле A/B                     K-BAT, PERMIT
```

- Кубик остаётся **единственным** узлом-«BMS» на шине станции. Контроллер к шине станции **не подключён**
  (у ESP32-C3 один TWAI, и он занят Flatpack2).
- Связь **без проводов к кубику**: ESP-NOW поверх радио, которое у кубика уже работает (SoftAP, канал
  `CUBE_WIFI_CHANNEL = 6`). ESP-NOW выбран вместо Wi-Fi/HTTP или BLE: нет соединения/рукопожатия, задержка
  единицы мс, работает одновременно с SoftAP и веб-сервером кубика, шифрование пира (LMK).
- **Вариант «пассивно слушать CAN станции»** (контроллер со своим трансивером на тех же S+/S−) возможен как
  **дополнительная** проверка (видеть CCS/CST независимо от радио), но не заменяет радио: передавать в
  BCL/BSM может только кубик (два узла с адресом 0xF4 на шине недопустимы). Для него нужен второй CAN-контроллер
  (MCP2515 по SPI — пинов на SuperMini не осталось) → отложено (задача FW-10).

## 3. Протокол FCL v1 (`firmware/common/fc_common/include/fc_link.h`)

Кадр (little-endian, без упакованных структур — сериализация вручную, одинаковая на обеих сторонах):

| Байт | Поле |
|---|---|
| 0–1 | `'F' 'C'` |
| 2 | версия = 1 |
| 3 | тип: 1 = STATION (кубик → контроллер), 2 = REQUEST (контроллер → кубик) |
| 4 | длина полезной нагрузки N |
| 5 … 4+N | полезная нагрузка |
| 5+N … 6+N | CRC-16/CCITT-FALSE по байтам 0 … 4+N |

Единицы — как в GB/T: 0,1 В, 0,1 А, **модули токов без смещения −400 А** (смещение добавляет кубик при кодировании).

### STATION, кубик → контроллер, каждые 100 мс (24 байта нагрузки)

| Поле | Тип | Откуда в кубике (`cube_state_snapshot_t`) |
|---|---|---|
| seq, ack | u16, u16 | счётчик кадров; последний принятый seq REQUEST |
| session | u8 | `main_state`: IDLE 0 / HANDSHAKE (1,2) 1 / PARAM (3) 2 / READY (4) 3 / CHARGING (5) 4 / STOP, FINALIZE 5 |
| protocol | u8 | `protocol` (1 = 2015, 2 = 2011, 3 = 2023-B) |
| flags | u8 | b0 CAN жив (`last_charger_rx_ms` < 1 с), b1 CML свежий (< 5 с), b2 CCS свежий (< 1 с), b3 `ccs_charge_permit == 1`, b4 CRO 0xAA, b5 реле A, b6 реле B, b7 станция завершает (CST/CEM < 2 с или stage COMPLETED/ERROR) |
| cst_reason | u8 | `cst_status_raw` |
| cml_vmax, cml_vmin, cml_imax, cml_imin | u16 ×4 | `cml_*` (токи — модули, большее = imax) |
| ccs_v, ccs_i | u16 ×2 | `ccs_output_voltage_raw`, `|ccs_output_current_0p1a|` |
| uptime_ms | u32 | время кубика |

### REQUEST, контроллер → кубик, каждые 100 мс (35 байт нагрузки)

| Поле | Тип | Куда в кубике |
|---|---|---|
| seq, ack | u16, u16 | |
| flags | u8 | b0 READY → разрешить BRO 0xAA; b1 ALLOW → BSM «заряд разрешён»; b2 STOP → BST и конец сессии; b3 HV_PERMIT (информ.) |
| bcl_mode, bcl_v, bcl_i | u8, u16, u16 | `app_state_set_bcl(v, −i, mode)` → существующий `tx_bcl()` |
| bcs_v, bcs_i | u16, u16 | `app_state_set_battery_vi(v, −i)` → существующий `tx_bcs()` (это **ВВ-сторона**: реальное U станции и ток, который мы берём) |
| bhm_vmax | u16 | BHM и BCP «макс. суммарное U» (275 В) |
| cell_vmax_mv, cell_vmax_no | u16, u8 | BSM/BCS (реальная макс. ячейка 1…36) |
| tmax_c, tmin_c, tmax_no, tmin_no | i8, i8, u8, u8 | BSM |
| soc, remain_min | u8, u16 | `app_state_set_soc()`, BCS |
| bsm_byte5 | u8 | BSM байт 6 (поля состояния, 00 = норма) |
| bst_reason, bst_fault, bst_error | u8, u16, u8 | BST байты 1…4 |
| batt_v, batt_i | u16, i16 | реальные 65 В / ток батареи — только для экрана/лога кубика |

### Тайм-ауты и безопасный отказ

| Возраст последнего кадра | Контроллер (нет STATION) | Кубик (нет REQUEST) |
|---|---|---|
| ≤ 300 мс | норма | норма |
| > 300 мс (`FCL_PAUSE_MS`) | **ток Flatpack → 0**, K-BAT пока держит | BCL I = 0, BSM «заряд не разрешён», BRO не 0xAA |
| > 1000 мс (`FCL_STOP_MS`) | стоп `link_lost` → ждёт ток < 1 А (≥ 300 мс, ≤ 2 с) → **K-BAT разомкнуть**, CRPS выкл., PERMIT снять (→ K5/K6 отпадают) | **BST** («прочая неисправность») + `end_session()` → реле A/B выкл. |
| ESP завис | «динамические» драйверы отпускают K-BAT и PERMIT за ~50 мс; Flatpack через ~15 с без логина уходит на 43,5 В | (кубик — как сейчас) |

Дополнительно: seq растёт; повтор/старый seq отбрасывается (`fcl_monitor_accept`), после тишины > 1 с принимается
любой (перезагрузка пира). Кадры только от MAC спаренного устройства, ESP-NOW с шифрованием (PMK + LMK).
Если кубик сменил канал (его STA подключился к роутеру — `web_sta_join()` переводит в APSTA и канал следует за
роутером), контроллер в состоянии IDLE сканирует каналы 1…13 (`clink_tick`). **Во время заряда канал менять нельзя** —
не подключать кубик к домашнему Wi-Fi на станции.

## 4. Последовательность сессии

| Шаг | Станция | Кубик | Контроллер |
|---|---|---|---|
| 1 | CHM/CRM | HANDSHAKE, шлёт STATION session=1 | IDLE → ARMED: читает JBD, CML ещё нет |
| 2 | CML | PARAM, BCP с max 275 В | проверяет окно CML (min ≤ 270 В) и BMS → READY=1 |
| 3 | — | READY: BRO 0x00 первые 2 с, **затем 0xAA только при свежем REQUEST.READY** | PERMIT = 1 |
| 4 | CRO 0xAA, K1/K2, CCS | CHARGING; реле B через 3 с стабильного U; BCL из REQUEST (≥ 1 А, чтобы станция дала напряжение) | WAIT_HV: Flatpack сообщает Vin ≈ 250 В → PS_ON CRPS |
| 5 | | | MATCH: Flatpack на Vbat − 12,2 В, лимит 2 А → Vstack ≈ Vbat → **K-BAT** |
| 6 | | BCL I растёт по REQUEST | CHARGING: ток +10 А/с до min(BMS, температура, 37 А × ниток) → CV 65,7 В |
| 7 | CCS | | полная (ток < 3 А 60 с) / перегрев / BMS / кнопка → RAMPDOWN, STOP=1 + причина |
| 8 | CST/CSD | BST с причиной → BSD → end_session | ток < 1 А → K-BAT открыть, CRPS выкл., PERMIT 0 → DONE → IDLE, когда session = 0 |

## 5. Правки в прошивке кубика (минимальные)

Новые файлы в `main/` (из этого репозитория): `fc_link.h`, `fc_link.c` (из `firmware/common/fc_common/`),
`fc_cube_link.h`, `fc_cube_link.c` (из `firmware/cube_patch/`). Проверены компиляцией (`-fsyntax-only`) против
настоящих `app_state.h`/`config.h` кубика с заглушками IDF, для IDF 5.0.9 и 5.1+.

| # | Файл кубика, место | Правка |
|---|---|---|
| 1 | `main/CMakeLists.txt`, `SRCS` (строки 5–22) | добавить `"fc_link.c" "fc_cube_link.c"`; в `REQUIRES` — `esp_wifi` (если ещё нет) |
| 2 | `main/main.c`, после `web_init()` (строка 108) | `fc_cube_link_init();` (без MAC в настройках — ничего не делает, кубик работает как раньше) |
| 3 | `main/sm_relay.c`, начало `sm_relay_tick()` (строка 449) | `fc_cube_link_tick(t);` после `uint32_t t = now_ms();` |
| 4 | `sm_relay.c`, `tx_bhm()` строка 114 и `tx_bcp()` строка 187 | `uint16_t vmax = fc_cube_link_enabled() ? fc_cube_bhm_vmax() : CUBE_DEFAULT_MAX_VOLTAGE_0P1V;` |
| 5 | `sm_relay.c`, `ST_READY_WAIT_CRO`, строка 543 | `else tx_bro((!fc_cube_link_enabled() \|\| fc_cube_ready_for_bro(t)) ? 0xAA : 0x00);` |
| 6 | `sm_relay.c`, `tx_bsm()` строка 306 | в начале: `if (fc_cube_fill_bsm(now_ms(), d)) { gbt_can_send(GBT_ID_BSM, d, 7); return; }` (иначе старая константа; при «паузе» связи — бит «не разрешён», см. 7) |
| 7 | `sm_relay.c`, `tx_bsm()` | если `fc_cube_link_enabled() && !fc_cube_charge_allowed(now_ms())` — не ставить бит `1u << 12` |
| 8 | `sm_relay.c`, `tx_bst()` строка 340 | `uint8_t d[8] = {0x01,0,0,0}; if (fc_cube_link_enabled()) { uint8_t b[4]; if (fc_cube_stop_requested(now_ms(), b)) memcpy(d, b, 4); }` |
| 9 | `sm_relay.c`, `case ST_CHARGING`, рядом со строкой 618 (проверка `charger_stage`) | `{ uint8_t b[4]; if (fc_cube_stop_requested(t, b)) { tx_bst(); tx_bsd(); end_session(); break; } }` |
| 10 | `sm_relay.c`, `ST_READY_WAIT_CRO` / `ST_PARAM_CONFIG` | то же условие стопа (контроллер отказал: BMS/окно станции) → `sm_stop_session()` |
| 11 | `web.c` `setpoints_post()` (строка 358), экран (`display.c` 626/670) | при `fc_cube_link_enabled()` ручные уставки отключить (иначе они перезапишут BCL от контроллера) |
| 12 | `web.c` `web_wifi_restart()` (строка 734) | после перезапуска Wi-Fi вызвать `fc_cube_link_init()` ещё раз (ESP-NOW переживает не всегда — **ПРОВЕРИТЬ**) |
| 13 | `settings.c` + веб | хранить MAC контроллера, PMK/LMK в NVS вместо `#define` (сейчас `FC_CUBE_PEER_MAC` в `fc_cube_link.c`) |
| 14 | `tx_bsd()` (строка 238) | по желанию: реальные мин./макс. ячейки и температуры (сейчас 3,30/4,20 В, 20/30 °C) — нужно добавить их в REQUEST v2 |

Что **не** меняется: пины, I2C, реле A/B и их логика (кроме того, что K5/K6 теперь ещё и через PERMIT контроллера),
приём CAN, BAM, 2023-B (`gbt_b.c` — отдельно проверить, что `fc_cube_stop_requested` тоже вызывает `gbt_b_request_stop()`,
задача CUBE-8).

**Важно про ток в BCL:** в кубике ток BCL хранится со знаком «минус = заряд» (`web.c:379`), а `tx_bcl()` берёт модуль
и сам ограничивает по CML. `fc_cube_link_tick()` пишет `-bcl_i` — так же, как веб.

## 6. Спаривание

1. Прошить контроллер, в логе взять его MAC (STA). Прошить кубик, в его логе/вебе взять MAC интерфейса **AP**.
2. В контроллер: `idf.py menuconfig` → *FC charger controller* → `FC_PEER_MAC` = MAC AP кубика, `FC_PMK` (16 символов),
   `FC_LMK` (32 hex).
3. В кубик: `FC_CUBE_PEER_MAC` = MAC STA контроллера, те же PMK/LMK (позже — через веб, правка 13).
4. Проверка: на контроллере в логе `idle -> armed` при подключении к станции; на кубике — `FCLINK: ESP-NOW link on`.
