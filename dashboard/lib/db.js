/**
 * Persistencia en SQLite.
 *
 * Se usa `node:sqlite`, que viene incluido en Node 22.5+. Eso evita compilar
 * modulos nativos: la base es un solo archivo (datos/ciudad.db) que se puede
 * copiar, respaldar o abrir con cualquier visor de SQLite.
 *
 * Se guardan tres cosas:
 *   - telemetria : una fila por muestra del estado de la maqueta
 *   - eventos    : las lineas de texto que emite el ESP32 ([OK], [EVENTO]...)
 *   - comandos   : todo comando enviado, con su origen
 */

import { DatabaseSync } from 'node:sqlite';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

export class Almacen {
  constructor(rutaArchivo) {
    mkdirSync(dirname(rutaArchivo), { recursive: true });
    this.db = new DatabaseSync(rutaArchivo);

    // WAL: lecturas rapidas mientras se sigue escribiendo telemetria.
    this.db.exec('PRAGMA journal_mode = WAL');
    this.db.exec('PRAGMA synchronous = NORMAL');

    this.#crearTablas();
    this.#prepararConsultas();

    // Una "sesion" agrupa todo lo capturado desde que arranca el servidor,
    // para poder separar una demo de otra.
    this.sesionId = this.#abrirSesion();
  }

  #crearTablas() {
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS sesiones (
        id        INTEGER PRIMARY KEY AUTOINCREMENT,
        inicio    INTEGER NOT NULL,
        nota      TEXT
      );

      CREATE TABLE IF NOT EXISTS telemetria (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        sesion_id  INTEGER NOT NULL,
        ts         INTEGER NOT NULL,   -- epoch ms del PC
        ms_placa   INTEGER,            -- millis() del ESP32
        fase       TEXT,
        fase_texto TEXT,
        sem_auto   INTEGER,
        restante   INTEGER,
        verde_calc INTEGER,
        cny        TEXT,               -- "110000"
        cny_sim    TEXT,
        autos1     INTEGER,
        autos2     INTEGER,
        ldr1       INTEGER,
        ldr2       INTEGER,
        co2        INTEGER,
        co2_alto   INTEGER,
        noche      INTEGER,
        ped1       INTEGER,
        ped2       INTEGER,
        leds       TEXT,               -- JSON [0,0,255,255,0,0]
        pantalla   INTEGER
      );

      CREATE TABLE IF NOT EXISTS eventos (
        id        INTEGER PRIMARY KEY AUTOINCREMENT,
        sesion_id INTEGER NOT NULL,
        ts        INTEGER NOT NULL,
        nivel     TEXT,                -- ok | error | evento | info
        texto     TEXT
      );

      CREATE TABLE IF NOT EXISTS comandos (
        id        INTEGER PRIMARY KEY AUTOINCREMENT,
        sesion_id INTEGER NOT NULL,
        ts        INTEGER NOT NULL,
        comando   TEXT,
        origen    TEXT                 -- dashboard | sistema
      );

      CREATE TABLE IF NOT EXISTS conversaciones (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        sesion_id  INTEGER NOT NULL,
        ts         INTEGER NOT NULL,
        mensaje    TEXT,               -- lo que escribio el usuario
        respuesta  TEXT,               -- la frase del modelo
        comandos   TEXT,               -- JSON con los comandos aceptados
        rechazados TEXT,               -- comandos que no pasaron la validacion
        ms         INTEGER             -- cuanto tardo el modelo
      );

