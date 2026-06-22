# 🐠 Acuario Display — ESP32 + e-paper 4.2"

Proyectos para ESP32 con pantalla **GDEY042T81** (e-paper 4.2" B/N, 400×300 px).

## Versiones

| Ver | Carpeta | Descripción |
|-----|---------|-------------|
| v1  | `acuario_calendario/` | Sketch inicial: 2 vistas (calendario Google + acuario), 2 botones físicos, polling WhatsApp vía bridge local |
| v2  | `web_display/`        | Sketch con servidor web embebido (modo Access Point). **Esta es la versión actual.** |

## ✅ v2 — qué tiene

- ✏️ **Texto** con word wrap (multi-línea, párrafos completos)
- 🖼️ **Imagen** que se convierte a B/N automáticamente desde el navegador
- 🔀 **Texto + imagen** juntos
- 🎨 **3 tamaños** de fuente (12 / 18 / 24 pt)
- ⬛ **Modo invertido** (blanco sobre negro)
- 🧹 **Limpiar pantalla**
- ⬆️ **OTA updates** — sube nuevo firmware `.bin` desde la web, sin USB
- ℹ️ **Endpoint `/api/status`** — versión, uptime, heap libre, clientes conectados

## 🔌 Conexiones

```
ESP32                     E-paper 4.2" GDEY042T81
─────                     ─────────────────────────
GPIO 5  (CS)   ─────────► CS
GPIO 17 (DC)   ─────────► DC
GPIO 16 (RST)  ─────────► RST
GPIO 4  (BUSY)  ─────────► BUSY
3.3V                    ──► VCC
GND                     ──► GND
```

```
WiFi SPI (compartida)
MOSI = GPIO 23
SCK  = GPIO 18
MISO = GPIO 19   (no se usa, pero se inicializa)
```

## 🚀 Cómo usar v2

1. **Flasha** el sketch (`arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyACM0 .`)
2. La pantalla muestra las instrucciones (SSID + IP)
3. **Conéctate** desde tu celular/laptop a la WiFi:
   ```
   SSID:     Acuario-Display
   Clave:    12345678
   ```
4. Abre en el navegador: **`http://192.168.4.1`**
5. Escribe o sube una imagen → pulsa **Enviar**

## ⬆️ OTA — actualizar sin USB

1. Compila el firmware nuevo:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 web_display/
   ```
2. El `.bin` queda en `~/.cache/arduino/sketches/<HASH>/web_display.ino.merged.bin`
3. Conéctate a `Acuario-Display` y abre `http://192.168.4.1`
4. Abre el panel **📡 Actualizar firmware (OTA)**
5. Selecciona el `.bin` → **Subir firmware**
6. La barra muestra el progreso → al terminar, el ESP32 se reinicia solo con el firmware nuevo

## 📦 Otros proyectos de la carpeta

### `acuario_calendario/` (v1, legacy)
Sketch con dos vistas (calendario Google + acuario) cambiadas con dos botones físicos en GPIO 25 y 26. Tiene integración con un bridge de WhatsApp escrito en Node.js.

### `whatsapp_bridge/` (bridge para v1)
Servidor Node.js que usa Baileys para conectar a WhatsApp Web y exponer una API HTTP:
- `GET  /messages?since=<ts>` → mensajes nuevos
- `POST /send {to, text}`       → enviar mensaje
- `GET  /status`                → estado

⚠️ **Nota**: el bridge de WhatsApp solo funciona si la ESP32 y el bridge están en la **misma red WiFi** (no en WiFi públicas / AP-aisladas). Para entornos públicos es mejor usar la opción OTA + web_display.

## 🛠️ Compilar todo

```bash
# v1 (calendario + acuario)
arduino-cli compile --fqbn esp32:esp32:esp32 acuario_calendario/

# v2 (web display, la versión actual)
arduino-cli compile --fqbn esp32:esp32:esp32 web_display/

# Flashear
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyACM0 web_display/

# Bridge WhatsApp
cd whatsapp_bridge && npm install && node bridge.js
```

## 📚 Librerías usadas

- **GxEPD2** — driver e-paper
- **ArduinoJson** — parseo JSON
- **WiFi + WebServer** — servidor web embebido
- **Update.h** — OTA desde HTTP

## 🐟 Hardware

- ESP32-D0WD-V3 (240 MHz, dual core, WiFi+BT)
- e-paper GDEY042T81 (4.2", 400×300, B/N, parcial refresh)
- Conexión SPI estándar