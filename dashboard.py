from flask import Flask, jsonify, render_template_string
import csv, os, json

app = Flask(__name__)
CSV_FILE = "medicine_log.csv"
STATE_FILE = "state.json"

PAGE = """
<!DOCTYPE html><html><head><title>Smart Medicine Cabinet</title>
<style>
body{font-family:Arial;background:#0f172a;color:#e2e8f0;margin:0;padding:30px}
h1{text-align:center;color:#38bdf8}
.cards{display:flex;gap:20px;justify-content:center;flex-wrap:wrap;margin-bottom:30px}
.card{background:#1e293b;padding:20px;border-radius:12px;min-width:160px;text-align:center}
.card h2{margin:0;font-size:28px;color:#38bdf8}
.card p{margin:5px 0 0;color:#94a3b8}
table{width:100%;border-collapse:collapse;background:#1e293b;border-radius:8px;overflow:hidden}
th,td{padding:10px;text-align:left;border-bottom:1px solid #334155}
th{background:#334155}
.low{color:#f87171;font-weight:bold}
</style></head><body>
<h1>💊 Smart Medicine Cabinet — Live Dashboard</h1>
<div class="cards" id="env"></div>
<div class="cards" id="cards"></div>
<table><thead><tr><th>Time</th><th>Medicine</th><th>Qty</th><th>Min Stock</th><th>Temp (°C)</th><th>Humidity (%)</th></tr></thead>
<tbody id="rows"></tbody></table>
<script>
async function refresh(){
  const res = await fetch('/data');
  const d = await res.json();
  const temp = d.env.temp ?? '--', hum = d.env.humidity ?? '--';
  document.getElementById('env').innerHTML =
    `<div class="card"><h2>${temp}°C</h2><p>Temperature</p></div>
     <div class="card"><h2>${hum}%</h2><p>Humidity</p></div>`;
  document.getElementById('cards').innerHTML = d.latest.map(m =>
    `<div class="card"><h2 class="${m.quantity<=m.minStock?'low':''}">${m.quantity}</h2><p>${m.name}</p></div>`).join('');
  document.getElementById('rows').innerHTML = d.rows.slice(-15).reverse().map(r =>
    `<tr><td>${r.timestamp.split('T')[1]?.slice(0,8)||''}</td><td>${r.name}</td>
    <td class="${parseInt(r.quantity)<=parseInt(r.minStock)?'low':''}">${r.quantity}</td>
    <td>${r.minStock}</td><td>${r.temp}</td><td>${r.humidity}</td></tr>`).join('');
}
refresh(); setInterval(refresh, 2000);
</script></body></html>
"""

@app.route("/")
def home():
    return render_template_string(PAGE)

@app.route("/data")
def data():
    rows = []
    if os.path.exists(CSV_FILE):
        with open(CSV_FILE) as f:
            rows = list(csv.DictReader(f))

    state = {"env": {"temp": None, "humidity": None}, "medicines": []}
    if os.path.exists(STATE_FILE):
        with open(STATE_FILE) as f:
            try:
                state = json.load(f)
            except json.JSONDecodeError:
                pass  # serial_logger.py may be mid-write; use last known state

    return jsonify({
        "rows": rows,
        "latest": state.get("medicines", []),
        "env": state.get("env", {"temp": None, "humidity": None}),
    })

if __name__ == "__main__":
    app.run(debug=True, port=5000)
