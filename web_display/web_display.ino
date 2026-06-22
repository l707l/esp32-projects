/*
 * Web Display — ESP32 + GDEY042T81 (4.2" e-paper)
 *
 * Modo Access Point. Sin internet. Sin bridge.
 *
 * 1) Enciende la ESP32 → crea WiFi "Acuario-Display" (clave 12345678)
 * 2) Conéctate desde tu celular a esa WiFi
 * 3) Abre http://192.168.4.1 en el navegador
 * 4) Escribe texto o sube una imagen → aparece en el e-paper
 *
 * Librerías (Library Manager):
 *   - GxEPD2
 *   - ArduinoJson (v6+)
 */

#include <WiFi.h>
#include <WebServer.h>
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
const char* AP_PASS = "12345678";   // Mín 8 chars
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_GW(192, 168, 4, 1);
IPAddress AP_MASK(255, 255, 255, 0);

WebServer server(80);

// ==== BITMAP BUFFER (400x300 / 8 = 15000 bytes) ====
uint8_t bitmapBuf[15000];

// ==== HTML ====
const char INDEX_HTML[] PROGMEM = R"INDEXHTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<title>Acuario Display</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  * { box-sizing: border-box; }
  body { font-family: -apple-system, BlinkMacSystemFont, sans-serif;
         max-width: 640px; margin: 0 auto; padding: 16px; background: #f5f7fa; color: #2c3e50; }
  h1 { margin: 0 0 8px; font-size: 24px; }
  h2 { margin: 20px 0 8px; font-size: 16px; color: #34495e; }
  .status { padding: 12px; background: #fff; border-left: 4px solid #3498db;
            border-radius: 4px; margin-bottom: 16px; font-size: 14px; }
  .ok    { border-color: #27ae60; }
  .err   { border-color: #e74c3c; }
  textarea, input[type=file] { width: 100%; padding: 10px; border: 1px solid #ccd;
    border-radius: 4px; font-size: 15px; font-family: inherit; }
  textarea { min-height: 90px; resize: vertical; }
  .btns { display: flex; gap: 8px; margin-top: 12px; flex-wrap: wrap; }
  button { flex: 1; min-width: 100px; padding: 12px 16px; border: none;
    border-radius: 4px; background: #3498db; color: white; font-size: 15px;
    cursor: pointer; font-weight: 500; }
  button:hover { background: #2980b9; }
  button.sec { background: #95a5a6; }
  button.sec:hover { background: #7f8c8d; }
  .preview { background: #fff; padding: 8px; border: 1px solid #ccd; border-radius: 4px;
             margin-top: 8px; text-align: center; min-height: 60px; }
  .preview img { max-width: 100%; max-height: 200px; }
  small { color: #7f8c8d; }
</style>
</head>
<body>
  <h1>🐠 Acuario Display</h1>
  <div class="status" id="status">Listo. Escribe o sube algo y pulsa Enviar.</div>

  <h2>Texto</h2>
  <textarea id="text" placeholder="Escribe el texto a mostrar…"></textarea>
  <small>Usa saltos de línea para múltiples renglones. La primera línea sale más grande.</small>

  <h2>Imagen <small>(opcional, se convierte a B/N automáticamente)</small></h2>
  <input type="file" id="image" accept="image/*">
  <div class="preview" id="preview" style="display:none"></div>

  <div class="btns">
    <button onclick="send()">📤 Enviar a pantalla</button>
    <button class="sec" onclick="clearDisplay()">🧹 Limpiar</button>
  </div>

<script>
const statusEl = document.getElementById('status');
function setStatus(msg, cls) {
  statusEl.textContent = msg;
  statusEl.className = 'status ' + (cls || '');
}

// Preview de imagen
document.getElementById('image').onchange = (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const r = new FileReader();
  r.onload = ev => {
    document.getElementById('preview').innerHTML =
      '<img src="' + ev.target.result + '">';
    document.getElementById('preview').style.display = 'block';
  };
  r.readAsDataURL(f);
};

// Convierte imagen a 1-bit (400x300, MSB-first) y devuelve base64
function imageToBase64(file) {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = 400; c.height = 300;
      const ctx = c.getContext('2d');
      ctx.fillStyle = 'white';
      ctx.fillRect(0, 0, 400, 300);
      // Aspect-fit
      const s = Math.min(400 / img.width, 300 / img.height);
      const w = img.width * s, h = img.height * s;
      ctx.drawImage(img, (400 - w) / 2, (300 - h) / 2, w, h);
      const px = ctx.getImageData(0, 0, 400, 300).data;
      const bytesPerRow = 50;            // 400 / 8
      const bytes = new Uint8Array(bytesPerRow * 300);
      for (let y = 0; y < 300; y++) {
        for (let x = 0; x < 400; x++) {
          const i = (y * 400 + x) * 4;
          const lum = 0.299 * px[i] + 0.587 * px[i+1] + 0.114 * px[i+2];
          if (lum < 128) {               // negro
            bytes[y * bytesPerRow + (x >> 3)] |= (0x80 >> (x & 7));
          }
        }
      }
      // a base64
      let bin = '';
      for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
      resolve(btoa(bin));
    };
    img.onerror = reject;
    img.src = URL.createObjectURL(file);
  });
}

async function send() {
  const text = document.getElementById('text').value;
  const file = document.getElementById('image').files[0];
  setStatus('Enviando…');
  try {
    const body = { text };
    if (file) body.image_b64 = await imageToBase64(file);
    const r = await fetch('/api/display', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    const j = await r.json();
    setStatus(j.ok ? '✅ Mostrado en pantalla' : '❌ ' + (j.error || 'error'),
              j.ok ? 'ok' : 'err');
  } catch (e) {
    setStatus('❌ ' + e.message, 'err');
  }
}

async function clearDisplay() {
  setStatus('Limpiando…');
  const r = await fetch('/api/clear', { method: 'POST' });
  const j = await r.json();
  setStatus(j.ok ? '✅ Pantalla limpia' : '❌ error', j.ok ? 'ok' : 'err');
}
</script>
</body>
</html>
)INDEXHTML";

// ==== BASE64 DECODE (compact) ====
static const int8_t B64_TBL[128] PROGMEM = {
  // 0-42: invalid (-1)
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,62,-1,-1,-1,63,    // '+' = 62, '/' = 63
  52,53,54,55,56,57,58,59, 60,61,-1,-1,-1,-2,-1,-1,    // 0-9 = 52-61, '=' = -2 (pad)
  -1, 0, 1, 2, 3, 4, 5, 6,  7, 8, 9,10,11,12,13,14,    // A-Z = 0-25
  15,16,17,18,19,20,21,22, 23,24,25,-1,-1,-1,-1,-1,
  -1,26,27,28,29,30,31,32, 33,34,35,36,37,38,39,40,    // a-z = 26-51
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
    if (v < 0) {  // -1 = invalid, -2 = '=' pad
      if (v == -2) break;   // fin
      continue;
    }
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (out < dstMax) dst[out++] = (buf >> bits) & 0xFF;
    }
  }
  return out;
}

// ==== RENDER ====
void renderContent(const String& text, const String& imageB64) {
  display.setRotation(0);   // 400x300
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    bool hasImage = false;
    if (imageB64.length() > 0) {
      size_t n = b64_decode(imageB64.c_str(), imageB64.length(),
                            bitmapBuf, sizeof(bitmapBuf));
      if (n >= sizeof(bitmapBuf)) {
        display.drawBitmap(0, 0, bitmapBuf, 400, 300, GxEPD_BLACK);
        hasImage = true;
      }
    }

    if (text.length() > 0) {
      display.setTextColor(GxEPD_BLACK);

      // Si hay imagen, dibujamos el texto en una franja blanca arriba
      if (hasImage) {
        display.fillRect(0, 0, 400, 56, GxEPD_WHITE);
        display.setFont(&FreeMonoBold18pt7b);
        display.setCursor(10, 38);
        String t = text;
        if (t.length() > 28) t = t.substring(0, 26) + "…";
        display.print(t);
      } else {
        // Texto solo, en el centro
        int y = 60;
        display.setFont(&FreeMonoBold24pt7b);
        display.setCursor(20, y);
        display.print(text);
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
  String body = server.arg("plain");
  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON invalido\"}");
    return;
  }
  String text = doc["text"] | "";
  String img  = doc["image_b64"] | "";
  Serial.printf("API display: text=%d chars, img=%d chars\n", text.length(), img.length());
  renderContent(text, img);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiClear() {
  display.setFullWindow();
  display.firstPage();
  do { display.fillScreen(GxEPD_WHITE); } while (display.nextPage());
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiStatus() {
  String s = "{\"ap\":\"" + String(AP_SSID) + "\",\"ip\":\"" +
             WiFi.softAPIP().toString() + "\",\"clients\":" +
             String(WiFi.softAPgetStationNum()) + "}";
  server.send(200, "application/json", s);
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);

  SPI.begin(18, 19, 23, EPD_CS);
  display.init(115200, true, 50, false);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold18pt7b);
    display.setCursor(20, 80);
    display.print("Acuario Display");
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(20, 130);
    display.print("WiFi: "); display.print(AP_SSID);
    display.setCursor(20, 160);
    display.print("Clave: "); display.print(AP_PASS);
    display.setCursor(20, 210);
    display.print("http://192.168.4.1");
  } while (display.nextPage());

  // AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP '%s' listo, IP %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/display", HTTP_POST, handleApiDisplay);
  server.on("/api/clear",   HTTP_POST, handleApiClear);
  server.on("/api/status",  HTTP_GET,  handleApiStatus);
  server.begin();
}

void loop() {
  server.handleClient();
  delay(2);
}
