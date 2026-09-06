/**
 * AGENTE — traduce lenguaje natural a comandos de la maqueta.
 * ============================================================================
 * Flujo de cada mensaje:
 *
 *   1. Recuperador  : elige 4-5 categorias del catalogo segun lo que dijiste.
 *   2. Contexto     : arma un resumen CORTO del estado real (de la telemetria)
 *                     + las ultimas ordenes, para que sepa que dejo activo.
 *   3. Modelo       : recibe solo eso y responde un JSON con los comandos.
 *   4. Validador    : cada comando se verifica contra el catalogo.
 *   5. Salida       : los comandos aceptados se mandan por serial.
 *
 * El modelo NUNCA ve el codigo fuente ni el catalogo completo.
 */

import { renderCategorias } from './catalogo.js';
import { elegirCategorias } from './recuperador.js';
import { validarLista } from './validador.js';
import { ClienteGemini } from './gemini.js';

const MAX_MEMORIA = 6;   // cuantos turnos recuerda

/** Mismo orden que `leds`/`ledSim`/`parpadeo` en la telemetria. */
const NOMBRES_LED = ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'];

export class Agente {
  constructor({ cliente } = {}) {
    this.cliente = cliente ?? new ClienteGemini();
    this.memoria = [];    // [{ mensaje, comandos, cuando }]
  }

  get disponible() {
    return this.cliente.disponible;
  }

  /**
   * @param {string} mensaje       lo que escribio el usuario
   * @param {object|null} telemetria ultima telemetria recibida de la placa
   * @returns {Promise<object>} resultado con comandos, respuesta y diagnostico
   */
  async interpretar(mensaje, telemetria) {
    const seleccion = elegirCategorias(mensaje);
    const prompt = this.#construirPrompt(mensaje, telemetria, seleccion);

    const { texto, ms } = await this.cliente.preguntar(prompt);
    const cruda = extraerJson(texto);

    if (!cruda) {
      return {
        ok: false,
        respuesta: 'No entendi la respuesta del modelo. Intenta reformular la orden.',
        comandos: [], rechazados: [],
        diagnostico: { categorias: seleccion.categorias, ms, textoCrudo: texto.slice(0, 400) }
      };
    }

    const { aceptados, rechazados } = validarLista(cruda.comandos);

    this.#recordar(mensaje, aceptados);

    return {
      ok: true,
      respuesta: cruda.respuesta ?? '',
      comandos: aceptados,
      rechazados,
      diagnostico: {
        categorias: seleccion.categorias,
        calleDetectada: seleccion.calle,
        ms,
        tokensPrompt: Math.round(prompt.length / 4)   // estimacion util para medir
      }
    };
  }

