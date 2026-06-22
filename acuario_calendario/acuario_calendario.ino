/*
 * Calendario + Acuario + Telegram — ESP32 + GDEY042T81 (4.2" e-paper)
 * BTN1 (GPIO 25) = Calendario | BTN2 (GPIO 26) = Acuario
 *
 * Setup Telegram (5 min):
 *   1) Habla con @BotFather en Telegram, /newbot, copia el TOKEN
 *   2) Manda /start a tu bot
 *   3) Visita https://api.telegram.org/bot<TU_TOKEN>/getUpdates
 *      y copia el chat_id (en "chat":{"id": XXXXX })
 *   4) Pega ambos abajo en TG_TOKEN y TG_CHAT_ID
 *   5) Recompila y sube
 *
 * Librerías (Library Manager):
 *   - GxEPD2
 *   - ArduinoJson (v6+)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

// ==== E-PAPER ====
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ==== BOTONES ====
#define BTN_CALENDARIO  25
#define BTN_ACUARIO     26

// ==== WIFI (Biblioteca) ====
const char* SSID     = "AP-BIBLIOTECA-FOPCA";
const char* PASSWORD = "BIBLIO.2019";

// ==== TELEGRAM ====
const char* TG_TOKEN    = "PEGAR_TU_TOKEN_AQUI";
const char* TG_CHAT_ID  = "PEGAR_TU_CHAT_ID";
const unsigned long TG_POLL = 15000;  // 15s entre polls

unsigned long lastTgPoll = 0;
long lastTgUpdateId = 0;

// ==== CALENDARIO ====
const char* CALENDAR_URL = "";

// ==== DATOS DEL ACUARIO ====
struct AquariumData {
  float tempC;
  float o2mgL;
  float ph;
  int   guppys;
  int   tetras;
  bool  anubias, javaFern, vallisneria, helecho;
  bool  algasPuntoVerde, algaCepillo;
};

AquariumData acuario = {
  26.5, 7.2, 7.0,
  6, 8,
  true, true, true, false,
  true, false
};

// ==== TELEGRAM BUFFER ====
#define MAX_TG 3
struct TgMessage {
  String from;
  String text;
};
TgMessage tgBuf[MAX_TG];
int tgCount = 0;

// ==== ESTADO ====
enum View { VIEW_CALENDAR, VIEW_AQUARIUM };
View currentView = VIEW_CALENDAR;
bool viewDirty = true;

unsigned long lastBtn1 = 0, lastBtn2 = 0;
const unsigned long DEBOUNCE = 250;

// ==== HELPERS ====
void drawHeader(const char* titulo) {
  display.setFont(&FreeMonoBold12pt7b);
  display.setCursor(10, 25);
  display.print(titulo);
  display.drawFastHLine(0, 35, 400, GxEPD_BLACK);
}

void drawClock(int x, int y) {
  struct tm t;
  if (!getLocalTime(&t)) return;
  char buf[6];
  sprintf(buf, "%02d:%02d", t.tm_hour, t.tm_min);
  display.setFont(&FreeMonoBold12pt7b);
  display.setCursor(x, y);
  display.print(buf);
}

void drawFish(int cx, int cy, int len, bool facingRight) {
  int dir = facingRight ? 1 : -1;
  int bw = len * 0.7;
  int bh = len * 0.4;

  display.fillRoundRect(cx - bw/2, cy - bh/2, bw, bh, bh/2, GxEPD_BLACK);
  int tailBase = cx - dir * bw/2;
  int tailTip  = cx - dir * (bw/2 + len * 0.3);
  display.fillTriangle(tailBase, cy, tailTip, cy - bh*0.7, tailTip, cy + bh*0.7, GxEPD_BLACK);
  display.fillTriangle(
    cx - len*0.12, cy - bh/2,
    cx + len*0.12, cy - bh/2,
    cx, cy - bh*0.75,
    GxEPD_BLACK);
  display.fillCircle(cx + dir * len*0.2, cy - bh*0.18, max(2, len/15), GxEPD_WHITE);
  display.fillCircle(cx + dir * len*0.6, cy - bh*0.6, 2, GxEPD_BLACK);
  display.fillCircle(cx + dir * len*0.7, cy - bh*0.9, 1, GxEPD_BLACK);
}

void drawTelegramStrip() {
  if (tgCount == 0) return;
  int y = 263;
  display.drawFastHLine(0, y - 4, 400, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  int idx = tgCount - 1;
  String line = "TG " + tgBuf[idx].from + ": " + tgBuf[idx].text;
  if (line.length() > 50) line = line.substring(0, 47) + "...";
  display.setCursor(10, y + 8);
  display.print(line);
  if (tgCount > 1) {
    char badge[6];
    sprintf(badge, " (%d)", tgCount);
    display.setCursor(370, y + 8);
    display.print(badge);
  }
}

// ==== TELEGRAM: POLLING + ENVÍO ====
void pollTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (strlen(TG_TOKEN) < 20) return;
  unsigned long now = millis();
  if (now - lastTgPoll < TG_POLL && tgCount > 0) return;
  lastTgPoll = now;

  WiFiClientSecure client;
  client.setInsecure();  // hobby: skip cert verification

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TG_TOKEN +
               "/getUpdates?offset=" + String(lastTgUpdateId + 1) +
               "&timeout=0&limit=5";
  http.begin(client, url);
  http.setTimeout(5000);
  int code = http.GET();
  if (code == 200) {
    String body = http.getString();
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      JsonArray arr = doc["result"].as<JsonArray>();
      int nuevos = 0;
      for (JsonObject upd : arr) {
        long uid = upd["update_id"].as<long>();
        if (uid > lastTgUpdateId) lastTgUpdateId = uid;
        if (upd["message"].is<JsonObject>()) {
          JsonObject m = upd["message"];
          String from = m["from"]["first_name"].as<String>();
          if (from.isEmpty()) from = m["from"]["username"].as<String>();
          if (from.isEmpty()) from = "anon";
          String text = m["text"].as<String>();
          if (text.isEmpty()) text = "[media]";

          if (tgCount >= MAX_TG) {
            for (int i = 0; i < MAX_TG - 1; i++) tgBuf[i] = tgBuf[i+1];
            tgCount = MAX_TG - 1;
          }
          tgBuf[tgCount].from = from;
          tgBuf[tgCount].text = text;
          tgCount++;
          nuevos++;
        }
      }
      if (nuevos > 0) {
        Serial.printf("[TG] %d mensaje(s)\n", nuevos);
        viewDirty = true;
      }
    }
  }
  http.end();
}

void sendTelegram(const char* text) {
  if (WiFi.status() != WL_CONNECTED || strlen(TG_TOKEN) < 20) return;
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TG_TOKEN + "/sendMessage";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"chat_id\":\"") + TG_CHAT_ID + "\",\"text\":\"" + text + "\"}";
  http.POST(body);
  http.end();
  Serial.printf("[TG] Enviado: %s\n", text);
}

// ==== VISTA 1: CALENDARIO ====
String fetchCalendarEvents() {
  if (strlen(CALENDAR_URL) < 10) return "";
  HTTPClient http;
  http.begin(CALENDAR_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  String body = "";
  if (code == 200) body = http.getString();
  http.end();
  return body;
}

void drawCalendar() {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawHeader("Calendario");
    drawClock(310, 25);

    display.setFont(&FreeMonoBold12pt7b);
    struct tm t;
    if (getLocalTime(&t)) {
      const char* DIAS[]  = {"Domingo","Lunes","Martes","Miercoles","Jueves","Viernes","Sabado"};
      const char* MESES[] = {"Ene","Feb","Mar","Abr","May","Jun","Jul","Ago","Sep","Oct","Nov","Dic"};
      char fecha[40];
      sprintf(fecha, "%s %d %s", DIAS[t.tm_wday], t.tm_mday, MESES[t.tm_mon]);
      display.setCursor(10, 75);
      display.print(fecha);
    }

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(10, 110);
    display.print("Eventos de hoy:");

    String json = fetchCalendarEvents();
    int y = 140;
    if (json.length() > 0) {
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, json) == DeserializationError::Ok && doc.is<JsonArray>()) {
        for (JsonObject ev : doc.as<JsonArray>()) {
          if (y > 250) break;
          display.setCursor(10, y);
          display.print("- ");
          display.print(ev["time"].as<const char*>());
          display.print("  ");
          display.print(ev["title"].as<const char*>());
          y += 18;
        }
      } else {
        display.setCursor(10, y); display.print("(error JSON)");
      }
    } else {
      const char* DEMO[][2] = {
        {"10:00", "Junta equipo"},
        {"14:30", "Llamada cliente"},
        {"19:00", "Comprar comida peces"},
        {"21:00", "Cambiar agua acuario"}
      };
      for (int i = 0; i < 4; i++) {
        display.setCursor(10, y);
        display.print("- ");
        display.print(DEMO[i][0]);
        display.print("  ");
        display.print(DEMO[i][1]);
        y += 18;
      }
      display.setCursor(10, 250);
      display.print("(demo)");
    }

    drawTelegramStrip();
    display.setCursor(10, 290);
    display.print("BTN2 -> Acuario");
  } while (display.nextPage());
}

// ==== VISTA 2: ACUARIO ====
void drawAquarium() {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    drawHeader("Mi Acuario");
    drawClock(320, 25);

    display.setFont(&FreeMonoBold9pt7b);

    int x = 10, y = 60;
    display.setCursor(x, y); display.print("PECES:");
    y += 18;
    display.setCursor(x + 10, y); display.printf("Guppys: %d", acuario.guppys);
    y += 16;
    display.setCursor(x + 10, y); display.printf("Tetras: %d", acuario.tetras);
    y += 16;
    display.setCursor(x + 10, y);
    display.print("Total: "); display.print(acuario.guppys + acuario.tetras); display.print(" peces");

    y += 22;
    display.setCursor(x, y); display.print("PLANTAS:");
    y += 18;
    display.setCursor(x + 10, y); display.printf("Anubias: %s", acuario.anubias ? "OK" : "--");
    y += 16;
    display.setCursor(x + 10, y); display.printf("Java Fern: %s", acuario.javaFern ? "OK" : "--");
    y += 16;
    display.setCursor(x + 10, y); display.printf("Vallisneria: %s", acuario.vallisneria ? "OK" : "--");
    y += 16;
    display.setCursor(x + 10, y); display.printf("Helecho: %s", acuario.helecho ? "OK" : "--");

    int xr = 200;
    y = 60;
    display.setCursor(xr, y); display.print("PARAMETROS:");
    y += 22;
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(xr, y); display.printf("%.1f C", acuario.tempC);
    y += 22;
    display.setCursor(xr, y); display.printf("%.1f mg/L O2", acuario.o2mgL);
    y += 22;
    display.setCursor(xr, y); display.printf("pH %.1f", acuario.ph);

    display.setFont(&FreeMonoBold9pt7b);
    y += 18;
    display.setCursor(xr, y); display.print("ALGAS:");
    y += 18;
    display.setCursor(xr, y); display.printf("Pto verde: %s", acuario.algasPuntoVerde ? "Algo" : "OK");
    y += 16;
    display.setCursor(xr, y); display.printf("Cepillo: %s", acuario.algaCepillo ? "Si!" : "OK");

    drawFish(80, 230, 50, true);
    drawFish(150, 245, 30, false);
    drawFish(330, 220, 60, true);
    drawFish(370, 255, 28, false);

    display.fillCircle(110, 200, 2, GxEPD_BLACK);
    display.fillCircle(120, 185, 1, GxEPD_BLACK);
    display.fillCircle(360, 195, 2, GxEPD_BLACK);
    display.fillCircle(370, 180, 1, GxEPD_BLACK);

    drawTelegramStrip();
    display.setCursor(10, 290);
    display.print("BTN1 -> Calendario");
  } while (display.nextPage());
}

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  pinMode(BTN_CALENDARIO, INPUT_PULLUP);
  pinMode(BTN_ACUARIO, INPUT_PULLUP);

  SPI.begin(18, 19, 23, EPD_CS);
  display.init(115200, true, 50, false);

  WiFi.begin(SSID, PASSWORD);
  Serial.printf("WiFi -> %s ", SSID);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500); Serial.print("."); tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf(" OK (%s)\n", WiFi.localIP().toString().c_str());
    configTime(-5 * 3600, 0, "pool.ntp.org");
  } else {
    Serial.println(" FAIL (continuando sin WiFi)");
  }

  viewDirty = true;
}

// ==== LOOP ====
void loop() {
  unsigned long now = millis();

  if (digitalRead(BTN_CALENDARIO) == LOW && now - lastBtn1 > DEBOUNCE) {
    lastBtn1 = now;
    currentView = VIEW_CALENDAR;
    viewDirty = true;
    Serial.println("-> Calendario");
  }
  if (digitalRead(BTN_ACUARIO) == LOW && now - lastBtn2 > DEBOUNCE) {
    lastBtn2 = now;
    currentView = VIEW_AQUARIUM;
    viewDirty = true;
    Serial.println("-> Acuario");
  }

  // Ejemplo: enviar alerta a Telegram cuando cambia un parametro
  static bool alertSent = false;
  if (!alertSent && acuario.tempC > 28.0) {
    sendTelegram("Alerta: temperatura alta en el acuario!");
    alertSent = true;
  }

  pollTelegram();

  if (viewDirty) {
    viewDirty = false;
    if (currentView == VIEW_CALENDAR) drawCalendar();
    else                              drawAquarium();
    display.hibernate();
  }

  delay(50);
}
