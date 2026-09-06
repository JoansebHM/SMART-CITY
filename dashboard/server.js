/**
 * Puente Serial <-> WebSocket + servidor del dashboard.
 *
 *   ESP32-S3 --USB--> [este proceso] --WebSocket--> navegador
 *                          |
 *                          +--> SQLite (datos/ciudad.db)
 *
 * Uso:
 *   npm start                        conecta con la placa (autodetecta el puerto)
 *   npm start -- --puerto /dev/cu.X  fuerza un puerto
 *   npm run simular                  sin hardware, con placa simulada
 *   npm run puertos                  lista los puertos disponibles
 */

import express from 'express';
import { WebSocketServer } from 'ws';
import { createServer } from 'node:http';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { Almacen } from './lib/db.js';
import { EnlaceSerial } from './lib/enlaceSerial.js';
import { PlacaSimulada } from './lib/simulador.js';
import { Agente } from './lib/ia/agente.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// ---------------------------------------------------------------------------
// Opciones de linea de comandos
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
const opcion = (nombre) => {
  const i = args.indexOf(nombre);
  return i >= 0 ? args[i + 1] : null;
};

const CONFIG = {
  puerto: opcion('--puerto'),
  http: parseInt(opcion('--http') ?? '3000', 10),
  simular: args.includes('--simular'),
  // Cada cuanto pide la placa una muestra (ms). El dashboard recibe todas;
  // a la base solo se guarda una cada PERIODO_GUARDADO ms.
  periodoTelemetria: parseInt(opcion('--periodo') ?? '200', 10),
  periodoGuardado: parseInt(opcion('--guardar-cada') ?? '500', 10)
};

if (args.includes('--listar-puertos')) {
  const puertos = await EnlaceSerial.listarPuertos();
  if (puertos.length === 0) console.log('No hay puertos seriales disponibles.');
  for (const p of puertos) {
    console.log(`${p.path}  ${p.manufacturer ?? '(sin fabricante)'}`);
  }
  process.exit(0);
}

// ---------------------------------------------------------------------------
// Persistencia
// ---------------------------------------------------------------------------
const almacen = new Almacen(join(__dirname, 'datos', 'ciudad.db'));
console.log(`Base de datos lista. Sesion #${almacen.sesionId}`);

// ---------------------------------------------------------------------------
// Enlace con la placa (real o simulada)
// ---------------------------------------------------------------------------
const placa = CONFIG.simular
  ? new PlacaSimulada({ periodoTelemetria: CONFIG.periodoTelemetria })
  : new EnlaceSerial({ ruta: CONFIG.puerto, periodoTelemetria: CONFIG.periodoTelemetria });

let ultimaTelemetria = null;
let estadoEnlace = { conectado: false, mensaje: 'Iniciando...' };
let ultimoGuardado = 0;

placa.on('telemetria', (t) => {
  ultimaTelemetria = t;
  difundir({ ...t, tipo: 'tel' });

  // Guardar a ritmo reducido para no inflar la base innecesariamente.
  const ahora = Date.now();
  if (ahora - ultimoGuardado >= CONFIG.periodoGuardado) {
    ultimoGuardado = ahora;
    try {
      almacen.guardarTelemetria(t);
    } catch (e) {
      console.error('Error guardando telemetria:', e.message);
    }
  }
});

placa.on('consola', ({ nivel, texto }) => {
  almacen.guardarEvento(nivel, texto);
  difundir({ tipo: 'consola', nivel, texto, ts: Date.now() });
});

placa.on('estado', (estado) => {
  estadoEnlace = estado;
  console.log(`[enlace] ${estado.mensaje}`);
  almacen.guardarEvento('info', estado.mensaje);
  difundir({ tipo: 'enlace', ...estado });
});

// OJO: placa.conectar() se llama mas abajo, despues de crear el WebSocket
// server. La placa simulada emite eventos de forma sincrona y difundir()
// necesita que `wss` ya exista.

// ---------------------------------------------------------------------------
// HTTP + API REST (para historial y exportacion)
// ---------------------------------------------------------------------------
const app = express();
app.use(express.json());
app.use(express.static(join(__dirname, 'public')));

app.get('/api/estado', (_req, res) => {
  res.json({ enlace: estadoEnlace, telemetria: ultimaTelemetria, sesion: almacen.sesionId });
});

app.get('/api/historial', (req, res) => {
  const limite = Math.min(parseInt(req.query.limite ?? '300', 10), 5000);
  const sesion = req.query.sesion ? parseInt(req.query.sesion, 10) : null;
  res.json(almacen.historial({ limite, sesion }));
});

app.get('/api/eventos', (req, res) => {
  res.json(almacen.eventos({ limite: Math.min(parseInt(req.query.limite ?? '200', 10), 2000) }));
});

app.get('/api/comandos', (req, res) => {
  res.json(almacen.comandos({ limite: Math.min(parseInt(req.query.limite ?? '200', 10), 2000) }));
});

app.get('/api/sesiones', (_req, res) => res.json(almacen.sesiones()));
app.get('/api/conversaciones', (req, res) => {
  res.json(almacen.conversaciones({ limite: Math.min(parseInt(req.query.limite ?? '50', 10), 500) }));
});
app.get('/api/resumen', (_req, res) => res.json(almacen.resumenSesion()));

app.get('/api/exportar.csv', (req, res) => {
  const sesion = req.query.sesion ? parseInt(req.query.sesion, 10) : null;
  res.type('text/csv');
  res.attachment(`ciudad-sesion-${sesion ?? almacen.sesionId}.csv`);
  res.send(almacen.csvTelemetria(sesion));
});

