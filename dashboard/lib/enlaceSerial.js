/**
 * Enlace con el ESP32-S3 por puerto serial.
 *
 * Responsabilidades:
 *   - Encontrar el puerto de la placa (o usar el que se le indique).
 *   - Reconectar solo si se desconecta el USB.
 *   - Partir la entrada en lineas y clasificarlas:
 *       linea que empieza por '{'  -> telemetria JSON
 *       cualquier otra             -> texto de consola ([OK], [EVENTO], ...)
 *   - Al conectar, poner la placa en modo telemetria JSON automaticamente.
 */

import { EventEmitter } from 'node:events';
import { existsSync } from 'node:fs';
import { SerialPort } from 'serialport';
import { ReadlineParser } from '@serialport/parser-readline';

// Fabricantes / descripciones tipicas de placas ESP32-S3.
const PISTAS_ESP32 = [
  'esp32', 'espressif', 'usb jtag', 'cp210', 'ch340', 'ch910', 'silicon labs', 'wch'
];

export class EnlaceSerial extends EventEmitter {
  constructor({ ruta = null, baudios = 115200, periodoTelemetria = 200 } = {}) {
    super();
    this.rutaPedida = ruta;
    this.baudios = baudios;
    this.periodoTelemetria = periodoTelemetria;
    this.puerto = null;
    this.conectado = false;
    this.rutaActual = null;
    this.reintento = null;
    this.ultimoAviso = null;
  }

  static async listarPuertos() {
    return SerialPort.list();
  }

  /** Adivina cual de los puertos disponibles es la placa. */
  static async detectarPuerto() {
    const puertos = await SerialPort.list();
    const candidatos = puertos.filter((p) => {
      const texto = `${p.manufacturer ?? ''} ${p.friendlyName ?? ''} ${p.path}`.toLowerCase();
      return PISTAS_ESP32.some((pista) => texto.includes(pista));
    });
    const preferido = candidatos[0] ?? null;
    if (!preferido) return null;

    // En macOS hay que usar /dev/cu.* y no /dev/tty.*: abrir el tty espera
    // señal de "carrier detect" y se queda colgado. serialport suele listar
    // solo el tty, asi que traducimos la ruta nosotros.
    if (process.platform === 'darwin' && preferido.path.startsWith('/dev/tty.')) {
      const equivalente = preferido.path.replace('/dev/tty.', '/dev/cu.');
      if (existsSync(equivalente)) return equivalente;
    }
    return preferido.path;
  }

  async conectar() {
    const ruta = this.rutaPedida ?? (await EnlaceSerial.detectarPuerto());

    if (!ruta) {
      this.#avisar({
        conectado: false,
        mensaje: 'No se encontro ninguna placa. Conecta el ESP32 por USB.'
      });
      this.#programarReintento();
      return;
    }

    try {
      this.puerto = new SerialPort({ path: ruta, baudRate: this.baudios });
    } catch (e) {
      this.#avisar({ conectado: false, mensaje: `No se pudo abrir ${ruta}: ${e.message}` });
      this.#programarReintento();
      return;
    }

    const parser = this.puerto.pipe(new ReadlineParser({ delimiter: '\n' }));

    this.puerto.on('open', () => {
      this.conectado = true;
      this.rutaActual = ruta;
      this.#avisar({ conectado: true, puerto: ruta, mensaje: `Conectado a ${ruta}` });

      // La placa se acaba de reiniciar al abrir el puerto: esperamos a que
      // termine el setup() antes de pedirle telemetria.
      setTimeout(() => {
        this.enviar(`mon ${this.periodoTelemetria}`);
        this.enviar('mon json');
      }, 2500);
    });

    parser.on('data', (linea) => this.#procesarLinea(linea));

    this.puerto.on('close', () => {
      if (this.conectado) {
        this.conectado = false;
        this.#avisar({ conectado: false, mensaje: 'Placa desconectada' });
      }
      this.#programarReintento();
    });

    this.puerto.on('error', (e) => {
      // El caso mas comun de lejos: el Monitor Serie del Arduino IDE tiene
      // el puerto abierto. Un puerto serial solo admite un programa a la vez.
      const mensaje = /busy|access denied/i.test(e.message)
        ? `El puerto ${ruta} esta ocupado. Cierra el Monitor Serie del Arduino IDE (se reintenta solo).`
        : `Error serial: ${e.message}`;
      this.#avisar({ conectado: false, mensaje });

      // Si fallo la apertura no llega evento 'close', asi que el reintento
      // hay que programarlo aqui: en cuanto se libere el puerto, entra solo.
      this.puerto = null;
      this.#programarReintento();
    });
  }

  /**
   * Emite un cambio de estado, pero sin repetir el mismo mensaje una y otra
   * vez: mientras se reintenta cada 2 s el aviso seria identico y llenaria
   * la consola y la base de datos de ruido.
   */
  #avisar(estado) {
    if (estado.mensaje === this.ultimoAviso) return;
    this.ultimoAviso = estado.mensaje;
    this.emit('estado', estado);
  }

  #procesarLinea(cruda) {
    const linea = cruda.replace(/\r/g, '').trim();
    if (!linea) return;

    if (linea.startsWith('{')) {
      try {
        this.emit('telemetria', JSON.parse(linea));
      } catch {
        // JSON truncado (p. ej. la placa se reinicio a mitad de linea): se ignora.
      }
      return;
    }

    let nivel = 'info';
    if (linea.startsWith('[OK]')) nivel = 'ok';
    else if (linea.startsWith('[ERROR]')) nivel = 'error';
    else if (linea.startsWith('[EVENTO]')) nivel = 'evento';

    this.emit('consola', { nivel, texto: linea });
  }

  #programarReintento() {
    if (this.reintento) return;
    this.reintento = setTimeout(() => {
      this.reintento = null;
      this.conectar();
    }, 2000);
  }

  /** Manda un comando de texto a la placa (le agrega el salto de linea). */
  enviar(comando) {
    if (!this.puerto?.writable) return false;
    this.puerto.write(`${comando}\n`);
    return true;
  }

  cerrar() {
    if (this.reintento) clearTimeout(this.reintento);
    this.puerto?.close?.(() => {});
  }
}
