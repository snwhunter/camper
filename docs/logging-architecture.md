# Logging architecture

## Goal

The ESP32-2432S028R is the primary acquisition device for the camper battery system.

It will:

1. Connect directly to the ECO-WORTHY battery BMS over BLE.
2. Decode battery telemetry locally.
3. Write every sample to local storage first.
4. Expose current values to Home Assistant over Wi-Fi.
5. Upload unsynced samples to the existing Google Sheets / Apps Script backend when Internet connectivity is available.
6. Continue logging while Wi-Fi, Home Assistant, or the Internet is unavailable.

## Data path

```text
ECO-WORTHY BMS
     |
     | BLE
     v
ESP32-2432S028R
     |
     +--> touchscreen display
     |
     +--> Home Assistant live entities
     |
     +--> microSD local log / sync queue
                |
                | HTTPS when online
                v
        Google Apps Script backend
                |
                v
           Google Sheet
```

## Local-first rule

A sample is considered captured only after it has been written successfully to local storage.

Network upload happens after the local write. The ESP32 must never discard a sample simply because Wi-Fi, Home Assistant, DNS, or the Google endpoint is unavailable.

## Suggested sample record

Each record should contain a unique sequence ID so uploads are idempotent and duplicates can be ignored by the backend.

Fields:

- sequence_id
- timestamp
- soc_pct
- voltage_v
- current_a
- power_w
- remaining_ah
- capacity_ah
- charging
- discharging
- temperature_c
- temp1_c
- temp2_c
- temp3_c
- cell1_v
- cell2_v
- cell3_v
- cell4_v
- cell_min_v
- cell_max_v
- cell_delta_mv
- health_pct
- problem_code
- problem
- cycle_capacity
- sync_status

## Local storage format

Use the CYD microSD card as a durable append-only queue.

Recommended layout:

```text
/logs/2026/08/2026-08-15.csv
/state/sync.json
```

The CSV file is the canonical local record. `sync.json` stores the last successfully acknowledged sequence ID and other small persistent state.

Do not rewrite the CSV on every upload. Append records and advance the acknowledged sequence instead.

## Sync behavior

Normal behavior:

1. Acquire BMS sample.
2. Assign monotonically increasing `sequence_id`.
3. Append sample to SD.
4. Update display and Home Assistant entities.
5. If Internet is available, send a batch of unsynced rows to the Google Apps Script endpoint.
6. Backend writes only sequence IDs it has not already received.
7. Backend returns the highest accepted sequence ID.
8. ESP32 advances its local sync cursor.

Batching is preferred over one HTTP request per measurement. Initial target: up to 25-100 rows per upload batch.

## Sampling

Start with a 10-second battery sample interval. This is detailed enough for charging/discharging history while keeping storage and upload volume small.

The display may refresh more frequently than the log interval.

## Connectivity priorities

BLE acquisition and local storage have priority over network synchronization.

Losing Home Assistant or Internet connectivity must not interrupt BLE acquisition or logging.

## Boot recovery

On startup the ESP32 should:

1. Mount the SD card.
2. Restore the next sequence ID and sync cursor.
3. Start BLE acquisition.
4. Start local logging immediately.
5. Connect to Wi-Fi / Home Assistant independently.
6. Resume backlog synchronization when Internet access returns.

## Storage failure

If the SD card is absent or fails:

- Show a visible `SD ERROR` status on the touchscreen.
- Expose an error entity to Home Assistant.
- Continue showing live BMS data if BLE is working.
- Do not falsely mark samples as logged.

A small RAM queue may temporarily hold recent samples, but RAM is not considered durable storage.

## Google backend API

The ESP32 should post JSON batches to the camper logging endpoint. Exact endpoint and authentication will be added once the current Google Apps Script backend contract is defined.

Suggested request shape:

```json
{
  "device": "camper-controller",
  "records": [
    {
      "sequence_id": 12345,
      "timestamp": "2026-08-15T15:30:00-07:00",
      "soc_pct": 82,
      "voltage_v": 13.31,
      "current_a": 9.6,
      "power_w": 127.8
    }
  ]
}
```

Suggested response:

```json
{
  "ok": true,
  "ack_sequence_id": 12345
}
```

## Development order

1. ECO-WORTHY BLE connection and frame decoding on ESP32.
2. Show decoded values on serial console.
3. Write records to microSD.
4. Recover logging state correctly after reboot.
5. Publish live entities to Home Assistant.
6. Add Google batch synchronization.
7. Add backlog/sync status to touchscreen.
8. Re-enable automatic charge-limit control after SOC input has been proven reliable.
