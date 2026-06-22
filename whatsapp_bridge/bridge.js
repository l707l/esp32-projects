/*
 * WhatsApp ↔ ESP32 Bridge
 *
 * Ejecuta: node bridge.js
 *
 * 1) La primera vez aparece un QR en la terminal. Escanéalo con
 *    WhatsApp del teléfono (Dispositivos vinculados → Vincular dispositivo).
 * 2) El bridge queda escuchando en http://0.0.0.0:3000.
 *
 * Endpoints:
 *   GET  /messages?since=<unix_ts_seconds>   → mensajes nuevos
 *   POST /send          body: { to, text }   → enviar mensaje
 *   GET  /status                              → estado del bridge
 *   GET  /qr.png                              → QR actual como PNG (opcional)
 */

const express = require('express');
const qrcode = require('qrcode-terminal');
const QRImage = require('qrcode');
const { default: makeWASocket, useMultiFileAuthState, DisconnectReason } =
  require('@whiskeysockets/baileys');

const PORT = 3000;
const MAX_MESSAGES = 50;
let messages = []; // buffer en memoria
let sock = null;
let lastQR = null;
let connected = false;

const app = express();
app.use(express.json());

// --- WHATSAPP -----------------------------------------------------------
async function startWhatsApp() {
  const { state, saveCreds } = await useMultiFileAuthState('./auth');
  sock = makeWASocket({
    auth: state,
    printQRInTerminal: false,
    browser: ['ESP32-Acuario', 'Chrome', '1.0']
  });

  sock.ev.on('creds.update', saveCreds);

  sock.ev.on('connection.update', (update) => {
    const { connection, lastDisconnect, qr } = update;
    if (qr) {
      lastQR = qr;
      console.log('\n========================================');
      console.log('   ESCANEA ESTE QR CON WHATSAPP');
      console.log('   (Dispositivos vinculados → Vincular)');
      console.log('   PNG: http://localhost:3000/qr.png');
      console.log('========================================');
      qrcode.generate(qr, { small: true });
      // Guardar PNG para fácil escaneo
      QRImage.toFile('/home/moyao/.openclaw/workspace/esp32_projects/whatsapp_bridge/qr.png', qr, { width: 400 })
        .then(() => console.log('[bridge] QR PNG actualizado'))
        .catch(e => console.error('[bridge] Error guardando QR:', e));
    }
    if (connection === 'open') {
      connected = true;
      console.log('[wa] Conectado a WhatsApp ✓');
    }
    if (connection === 'close') {
      connected = false;
      const reason = lastDisconnect?.error?.output?.statusCode;
      console.log('[wa] Desconectado. Razón:', reason);
      if (reason !== DisconnectReason.loggedOut) {
        console.log('[wa] Reconectando...');
        setTimeout(startWhatsApp, 3000);
      } else {
        console.log('[wa] Sesión cerrada. Borra ./auth y reinicia.');
      }
    }
  });

  sock.ev.on('messages.upsert', async ({ messages: msgs, type }) => {
    if (type !== 'notify') return;
    for (const msg of msgs) {
      // Ignorar mensajes que YO envié (fromMe=true)
      if (msg.key.fromMe) continue;
      // Ignorar estados / broadcasts
      if (msg.key.remoteJid === 'status@broadcast') continue;

      const text =
        msg.message?.conversation ||
        msg.message?.extendedTextMessage?.text ||
        msg.message?.imageMessage?.caption ||
        msg.message?.videoMessage?.caption ||
        '[media]';

      const from =
        msg.pushName ||
        msg.key.remoteJid.split('@')[0];

      const entry = {
        id: msg.key.id,
        from,
        text,
        time: new Date().toISOString(),
        ts: msg.messageTimestamp
          ? Number(msg.messageTimestamp) * 1000   // a ms
          : Date.now()
      };

      messages.push(entry);
      if (messages.length > MAX_MESSAGES) {
        messages = messages.slice(-MAX_MESSAGES);
      }
      console.log(`[wa] ${from}: ${text.substring(0, 80)}`);
    }
  });
}

// --- HTTP API -----------------------------------------------------------
app.get('/status', (req, res) => {
  res.json({
    connected,
    message_count: messages.length,
    has_qr: !!lastQR,
    bridge_version: '1.0'
  });
});

app.get('/messages', (req, res) => {
  // since en milisegundos (lo que manda la ESP32)
  const since = parseInt(req.query.since) || 0;
  const filtered = messages.filter(m => m.ts > since);
  res.json({
    messages: filtered,
    server_time: Date.now(),
    connected
  });
});

app.post('/send', async (req, res) => {
  try {
    const { to, text } = req.body || {};
    if (!to || !text) return res.status(400).json({ error: 'to y text requeridos' });
    if (!connected) return res.status(503).json({ error: 'WhatsApp no conectado' });

    const jid = to.includes('@') ? to : `${to.replace(/[^0-9]/g, '')}@s.whatsapp.net`;
    await sock.sendMessage(jid, { text });
    console.log(`[wa] → ${jid}: ${text.substring(0, 80)}`);
    res.json({ ok: true, to: jid });
  } catch (e) {
    console.error('[send error]', e);
    res.status(500).json({ error: e.message });
  }
});

// QR como texto (útil para depurar desde la terminal)
app.get('/qr', (req, res) => {
  if (!lastQR) return res.status(404).json({ error: 'Aún no hay QR (¿ya está conectado?)' });
  res.type('text/plain').send(lastQR);
});

// QR como PNG (para escanear con el teléfono)
app.get('/qr.png', async (req, res) => {
  if (!lastQR) return res.status(404).json({ error: 'Aún no hay QR' });
  try {
    const buf = await QRImage.toBuffer(lastQR, { width: 400 });
    res.type('image/png').send(buf);
  } catch (e) {
    res.status(500).json({ error: e.message });
  }
});

// --- START --------------------------------------------------------------
app.listen(PORT, '0.0.0.0', () => {
  console.log(`[bridge] HTTP en http://0.0.0.0:${PORT}`);
  console.log(`[bridge] Endpoints:`);
  console.log(`           GET  /status`);
  console.log(`           GET  /messages?since=<ts_ms>`);
  console.log(`           POST /send  {to, text}`);
  startWhatsApp();
});

// Cerrar limpio
process.on('SIGINT', async () => {
  console.log('\n[bridge] Cerrando...');
  try { await sock?.end(); } catch (e) {}
  process.exit(0);
});