// Enviar un comando tambien por HTTP (util para pruebas con curl)
app.post('/api/comando', (req, res) => {
  const texto = String(req.body?.comando ?? '').trim();
  if (!texto) return res.status(400).json({ error: 'Falta el campo "comando"' });
  ejecutarComando(texto, 'http');
  res.json({ ok: true, comando: texto });
});

const servidor = createServer(app);

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
const wss = new WebSocketServer({ server: servidor });

function difundir(mensaje) {
  const texto = JSON.stringify(mensaje);
  for (const cliente of wss.clients) {
    if (cliente.readyState === 1) cliente.send(texto);
  }
}

function ejecutarComando(texto, origen) {
  almacen.guardarComando(texto, origen);
  const enviado = placa.enviar(texto);
  if (!enviado) {
    const aviso = 'No hay placa conectada: el comando no se envio.';
    almacen.guardarEvento('error', aviso);
    difundir({ tipo: 'consola', nivel: 'error', texto: `[ERROR] ${aviso}`, ts: Date.now() });
  }
  return enviado;
}

// ---------------------------------------------------------------------------
// Capa de IA: lenguaje natural -> comandos
// ---------------------------------------------------------------------------
const agente = new Agente();

/** Estado de la IA + consumo de cuota, para pintarlo en el panel. */
function estadoIa() {
  const c = agente.cliente;
  return {
    disponible: agente.disponible,
    modelo: c.modelo,
    uso: c.limitador?.uso() ?? null
  };
}

async function atenderMensajeIa(mensaje) {
  const texto = String(mensaje ?? '').trim();
  if (!texto) return;

  difundir({ tipo: 'ia', rol: 'usuario', texto, ts: Date.now() });

  if (!agente.disponible) {
    const aviso = 'Falta GEMINI_API_KEY. Copia .env.ejemplo a .env y pon tu key para usar el chat.';
    difundir({ tipo: 'ia', rol: 'error', texto: aviso, ts: Date.now() });
    return;
  }

  difundir({ tipo: 'ia', rol: 'pensando', ts: Date.now() });

  let resultado;
  const inicioIa = Date.now();
  try {
    resultado = await agente.interpretar(texto, ultimaTelemetria);
  } catch (e) {
    console.error('[ia]', e.message);
    difundir({ tipo: 'ia', rol: 'error', texto: e.message, ts: Date.now() });
    difundir({ tipo: 'iaEstado', ...estadoIa() });
    // Se guarda el tiempo tambien cuando falla: sin el no hay forma de saber
    // despues si fue un timeout, un limite de frecuencia o una caida inmediata.
    almacen.guardarConversacion(texto, '', [], e.message, Date.now() - inicioIa);
    return;
  }

  // Los comandos validados salen por serial, en orden.
  for (const comando of resultado.comandos) {
    ejecutarComando(comando, 'ia');
  }

  difundir({ tipo: 'iaEstado', ...estadoIa() });
  difundir({
    tipo: 'ia',
    rol: 'asistente',
    texto: resultado.respuesta,
    comandos: resultado.comandos,
    rechazados: resultado.rechazados,
    diagnostico: resultado.diagnostico,
    ts: Date.now()
  });

  almacen.guardarConversacion(
    texto,
    resultado.respuesta,
    resultado.comandos,
    resultado.rechazados.map((r) => `${r.comando}: ${r.motivo}`).join(' | '),
    resultado.diagnostico?.ms ?? 0
  );
}

wss.on('connection', (ws) => {
  // Al conectarse, el cliente recibe todo lo necesario para pintarse completo.
  ws.send(JSON.stringify({ tipo: 'enlace', ...estadoEnlace }));
  ws.send(JSON.stringify({ tipo: 'iaEstado', ...estadoIa() }));
  if (ultimaTelemetria) ws.send(JSON.stringify({ ...ultimaTelemetria, tipo: 'tel' }));
  ws.send(JSON.stringify({ tipo: 'historial', datos: almacen.historial({ limite: 120 }) }));
  ws.send(JSON.stringify({ tipo: 'eventosPrevios', datos: almacen.eventos({ limite: 40 }) }));

  ws.on('message', (crudo) => {
    let mensaje;
    try {
      mensaje = JSON.parse(crudo.toString());
    } catch {
      return;
    }
    if (mensaje.tipo === 'cmd' && typeof mensaje.texto === 'string') {
      ejecutarComando(mensaje.texto.trim(), 'dashboard');
    }
    if (mensaje.tipo === 'ia' && typeof mensaje.texto === 'string') {
      atenderMensajeIa(mensaje.texto);
    }
    if (mensaje.tipo === 'iaOlvidar') {
      agente.olvidar();
      difundir({ tipo: 'ia', rol: 'sistema', texto: 'Memoria de la conversacion borrada.', ts: Date.now() });
    }
  });
});

// ---------------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------------
placa.conectar();

servidor.listen(CONFIG.http, () => {
  console.log('');
  console.log('  Ciudad Autoadaptable — dashboard');
  console.log(`  http://localhost:${CONFIG.http}`);
  console.log(`  Modo: ${CONFIG.simular ? 'SIMULADO (sin hardware)' : 'placa real por USB'}`);
  console.log('');
});

function apagar() {
  console.log('\nCerrando...');
  placa.cerrar();
  almacen.cerrar();
  servidor.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 1000);
}

process.on('SIGINT', apagar);
process.on('SIGTERM', apagar);
