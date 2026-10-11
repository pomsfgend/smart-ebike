# Интеграция с обманкой FGMDCharge-FD-ROUND-V2

Изучено: <https://github.com/pomsfgend/FGMDCharge-FD-ROUND-V2>, ветка `main`, релиз **2.8.17**,
коммит `81bcd8c` (11.10.2026 04:19 МСК). Локальная копия только для чтения: `/workspace/ref/FGMDCharge-FD-ROUND-V2`.
Порт на S3 (<https://github.com/pomsfgend/FGMDCharge-S3>, 3.0.0) — только для справки, файлы `main/` те же плюс `main/boards/`.

Лицензия: исходники обманки — MIT; **заводской дамп `original/` и бинарники — не MIT**. Мы ничего не копируем;
ниже — имена файлов/функций и описание правок. Номера строк — для коммита `81bcd8c`.

## 1. Что обманка уже делает

| Область | Как сделано | Файл / функция |
|---|---|---|
| Платформа | ESP32-C3, ESP-IDF **5.0.9** строго (`idf: >=5.0.9,<5.1.0`), LVGL 8.3.11, OTA на два слота | `main/idf_component.yml`, `partitions.csv` |
| CAN | TWAI 250 кбит/с, extended, accept-all, очередь RX 32 / TX 16, восстановление после bus-off, история 48 кадров | `can_gbt.c`: `gbt_can_init()`, `gbt_can_task()`, `update_bus_state()` |
| Разбор кадров станции | CHM, CRM, CML (6/8 Б), CRO, CCS (6/7+ Б, бит разрешения), CST, CSD, CEM, BCL (эхо) | `can_gbt.c`: `decode_known()` → `app_state_set_*()` |
| Автоопределение версии | 2011 / 2015 (= 2023-A) / 2023-B по первому кадру, ручная фиксация | `app_state.c`: `app_state_can_rx()`, `app_state_set_protocol()` |
| Сессия (BMS-сторона) | состояния 0…7: IDLE, HANDSHAKE_INIT, WAIT_CRM, PARAM_CONFIG, READY_WAIT_CRO, CHARGING, STOP, FINALIZE; тик 10 мс | `sm_relay.c`: `sm_relay_tick()`, `set_state()`, `end_session()` |
| Передача | BHM, BRM 49/41 Б, BCP 13 Б, BRO (2 с 0x00 → 0xAA), BCL 50 мс, BCS 250 мс, BSM 250 мс, BMV/BMT/BSP 10 с/1 с, BST, BSD, BEM | `sm_relay.c`: `tx_*()` |
| Многопакетные | передача BAM-подобно (0x20 + DT с паузой 10 мс, блокирующе внутри тика); приём — сборщик RTS/BAM | `sm_relay.c`: `tx_bam()`; `gbt_tp.c`: `gbt_tp_consume()` |
| Реле | GPIO6 «prepare» вкл. при входе в READY; в CHARGING при CCS ≥ 50,0 В стабильно (±20 %) 3 с → GPIO7 «load» вкл., GPIO6 выкл.; CCS < 30 В → выкл.; в IDLE/STOP — всё выкл. | `sm_relay.c`: `set_state()`, `case ST_CHARGING` (стр. 554–592) |
| Запрос к станции | **фиксированные уставки** U/I/SOC/ёмкость из веба (`/api/setpoints`) или с экрана; по умолчанию 240 В / 50 А | `web.c`: `setpoints_post()` (стр. 358); `display.c` (≈ стр. 626, 670) |
| Учёт энергии | интегрирует U×I из CCS | `sm_relay.c`, `app_state_add_energy()` |
| Завершение | CST/CEM/CSD → стадия станции → BST + BSD один раз → IDLE; «CAN молчит 3 с» → стоп; пауза CCS не завершает | `sm_relay.c` стр. 615–640, `app_state_set_ccs()` |
| 2023 класс B (ChaoJi) | отдельный автомат | `gbt_b.c` |
| UI / сервис | Wi-Fi AP «Cube» / `12345678`, веб с сырой отправкой CAN, BLE (выкл.), журнал сессий во флеше | `web.c`, `ble.c`, `session_log.c` |

Открытые задачи в репозитории обманки (на 11.10.2026): #9 «старые исходники до 2.8.17 не архивированы»,
#8 «сборка на ESP32-S3» (решена отдельным репозиторием FGMDCharge-S3). Про зарядку issues нет.

## 2. Чего не хватает для настоящей быстрой зарядки

| # | Чего нет | Почему важно | Чем закрываем |
|---|---|---|---|
| 1 | **Нет реальной батареи.** Ячейка всегда 4,20 В, температуры 20–25 °C константы, SOC оценивается по энергии, тип «тройной литий», 400 В/100 Ач | станция не знает реального состояния; нет защиты батареи | `fc_fill_bcs/bsm/bcp/brm/bsd()` с данными двух JBD |
| 2 | **Нет связи с BMS** (UART/BLE к JBD) | BMS — источник истины | `jbd_bms.c`, `fc_hw_bms.c` |
| 3 | **Нет регулирования тока.** BCL = фиксированная уставка из веба; нет CC/CV, нет окончания по току | перезаряд, перегрев | `fc_supervisor.c` + DC-DC |
| 4 | **Нет DC-DC** — обманка рассчитана на нагрузку, принимающую напряжение станции (≥ 200 В) | батарея 65,7 В | `docs/DCDC_DESIGN.md`, `fc_hw_dcdc.c` |
| 5 | **Нет измерений** (АЦП не используется; `adc_pwm.c` — только подсветка) | нет Vin/Vout/T/CC2 | ADS1115 по I²C |
| 6 | **Нет CC2** — подключение определяется по наличию CAN; отключение — «CAN молчит 3 с» | при выдёргивании пистолета ток надо снять за миллисекунды | `fc_cc2_read()` → `FC_STOP_CC2_LOST` |
| 7 | **Ручное управление реле** (долгое нажатие, BLE 0x66, веб `/api/can/raw`) | на ВВ-контакторе это опасно | отключить в сборке fastcharge (`CUBE_FASTCHARGE`) |
| 8 | **BRO = 0xAA через 2 с безусловно** | готовность должна зависеть от батареи и контактора | `fc_ready_for_bro()` |
| 9 | **Завершение упрощено:** BST один раз, BSD один раз, сразу IDLE; не ждёт CST/CSD и спада тока < 5 А перед размыканием | размыкание под током, станции пишут ошибку | правка `ST_STOP/ST_FINALIZE` (задача GBT-6) |
| 10 | **TP только BAM-подобно**, блокирует тик SM до 70 мс (BRM 7 пакетов × 10 мс) | часть станций может требовать RTS/CTS; джиттер BCL | `gbt_tp_tx.c` (неблокирующий, RTS/CTS + BAM) |
| 11 | Тайм-ауты 20–30 с вместо ~5 с стандарта | дольше висит в ошибке | оставить, пока не мешает (см. PROTOCOL.md п. 5) |
| 12 | Нет контактора/предзаряда на стороне батареи | бросок тока на конденсаторы DC-DC | `fc_task()` K-BAT + предзаряд |
| 13 | Несоответствие комментария: `config.h` «BCL defaults: 400.0 V / 10.0 A», а значения 2400/500 = **240 В / 50 А** | путаница | сообщить автору (только описание, без PR) |
| 14 | Wi-Fi AP с паролем по умолчанию `12345678` и сырой CAN | в устройстве, управляющем 6,6 кВт, это риск | в сборке fastcharge: свой пароль, `/api/can/raw` и `/api/setpoints` выкл. |

## 3. Как подключить компонент

1. Скопировать `firmware/components/fastcharge/` в `components/fastcharge/` дерева обманки.
2. `main/CMakeLists.txt` обманки: добавить `fastcharge` в `REQUIRES`.
3. `main/config.h`: добавить флаг сборки
   ```c
   #define CUBE_FASTCHARGE 1   /* 0 = поведение обманки без изменений */
   ```
4. `main/main.c`, `app_main()`: после `sm_relay_init()` и **до** `gbt_can_init()`:
   ```c
   #if CUBE_FASTCHARGE
   #include "fc_api.h"
       if (fc_start() != ESP_OK) ESP_LOGE(TAG, "fastcharge off");
   #endif
   ```
   I²C: `fc_i2c_init()` терпит уже установленный драйвер I²C0 тачскрина (`ESP_ERR_INVALID_STATE`), и наоборот.

## 4. Точки врезки (TODO в коде обманки)

Все правки — под `#if CUBE_FASTCHARGE`. «fc_» — функции из `components/fastcharge/include/fc_api.h`.

### 4.1 `can_gbt.c` → `gbt_can_task()` (стр. ~278, сразу после `decode_known(&rx);`)
```c
    fc_on_can_rx(f.id, f.data, f.dlc);          /* CML, CCS, CST → fc_station_t */
    /* и если используется gbt_tp_tx: gbt_tp_tx_on_rx(&s_tp_tx, f.id, f.data, f.dlc, now_ms()); */
```

### 4.2 `sm_relay.c` → `set_state()` (стр. 66)
В начале функции сообщить состояние:
```c
    static const fc_session_t map[8] = { FC_SESS_IDLE, FC_SESS_HANDSHAKE, FC_SESS_HANDSHAKE,
        FC_SESS_PARAM, FC_SESS_READY, FC_SESS_CHARGING, FC_SESS_IDLE, FC_SESS_IDLE };
    fc_on_session_state(map[s & 7]);
```

### 4.3 Полезные данные: `tx_bhm()` (111), `tx_brm()` (152), `tx_bcp()` (177), `tx_bcl()` (256), `tx_bcs()` (280), `tx_bsm()` (306), `tx_bsd()` (238)
Шаблон: собрать буфер через `fc_fill_*()`, если вернулся 0 — оставить старый код.
```c
static void tx_bcl(void)
{
#if CUBE_FASTCHARGE
    uint8_t d[8] = {0};
    size_t n = fc_fill_bcl(d);
    if (n) { (void)gbt_can_send(GBT_ID_BCL, d, (uint8_t)n); return; }
#endif
    ... существующий код ...
}
```
Для многопакетных (`tx_brm`, `tx_bcp`, `tx_bcs`): `n = fc_fill_bcp(b); tx_bam(0x000600u, b, n);`.
В `tx_brm()` после `fc_fill_brm()` переписать VIN из `app_state_vin()` в байты 24…40 (как сейчас).
**Регулирование тока — не здесь.** BCL лишь просит у станции напряжение/ток; реальный ток задаёт `fc_task` через DC-DC.

### 4.4 `case ST_PARAM_CONFIG` (524): проверка станции
После получения CML: если `fc_get_status()` → `!fc_station_compatible(&st.station)`, послать BST
(`fc_stop_requested()` вернёт `FC_STOP_STATION_RANGE` на следующем шаге супервизора) и `end_session()`.

### 4.5 `case ST_READY_WAIT_CRO` (539): готовность
Заменить «2 с 0x00, потом 0xAA» на:
```c
    tx_bro(fc_ready_for_bro() ? 0xAA : 0x00);
```
Тайм-аут состояния 30 с оставить (батарее нужно ≤ 2–3 с на предзаряд K-BAT).

### 4.6 `case ST_CHARGING` (554): реле и стоп
- Логика реле обманки сохраняется: A = предзаряд ВВ-входа DC-DC, B = K5/K6. Порог `v >= 500` (50,0 В) заменить на
  `v >= FC_DCDC_VIN_MIN_0P1V` (250 В), порог выключения `< 300` — на, например, 200 В.
- Ветка `s_load_manual` (ручная фиксация) — под `#if !CUBE_FASTCHARGE`.
- Перед существующей проверкой `charger_stage == COMPLETED/ERROR/SUSPENDED` добавить:
  ```c
      uint8_t r; uint16_t f; uint8_t e;
      if (fc_stop_requested(&r, &f, &e)) { tx_bst_reason(r, f, e); tx_bsd(); end_session(); }
  ```
  где `tx_bst_reason()` — вариант `tx_bst()` (стр. 340) с параметрами вместо `0x01, 0, 0, 0`.
- **Ограничение тока станции по веб-уставке** (`st.bcl_requested_current_0p1a`) в режиме fastcharge не используется.

### 4.7 `ST_STOP` / `ST_FINALIZE` (629) и `end_session()` (98) — правильное завершение (задача GBT-6)
Сейчас `end_session()` проходит STOP → FINALIZE → IDLE за один вызов. Нужно: в STOP слать BST каждые 10 мс до CST (≤ 5 с),
затем BSD каждые 250 мс до CSD (≤ 10 с); реле B размыкать только когда `fc_get_status().dcdc` показывает ток ≈ 0 и
CCS ток < 5 А. `fc_on_session_state(IDLE)` уже выключает DC-DC немедленно.

### 4.8 Отключение пистолета
В конце `sm_relay_tick()` (стр. ~636) рядом с «unplug: CAN silent» — `fc_core` сам остановит DC-DC при потере CC2
(`FC_STOP_CC2_LOST`); сессия завершится через `fc_stop_requested()`.

### 4.9 Сервисные входы (отключить при `CUBE_FASTCHARGE`)
`sm_request_relay_test()` (392), `sm_ble_cmd_0x66()` (417), `web.c` `setpoints_post()` (358) и `can_raw_post()` (406),
ввод уставок на экране (`display.c` ≈ 626/670). Вместо уставок — показать данные из `fc_get_status()`.

### 4.10 UI (по желанию)
`web.c` `status_get` / `display.c`: вывести SOC, ток Iref, Vin/Vout, T, причину стопа (`fc_stop_reason_name()`).

## 5. Соглашения, которым следует компонент

- ESP-IDF 5.0.x, C11, `idf_component_register`, legacy-драйверы (`driver/i2c.h`, `driver/uart.h`, `driver/twai.h`), как в обманке.
- Единицы как в `app_state.h`: 0,1 В, 0,1 А; ток заряда в кадрах отрицательный (смещение −400 А).
- ID кадров совпадают с `GBT_ID_*` из `can_gbt.h` (в компоненте — `GBTC_ID_*`, чтобы собираться отдельно).
- Задачи FreeRTOS с приоритетом 3 (ниже CAN 4 и SM 5 обманки).
- Конфигурация — `#define` в `fc_config.h` (у обманки — `CUBE_*` в `config.h`); можно перенести с префиксом `CUBE_FC_*`.
- Ничего не трогаем в OTA/разделах: компонент добавляет ~15–20 КБ кода (**ПРОВЕРИТЬ** влезание в слот 0x190000).

## 6. Альтернатива: без изменения автомата обманки

Если автор обманки не хочет правок в `sm_relay.c`, минимальный вариант: только 4.1, 4.2, 4.3 (BCL/BCS/BSM) и 4.5.
Остальное (завершение, сервисные входы) — желательно, но DC-DC всё равно выключается компонентом сам по CC2/BMS/FAULT.