  #recordar(mensaje, comandos) {
    this.memoria.push({ mensaje, comandos, cuando: Date.now() });
    while (this.memoria.length > MAX_MEMORIA) this.memoria.shift();
  }

  olvidar() {
    this.memoria = [];
  }

  // --------------------------------------------------------------------------
  // Construccion del prompt
  // --------------------------------------------------------------------------
  #construirPrompt(mensaje, telemetria, seleccion) {
    const vocabulario = renderCategorias(seleccion.categorias);
    const estado = resumirEstado(telemetria);
    const historial = this.#resumirMemoria();

    const pistaCalle = seleccion.calle
      ? `\nEl usuario menciono una calle que corresponde a: ${seleccion.calle}`
      : '';

    return `Eres el controlador de una maqueta de semaforos (un cruce de dos calles).
Tu unico trabajo es traducir la orden del usuario a comandos de texto de la maqueta.

REGLAS
- Responde SOLO con un objeto JSON, sin texto alrededor y sin bloques de codigo.
- Formato exacto: {"comandos": ["...", "..."], "respuesta": "una frase corta en espanol"}
- Usa unicamente los comandos listados abajo, con esa sintaxis exacta.
- Si no hace falta ningun comando (por ejemplo si solo preguntan algo), devuelve "comandos": [].
- El orden importa: los comandos se ejecutan uno tras otro.
- Calle 1 = c1 (horizontal). Calle 2 = c2 (vertical).
- Para una emergencia usa "prio". Guarda el estado anterior por si solo, asi que
  para volver a la normalidad basta "prio off".
- Si la orden implica una duracion concreta, prefiere "prio c1 60" antes que "inf".
- Si el usuario dice que la emergencia ya paso, usa "prio off".
- No inventes comandos ni parametros que no aparezcan abajo.
- Lee las notas "OJO:" de cada comando: ahi esta lo que el comando NO hace.

COMO COMBINAR COMANDOS (aqui es donde se falla)
- Si un "escenario" ya cubre lo que piden, usalo SOLO a el. No armes a mano una
  situacion que ya tiene escenario: te vas a dejar un paso.
- Un comando que apaga o fija LEDs ("leds off", "apagar", "led x off") los deja
  asi de forma permanente. Nunca mandes despues un efecto que dependa de que
  ese LED este encendido: no se veria nada.
- Para que un LED se vea, alguien tiene que encenderlo: el ciclo del semaforo
  (LED en "auto") o un "led <x> on". El brillo tampoco puede estar en 0.
- Revisa cada comando que propongas contra el ESTADO ACTUAL: si ya hay LEDs
  forzados, sensores fijos o brillo bajo, tu orden puede quedar invisible.
  Limpia primero ("reset" o "leds auto") y luego aplica lo nuevo.
- Antes de fijar una fase a mano, manda "sem manual"; si no, el ciclo la borra.
- Para "vuelve a la normalidad" basta "reset" (y "prio off" si hay prioridad).
- Si un juego de luces enciende los dos verdes (lg1 y lg2) a la vez, el
  interlock apaga uno sin avisar: empieza con "seguro off" y cierra con
  "reset", que ya lo vuelve a activar.
- Solo caben 8 comandos en espera entre todos los "en" y las "secuencia". Los
  pasos que van antes de la primera "espera" corren de inmediato y no ocupan
  ranura: agrupa ahi el primer tramo y deja diferido solo lo demas.

ESTADO ACTUAL DE LA MAQUETA
${estado}
${historial}${pistaCalle}

COMANDOS DISPONIBLES
${vocabulario}

ORDEN DEL USUARIO
"${mensaje}"

JSON:`;
  }

  #resumirMemoria() {
    if (this.memoria.length === 0) return '';
    const lineas = this.memoria.map((m, i) => {
      const cmds = m.comandos.length ? m.comandos.join(', ') : '(ninguno)';
      return `${i + 1}. "${m.mensaje}" -> ${cmds}`;
    });
    return `\nORDENES ANTERIORES (de mas vieja a mas reciente)\n${lineas.join('\n')}\n`;
  }
}

/**
 * Resumen del estado en pocas lineas. Es deliberadamente corto: el modelo no
 * necesita los 40 campos de la telemetria, solo lo que cambia una decision.
 * Y viene de la telemetria real, no de la memoria: si algo se restauro solo
 * (por el watchdog, por ejemplo), aqui se refleja.
 */