      CREATE INDEX IF NOT EXISTS idx_tel_ts  ON telemetria(ts);
      CREATE INDEX IF NOT EXISTS idx_conv_ts ON conversaciones(ts);
      CREATE INDEX IF NOT EXISTS idx_tel_ses ON telemetria(sesion_id, ts);
      CREATE INDEX IF NOT EXISTS idx_evt_ts  ON eventos(ts);
      CREATE INDEX IF NOT EXISTS idx_cmd_ts  ON comandos(ts);
    `);
  }

  #prepararConsultas() {
    this.insTelemetria = this.db.prepare(`
      INSERT INTO telemetria (
        sesion_id, ts, ms_placa, fase, fase_texto, sem_auto, restante, verde_calc,
        cny, cny_sim, autos1, autos2, ldr1, ldr2, co2, co2_alto, noche,
        ped1, ped2, leds, pantalla
      ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    `);

    this.insEvento = this.db.prepare(
      'INSERT INTO eventos (sesion_id, ts, nivel, texto) VALUES (?,?,?,?)'
    );

    this.insComando = this.db.prepare(
      'INSERT INTO comandos (sesion_id, ts, comando, origen) VALUES (?,?,?,?)'
    );

    this.insConversacion = this.db.prepare(
      `INSERT INTO conversaciones (sesion_id, ts, mensaje, respuesta, comandos, rechazados, ms)
       VALUES (?,?,?,?,?,?,?)`
    );
  }

  #abrirSesion() {
    const r = this.db
      .prepare('INSERT INTO sesiones (inicio, nota) VALUES (?, ?)')
      .run(Date.now(), null);
    return Number(r.lastInsertRowid);
  }

  /** Guarda una muestra de telemetria ya normalizada. */
  guardarTelemetria(t) {
    const b = (v) => (v ? 1 : 0);
    this.insTelemetria.run(
      this.sesionId,
      Date.now(),
      t.ms ?? null,
      t.fase ?? null,
      t.faseTexto ?? null,
      b(t.semAuto),
      t.restante ?? null,
      t.verdeCalc ?? null,
      (t.cny ?? []).join(''),
      (t.cnySim ?? []).join(''),
      t.autos?.[0] ?? null,
      t.autos?.[1] ?? null,
      t.ldr?.[0] ?? null,
      t.ldr?.[1] ?? null,
      t.co2 ?? null,
      b(t.co2Alto),
      b(t.noche),
      b(t.ped?.[0]),
      b(t.ped?.[1]),
      JSON.stringify(t.leds ?? []),
      t.pantalla ?? null
    );
  }

  guardarEvento(nivel, texto) {
    this.insEvento.run(this.sesionId, Date.now(), nivel, texto);
  }

  guardarComando(comando, origen) {
    this.insComando.run(this.sesionId, Date.now(), comando, origen);
  }

  guardarConversacion(mensaje, respuesta, comandos, rechazados, ms) {
    this.insConversacion.run(
      this.sesionId, Date.now(), mensaje, respuesta,
      JSON.stringify(comandos ?? []), rechazados ?? '', ms ?? 0
    );
  }

  conversaciones({ limite = 50 } = {}) {
    return this.db
      .prepare('SELECT * FROM conversaciones ORDER BY id DESC LIMIT ?')
      .all(limite)
      .reverse();
  }

  /**
   * Historial de telemetria. Por defecto trae los ultimos `limite` puntos de
   * la sesion actual, en orden cronologico (listo para graficar).
   */
  historial({ limite = 300, sesion = null, desde = null } = {}) {
    const sesionId = sesion ?? this.sesionId;
    const filtros = ['sesion_id = ?'];
    const args = [sesionId];
    if (desde !== null) {
      filtros.push('ts >= ?');
      args.push(desde);
    }
    const filas = this.db
      .prepare(
        `SELECT * FROM telemetria WHERE ${filtros.join(' AND ')}
         ORDER BY ts DESC LIMIT ?`
      )
      .all(...args, limite);
    return filas.reverse();
  }

  ultimaTelemetria() {
    return this.db
      .prepare('SELECT * FROM telemetria ORDER BY id DESC LIMIT 1')
      .get();
  }

  eventos({ limite = 200 } = {}) {
    return this.db
      .prepare('SELECT * FROM eventos ORDER BY id DESC LIMIT ?')
      .all(limite)
      .reverse();
  }

  comandos({ limite = 200 } = {}) {
    return this.db
      .prepare('SELECT * FROM comandos ORDER BY id DESC LIMIT ?')
      .all(limite)
      .reverse();
  }

  sesiones() {
    return this.db
      .prepare(
        `SELECT s.id, s.inicio, s.nota, COUNT(t.id) AS muestras
         FROM sesiones s LEFT JOIN telemetria t ON t.sesion_id = s.id
         GROUP BY s.id ORDER BY s.id DESC`
      )
      .all();
  }

  /** Resumen para las tarjetas de estadisticas del dashboard. */
  resumenSesion() {
    return this.db
      .prepare(
        `SELECT COUNT(*) AS muestras,
                MIN(ts) AS inicio,
                MAX(ts) AS fin,
                AVG(co2) AS co2_prom,
                MAX(co2) AS co2_max,
                AVG(autos1 + autos2) AS autos_prom,
                SUM(CASE WHEN noche = 1 THEN 1 ELSE 0 END) AS muestras_noche
         FROM telemetria WHERE sesion_id = ?`
      )
      .get(this.sesionId);
  }

  /** Exportacion a CSV para el informe del proyecto. */
  csvTelemetria(sesion = null) {
    const sesionId = sesion ?? this.sesionId;
    const filas = this.db
      .prepare('SELECT * FROM telemetria WHERE sesion_id = ? ORDER BY ts')
      .all(sesionId);
    if (filas.length === 0) return '';
    const columnas = Object.keys(filas[0]);
    const lineas = [columnas.join(',')];
    for (const f of filas) {
      lineas.push(columnas.map((c) => JSON.stringify(f[c] ?? '')).join(','));
    }
    return lineas.join('\n');
  }

  cerrar() {
    this.db.close();
  }
}
