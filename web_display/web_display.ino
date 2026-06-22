/*
 * Web Display v2 — ESP32 + GDEY042T81 (4.2" e-paper)
 *
 * Novedades v2:
 *   ✓ OTA: sube nuevo firmware desde el navegador
 *   ✓ Texto multi-línea con word wrap
 *   ✓ Modo invertido (blanco sobre negro)
 *   ✓ Endpoint /api/status con info del dispositivo
 *
 * Uso:
 *   1) Conéctate a WiFi "Acuario-Display" / clave 12345678
 *   2) Abre http://192.168.4.1
 *
 * Librerías: GxEPD2, ArduinoJson
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

// ==== E-PAPER ====
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ==== ACCESS POINT ====
const char* AP_SSID = "Acuario-Display";
const char* AP_PASS = "12345678";
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GW(192, 168, 4, 1);
IPAddress AP_MASK(255, 255, 255, 0);

WebServer server(80);

// ==== BITMAP BUFFER (400x300 / 8 = 15000 bytes) ====
uint8_t bitmapBuf[15000];

// ==== DEVICE INFO ====
const char* FW_VERSION = "v2.0";
unsigned long bootTime = 0;

// ==== HTML ====
const char INDEX_HTML[] PROGMEM = R"INDEXHTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<title>Acuario Display v2</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, sans-serif;
         max-width: 700px; margin: 0 auto; padding: 16px; background: #f5f7fa; color: #2c3e50; }
  h1 { margin: 0 0 4px; font-size: 22px; }
  .sub { color: #7f8c8d; font-size: 13px; margin-bottom: 16px; }
  .status { padding: 12px; background: #fff; border-left: 4px solid #3498db;
            border-radius: 4px; margin: 12px 0; font-size: 14px; }
  .ok    { border-color: #27ae60; }
  .err   { border-color: #e74c3c; }
  .panel { background: #fff; padding: 14px; border-radius: 6px; margin: 12px 0;
           box-shadow: 0 1px 3px rgba(0,0,0,.06); }
  h2 { margin: 0 0 8px; font-size: 15px; color: #34495e; }
  textarea, input[type=file], input[type=text] { width: 100%; padding: 10px; border: 1px solid #ccd;
    border-radius: 4px; font-size: 14px; font-family: inherit; margin-bottom: 8px; }
  textarea { min-height: 80px; resize: vertical; }
  .row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  .opt { display: flex; align-items: center; gap: 6px; font-size: 14px; }
  .opt input { margin: 0; }
  .btns { display: flex; gap: 8px; margin-top: 8px; flex-wrap: wrap; }
  button { padding: 11px 14px; border: none; border-radius: 4px; background: #3498db;
           color: white; font-size: 14px; cursor: pointer; font-weight: 500; }
  button:hover { background: #2980b9; }
  button.sec { background: #95a5a6; } button.sec:hover { background: #7f8c8d; }
  button.danger { background: #e74c3c; } button.danger:hover { background: #c0392b; }
  .preview { background: #fff; padding: 6px; border: 1px solid #ccd; border-radius: 4px;
             margin-top: 6px; text-align: center; min-height: 50px; }
  .preview img { max-width: 100%; max-height: 180px; }
  small { color: #7f8c8d; font-size: 12px; }
  .info-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; font-size: 13px; }
  .info-grid div { padding: 6px 8px; background: #f8f9fa; border-radius: 3px; }
  .info-grid b { color: #2c3e50; }
  details { margin-top: 8px; }
  details summary { cursor: pointer; color: #7f8c8d; font-size: 13px; padding: 4px 0; }
  code { background: #ecf0f1; padding: 1px 5px; border-radius: 3px; font-size: 13px; }
</style>
</head>
<body>
  <h1>🐠 Acuario Display <span id="ver" style="font-size:13px;color:#95a5a6"></span></h1>
  <div class="sub">Conectado a <code>Acuario-Display</code> · http://192.168.4.1</div>

  <div class="status" id="status">Listo.</div>

  <div class="panel">
    <h2>📝 Texto</h2>
    <textarea id="text" placeholder="Escribe el texto. Usa saltos de línea para párrafos."></textarea>
    <div class="row">
      <label class="opt"><input type="radio" name="fsize" value="12" checked> Chico (12pt)</label>
      <label class="opt"><input type="radio" name="fsize" value="18"> Mediano (18pt)</label>
      <label class="opt"><input type="radio" name="fsize" value="24"> Grande (24pt)</label>
      <label class="opt" style="margin-left:auto">
        <input type="checkbox" id="invert"> Invertir (negro)
      </label>
    </div>
  </div>

  <div class="panel">
    <h2>🖼️ Imagen <small>(opcional)</small></h2>
    <input type="file" id="image" accept="image/*">
    <div class="preview" id="preview" style="display:none"></div>
  </div>

  <div class="btns">
    <button onclick="send()">📤 Enviar a pantalla</button>
    <button class="sec" onclick="clearDisplay()">🧹 Limpiar</button>
    <button class="sec" onclick="showStatus()">ℹ️ Info</button>
  </div>

  <details>
    <summary>📡 Actualizar firmware (OTA)</summary>
    <div class="panel" style="margin-top:8px">
      <p style="margin:0 0 8px;font-size:13px">Sube un archivo <code>.bin</code> compilado con <code>arduino-cli</code>:</p>
      <input type="file" id="firmware" accept=".bin">
      <div id="ota-progress" style="display:none;margin:8px 0">
        <div style="background:#ecf0f1;height:18px;border-radius:3px;overflow:hidden">
          <div id="ota-bar" style="background:#27ae60;height:100%;width:0%;transition:width .2s"></div>
        </div>
        <small id="ota-text">Subiendo…</small>
      </div>
      <button onclick="uploadFirmware()">⬆️ Subir firmware</button>
    </div>
  </details>

<script>
const $ = (id) => document.getElementById(id);
const statusEl = $('status');
function setStatus(msg, cls) { statusEl.textContent = msg; statusEl.className = 'status ' + (cls || ''); }

// Preview imagen
$('image').onchange = (e) => {
  const f = e.target.files[0]; if (!f) return;
  const r = new FileReader();
  r.onload = ev => {
    $('preview').innerHTML = '<img src="' + ev.target.result + '">';
    $('preview').style.display = 'block';
  };
  r.readAsDataURL(f);
};

// Convierte imagen a 1-bit B/N y devuelve base64
function imageToBase64(file) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = 400; c.height = 300;
      const ctx = c.getContext('2d');
      ctx.fillStyle = 'white';
      ctx.fillRect(0, 0, 400, 300);
      const s = Math.min(400 / img.width, 300 / img.height);
      const w = img.width * s, h = img.height * s;
      ctx.drawImage(img, (400 - w) / 2, (300 - h) / 2, w, h);
      const px = ctx.getImageData(0, 0, 400, 300).data;
      const bpr = 50;
      const bytes = new Uint8Array(bpr * 300);
      for (let y = 0; y < 300; y++) {
        for (let x = 0; x < 400; x++) {
          const i = (y * 400 + x) * 4;
          const lum = 0.299 * px[i] + 0.587 * px[i+1] + 0.114 * px[i+2];
          if (lum < 128) bytes[y * bpr + (x >> 3)] |= (0x80 >> (x & 7));
        }
      }
      let bin = '';
      for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      resolve(btoa(bin));
    };
    img.onerror = reject;
    img.src = URL.createObjectURL(file);
  });
}

async function send() {
  const text = $('text').value;
  const file = $('image').files[0];
  const fsize = document.querySelector('input[name=fsize]:checked').value;
  const invert = $('invert').checked;
  setStatus('Enviando…');
  try {
    const body = { text, font_size: parseInt(fsize), invert };
    if (file) body.image_b64 = await imageToBase64(file);
    const r = await fetch('/api/display', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const j = await r.json();
    setStatus(j.ok ? '✅ Mostrado en pantalla' : '❌ ' + (j.error || 'error'),
              j.ok ? 'ok' : 'err');
  } catch (e) { setStatus('❌ ' + e.message, 'err'); }
}

async function clearDisplay() {
  setStatus('Limpiando…');
  const r = await fetch('/api/clear', { method: 'POST' });
  const j = await r.json();
  setStatus(j.ok ? '✅ Pantalla limpia' : '❌ error', j.ok ? 'ok' : 'err');
}

async function showStatus() {
  const r = await fetch('/api/status');
  const j = await r.json();
  const html = `
    <div class="info-grid">
      <div><b>FW:</b> ${j.fw}</div>
      <div><b>Uptime:</b> ${j.uptime_s}s</div>
      <div><b>Heap libre:</b> ${j.free_heap} B</div>
      <div><b>Clientes:</b> ${j.clients}</div>
    </div>`;
  statusEl.innerHTML = html;
  statusEl.className = 'status';
}

// OTA Upload
async function uploadFirmware() {
  const file = $('firmware').files[0];
  if (!file) { setStatus('Selecciona un .bin primero', 'err'); return; }
  const prog = $('ota-progress');
  const bar  = $('ota-bar');
  const txt  = $('ota-text');
  prog.style.display = 'block';
  setStatus('Subiendo firmware…');
  try {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update');
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const pct = Math.round(100 * e.loaded / e.total);
        bar.style.width = pct + '%';
        txt.textContent = 'Subiendo… ' + pct + '%';
      }
    };
    xhr.onload = () => {
      if (xhr.status === 200) {
        setStatus('✅ Firmware actualizado. Reiniciando en 3s…', 'ok');
        setTimeout(() => location.reload(), 5000);
      } else {
        setStatus('❌ Error OTA: ' + xhr.responseText, 'err');
      }
    };
    xhr.onerror = () => setStatus('❌ Error de red', 'err');
    xhr.send(file);
  } catch (e) { setStatus('❌ ' + e.message, 'err'); }
}

// Muestra versión al cargar
fetch('/api/status').then(r => r.json()).then(j => $('ver').textContent = j.fw);
</script>
</body>
</html>
)INDEXHTML";

// ==== BASE64 DECODE ====
static const int8_t B64_TBL[128] PROGMEM = {
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,62,-1,-1,-1,63,
  52,53,54,55,56,57,58,59, 60,61,-1,-1,-1,-2,-1,-1,
  -1, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14,
  15,16,17,18,19,20,21,22, 23,24,25,-1,-1,-1,-1,-1,
  -1,26,27,28,29,30,31,32, 33,34,35,36,37,38,39,40,
  41,42,43,44,45,46,47,48, 49,50,51,-1,-1,-1,-1,-1
};

size_t b64_decode(const char* src, size_t len, uint8_t* dst, size_t dstMax) {
  size_t out = 0;
  uint32_t buf = 0;
  int bits = 0;
  for (size_t i = 0; i < len; i++) {
    char c = src[i];
    if (c < 0 || c >= 128) continue;
    int8_t v = pgm_read_byte(&B64_TBL[(uint8_t)c]);
    if (v < 0) { if (v == -2) break; continue; }
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out < dstMax) dst[out++] = (buf >> bits) & 0xFF;
    }
  }
  return out;
}

// ==== HELPERS DE FUENTE ====
const GFXfont* fontFromSize(int s) {
  if (s >= 24) return &FreeMonoBold24pt7b;
  if (s >= 18) return &FreeMonoBold18pt7b;
  return &FreeMonoBold12pt7b;
}

// ==== RENDER CON WORD WRAP ====
void renderTextWrapped(int x, int y, int maxW, int maxH,
                       const String& text, const GFXfont* font,
                       uint16_t fg, uint16_t bg) {
  display.setFont(font);
  display.setTextColor(fg);
  int lineH = font->yAdvance;
  int cursorY = y;
  String currentLine = "";
  int i = 0;
  int len = text.length();

  while (i < len && cursorY < y + maxH) {
    if (text[i] == '\n') {
      display.setCursor(x, cursorY);
      display.print(currentLine);
      currentLine = "";
      cursorY += lineH;
      i++;
      continue;
    }

    int wordEnd = i;
    while (wordEnd < len && text[wordEnd] != ' ' && text[wordEnd] != '\n') wordEnd++;
    String word = text.substring(i, wordEnd);
    String test = currentLine.length() > 0 ? currentLine + " " + word : word;

    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(test, 0, 0, &bx, &by, &bw, &bh);
    if (bw > maxW && currentLine.length() > 0) {
      display.setCursor(x, cursorY);
      display.print(currentLine);
      currentLine = word;
      cursorY += lineH;
    } else {
      currentLine = test;
    }
    i = wordEnd;
    if (i < len && text[i] == ' ') i++;
  }
  if (currentLine.length() > 0 && cursorY < y + maxH) {
    display.setCursor(x, cursorY);
    display.print(currentLine);
  }
}

// ==== RENDER ====
void renderContent(const String& text, const String& imageB64,
                   int fontSize, bool invert) {
  uint16_t bg = invert ? GxEPD_BLACK : GxEPD_WHITE;
  uint16_t fg = invert ? GxEPD_WHITE : GxEPD_BLACK;

  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(bg);

    bool hasImage = false;
    if (imageB64.length() > 0) {
      size_t n = b64_decode(imageB64.c_str(), imageB64.length(),
                            bitmapBuf, sizeof(bitmapBuf));
      if (n >= sizeof(bitmapBuf)) {
        // Si invert, invertimos el bitmap
        if (invert) {
          for (size_t i = 0; i < sizeof(bitmapBuf); i++) bitmapBuf[i] = ~bitmapBuf[i];
        }
        display.drawBitmap(0, 0, bitmapBuf, 400, 300, GxEPD_BLACK);
        hasImage = true;
      }
    }

    if (text.length() > 0) {
      if (hasImage) {
        // Texto en franja blanca (o negra si invert) arriba
        display.fillRect(0, 0, 400, 56, bg);
        const GFXfont* f = &FreeMonoBold18pt7b;
        display.setFont(f);
        display.setTextColor(fg);
        display.setCursor(10, 38);
        String t = text;
        if (t.length() > 32) t = t.substring(0, 30) + "…";
        display.print(t);
      } else {
        // Texto multi-línea con word wrap
        const GFXfont* f = fontFromSize(fontSize);
        renderTextWrapped(15, 60, 370, 220, text, f, fg, bg);
      }
    }
  } while (display.nextPage());
}

// ==== HANDLERS ====
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiDisplay() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"sin body\"}");
    return;
  }
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON invalido\"}");
    return;
  }
  String text = doc["text"] | "";
  String img  = doc["image_b64"] | "";
  int    fs   = doc["font_size"] | 12;
  bool   inv  = doc["invert"] | false;
  Serial.printf("display: text=%d img=%d fs=%d inv=%d\n",
                text.length(), img.length(), fs, inv);
  renderContent(text, img, fs, inv);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiClear() {
  display.setFullWindow();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiStatus() {
  unsigned long up = (millis() - bootTime) / 1000;
  String s = "{\"fw\":\"" + String(FW_VERSION) +
             "\",\"uptime_s\":" + String(up) +
             ",\"free_heap\":" + String(ESP.getFreeHeap()) +
             ",\"clients\":" + String(WiFi.softAPgetStationNum()) +
             ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"}";
  server.send(200, "application/json", s);
}

// ==== OTA UPDATE ====
void handleUpdatePage() {
  const char* p = R"OTAHTML(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>OTA</title>
<style>body{font-family:sans-serif;max-width:500px;margin:40px auto;padding:20px}
button{padding:10px 20px;background:#3498db;color:white;border:none;border-radius:4px;cursor:pointer}
.progress{background:#ecf0f1;height:20px;border-radius:3px;margin:10px 0;overflow:hidden}
.bar{background:#27ae60;height:100%;width:0%;transition:width .2s}
</style></head><body>
<h2>OTA Update</h2>
<input type="file" id="f" accept=".bin"><br><br>
<button onclick="upload()">Subir</button>
<div class="progress" id="prog" style="display:none"><div class="bar" id="bar"></div></div>
<p id="msg"></p>
<script>
async function upload(){
  const f=document.getElementById('f').files[0]; if(!f){alert('Elige .bin');return}
  const prog=document.getElementById('prog'),bar=document.getElementById('bar'),msg=document.getElementById('msg');
  prog.style.display='block'; msg.textContent='Subiendo...';
  const x=new XMLHttpRequest(); x.open('POST','/update');
  x.upload.onprogress=e=>{if(e.lengthComputable){bar.style.width=Math.round(100*e.loaded/e.total)+'%'}};
  x.onload=()=>{if(x.status===200){msg.textContent='OK. Reiniciando...';setTimeout(()=>location='/',5000)}else msg.textContent='FAIL: '+x.responseText};
  x.onerror=()=>msg.textContent='Error de red';
  x.send(f);
}
</script></body></html>)OTAHTML";
  server.send(200, "text/html", p);
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("OTA start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("OTA OK: %u bytes\n", up.totalSize);
    } else Update.printError(Serial);
  }
}

void handleUpdateResult() {
  if (Update.hasError()) {
    server.send(500, "text/plain", "FAIL");
  } else {
    server.send(200, "text/plain", "OK - Reiniciando...");
    delay(500);
    ESP.restart();
  }
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  bootTime = millis();

  SPI.begin(18, 19, 23, EPD_CS);
  display.init(115200, true, 50, false);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold24pt7b);
    display.setCursor(40, 80);
    display.print("Acuario v2");
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(40, 130);
    display.print("WiFi: "); display.print(AP_SSID);
    display.setCursor(40, 160);
    display.print("Clave: "); display.print(AP_PASS);
    display.setCursor(40, 210);
    display.print("http://192.168.4.1");
    display.setCursor(40, 240);
    display.print("OTA listo - sube .bin desde web");
  } while (display.nextPage());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP '%s' listo, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/display", HTTP_POST, handleApiDisplay);
  server.on("/api/clear",   HTTP_POST, handleApiClear);
  server.on("/api/status",  HTTP_GET,  handleApiStatus);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateResult, handleUpdateUpload);
  server.begin();
}

void loop() {
  server.handleClient();
  delay(2);
}
