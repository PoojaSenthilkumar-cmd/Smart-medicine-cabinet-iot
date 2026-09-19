import serial, csv, os, json
from datetime import datetime

PORT = "COM5"      # Windows e.g. "COM3" | Mac/Linux e.g. "/dev/ttyUSB0"
BAUD = 115200
CSV_FILE = "medicine_log.csv"
STATE_FILE = "state.json"

if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, "w", newline="") as f:
        csv.writer(f).writerow(
            ["timestamp", "name", "quantity", "minStock", "temp", "humidity"]
        )

medicines = {}                        # name -> {quantity, minStock}
env = {"temp": None, "humidity": None}


def save_state():
    # Written on every update so dashboard.py (a separate process) can
    # read current stock/temperature without waiting for a CSV row.
    tmp = STATE_FILE + ".tmp"
    with open(tmp, "w") as f:
        json.dump({
            "updated": datetime.now().isoformat(),
            "env": env,
            "medicines": [{"name": n, **v} for n, v in medicines.items()],
        }, f)
    os.replace(tmp, STATE_FILE)


ser = serial.Serial(PORT, BAUD, timeout=1)
print("Listening on", PORT, "- Ctrl+C to stop")

while True:
    try:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue

        if line.startswith("INIT,"):
            # Startup snapshot of a medicine's stock, sent once per medicine on boot.
            name, qty, minStock = line.split(",")[1:4]
            medicines[name] = {"quantity": int(qty), "minStock": int(minStock)}
            save_state()

        elif line.startswith("ENV,"):
            # Live temp/humidity, sent every couple seconds regardless of RFID activity.
            temp, humidity = line.split(",")[1:3]
            env["temp"], env["humidity"] = temp, humidity
            save_state()

        elif line.startswith("DATA,"):
            # A medicine was dispensed: name, qty, minStock, temp, humidity
            name, qty, minStock, temp, humidity = line.split(",")[1:6]
            medicines[name] = {"quantity": int(qty), "minStock": int(minStock)}
            env["temp"], env["humidity"] = temp, humidity
            with open(CSV_FILE, "a", newline="") as f:
                csv.writer(f).writerow(
                    [datetime.now().isoformat(), name, qty, minStock, temp, humidity]
                )
            save_state()
            print("Logged:", name, qty, minStock, temp, humidity)

        else:
            print(line)  # boot banner / status / debug lines from the board

    except KeyboardInterrupt:
        break
    except Exception as e:
        print("Error:", e)

ser.close()