export function resumirEstado(t) {
  if (!t) return '- Sin datos de la placa todavia (puede estar desconectada).';

  const l = [];
  l.push(`- Fase: ${t.faseTexto ?? '?'} (${t.semAuto === false ? 'MANUAL' : 'automatico'})`);

  if (t.prioridad) {
    const resta = t.prioridadResta >= 0 ? `${t.prioridadResta} s restantes` : 'indefinida';
    l.push(`- PRIORIDAD ACTIVA en Calle ${t.prioridad} (${resta})`);
  }

  l.push(`- Autos detectados: Calle 1 = ${t.autos?.[0] ?? 0}, Calle 2 = ${t.autos?.[1] ?? 0}`);
  l.push(`- Ambiente: ${t.noche ? 'noche' : 'dia'}, CO2 ${t.co2Alto ? 'ALTO' : 'normal'} (${t.co2 ?? '?'})`);

  if (t.brilloMaestro !== undefined && t.brilloMaestro < 255) {
    l.push(`- Brillo maestro reducido a ${t.brilloMaestro}/255${t.brilloMaestro === 0 ? ' (TODO SE VE APAGADO)' : ''}`);
  }

  // Detalle por LED: sin esto el modelo no puede saber si el efecto que va a
  // mandar quedaria invisible sobre un LED apagado a la fuerza.
  const forzados = (t.ledSim ?? [])
    .map((v, i) => (v >= 0 ? `${NOMBRES_LED[i]}=${v === 0 ? 'apagado' : v}` : null))
    .filter(Boolean);
  if (forzados.length === 6 && (t.ledSim ?? []).every((v) => v === 0)) {
    l.push('- LOS SEIS LEDS ESTAN APAGADOS A LA FUERZA: el semaforo no se ve. Hace falta "leds auto" (o "led <x> on") antes de cualquier efecto de luz.');
  } else if (forzados.length) {
    l.push(`- LEDs forzados a mano, no siguen al semaforo: ${forzados.join(', ')}`);
  }

  const parpadeando = (t.parpadeo ?? [])
    .map((p, i) => (p > 0 ? NOMBRES_LED[i] : null))
    .filter(Boolean);
  if (parpadeando.length) {
    // Un parpadeo sobre un LED apagado no se ve: hay que decirlo explicitamente.
    const invisibles = parpadeando.filter((n) => {
      const i = NOMBRES_LED.indexOf(n);
      return t.ledSim?.[i] === 0 || t.brilloMaestro === 0;
    });
    l.push(`- Parpadeo activo en: ${parpadeando.join(', ')}` +
      (invisibles.length
        ? ` (pero ${invisibles.join(', ')} ${invisibles.length === 1 ? 'esta apagado' : 'estan apagados'}, asi que NO se ${invisibles.length === 1 ? 've' : 'ven'} parpadear)`
        : ''));
  }
  if (t.cnySim?.some((v) => v === 1)) l.push('- Hay sensores de vehiculos simulados');
  if (t.flujo?.some((f) => f > 0)) l.push(`- Flujo automatico de trafico: ${t.flujo.join('/')} autos/min`);
  if (t.lcdTexto) l.push(`- El LCD muestra el mensaje: "${t.lcdTexto}"`);
  if (t.programados > 0) l.push(`- Hay ${t.programados} comandos programados pendientes`);
  if (t.ped?.[0] || t.ped?.[1]) l.push('- Hay peatones esperando');
  if (t.seguro === false) l.push('- ATENCION: el interlock de seguridad esta desactivado');

  return l.join('\n');
}

/**
 * Saca el objeto JSON de la respuesta, aunque venga envuelto en ```json o con
 * texto alrededor.
 */
export function extraerJson(texto) {
  if (!texto) return null;

  const limpio = String(texto)
    .replace(/```json/gi, '')
    .replace(/```/g, '')
    .trim();

  try {
    return JSON.parse(limpio);
  } catch { /* seguimos intentando */ }

  // Buscar el primer objeto balanceado dentro del texto
  const inicio = limpio.indexOf('{');
  if (inicio < 0) return null;
  let profundidad = 0;
  let enCadena = false;
  let escape = false;
  for (let i = inicio; i < limpio.length; i++) {
    const c = limpio[i];
    if (escape) { escape = false; continue; }
    if (c === '\\') { escape = true; continue; }
    if (c === '"') { enCadena = !enCadena; continue; }
    if (enCadena) continue;
    if (c === '{') profundidad++;
    else if (c === '}') {
      profundidad--;
      if (profundidad === 0) {
        try { return JSON.parse(limpio.slice(inicio, i + 1)); } catch { return null; }
      }
    }
  }
  return null;
}
