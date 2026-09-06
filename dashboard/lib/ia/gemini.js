/**
 * CLIENTE DE GEMINI
 * ============================================================================
 * Lo unico que falta de tu lado es poner GEMINI_API_KEY en dashboard/.env
 *
 *   1. cp .env.ejemplo .env
 *   2. pega tu key en GEMINI_API_KEY
 *   3. npm start
 *
 * Sin key el sistema NO se cae: el chat avisa que falta y todo lo demas
 * (botones, consola, telemetria) sigue funcionando igual.
 *
 * ---------------------------------------------------------------------------
 * Sobre el formato de la respuesta: en vez de usar function calling se le pide
 * al modelo un JSON estricto y se valida cada comando contra el catalogo antes
 * de mandarlo por serial (ver validador.js). Es mas portable entre versiones
 * del SDK y la seguridad no depende del modelo sino de nuestra validacion.
 * ---------------------------------------------------------------------------
 */

import { GoogleGenAI } from '@google/genai';
import { Limitador } from './limitador.js';

// Medido contra el prompt real de este proyecto (mediana con la conexion ya
// caliente; ver la nota de abajo sobre la primera peticion):
//   gemini-3.1-flash-lite   ~1.9 s   500/dia, 15/min   <- el que usamos
//   gemini-3.8-flash        ~7.4 s    20/dia,  5/min
//   gemini-3.7-flash       ~36   s    20/dia,  5/min
//   gemini-3-flash-preview  latencia impredecible: no lo uses aqui.
//
// La cuota pesa mas que la velocidad: los "flash" normales dan 20 peticiones al
// dia en el plan gratuito y se agotan en una sola sesion de pruebas, y a partir
// de ahi el chat solo devuelve errores. El lite da 500.
const MODELO_POR_DEFECTO = 'models/gemini-3.1-flash-lite';

// OJO con la primera peticion: la primera llamada a un modelo despues de un
// rato tarda bastante mas que las siguientes (medido: 24 s frente a 1.9 s en
// el lite, 43 s frente a 7.4 s en el 3.8). Es arranque en frio del lado de
// Google, no lentitud del modelo.

// Sin limite de espera: se aguarda al modelo lo que haga falta.
//
// Antes habia un tope de 30 s y era la causa de casi todos los fallos del chat:
// la latencia real es muy irregular (medido en el mismo modelo y minuto: 1.8 s,
// 18 s y mas de 45 s), asi que el tope cortaba respuestas que iban a llegar
// bien y encima las reportaba como "el modelo es lento", escondiendo el motivo
// verdadero. Mejor esperar.
//
// Se puede volver a poner un tope con GEMINI_TIMEOUT_MS en .env; 0 = sin limite.
const TIMEOUT_MS = 0;

export class ClienteGemini {
  constructor({ apiKey, modelo, nivelPensamiento } = {}) {
    this.apiKey = apiKey ?? process.env['GEMINI_API_KEY'];
    this.modelo = modelo ?? process.env['GEMINI_MODELO'] ?? MODELO_POR_DEFECTO;

    // 'low' por defecto: esto es traducir una orden corta a un comando, no
    // resolver un problema dificil. Bajar el pensamiento es la palanca mas
    // grande que hay contra la latencia. Subelo a 'high' en .env si quieres
    // que razone mas en ordenes complejas.
    this.nivelPensamiento = nivelPensamiento ?? process.env['GEMINI_PENSAMIENTO'] ?? 'low';
    // 0 (o cualquier cosa que no sea un numero positivo) = esperar sin limite.
    const tope = parseInt(process.env['GEMINI_TIMEOUT_MS'] ?? String(TIMEOUT_MS), 10);
    this.timeoutMs = Number.isFinite(tope) && tope > 0 ? tope : 0;
    this.limitador = new Limitador(this.modelo);

    this.ai = this.disponible
      ? new GoogleGenAI({
          apiKey: this.apiKey,
          httpOptions: {
            // Sin la propiedad, el SDK tampoco pone tope por su cuenta.
            ...(this.timeoutMs > 0 ? { timeout: this.timeoutMs } : {}),
            retryOptions: {
              // Por defecto el SDK reintenta 5 veces, con esperas que crecen
              // hasta 60 s, y trata el 429 como reintentable. Resultado: ante
              // un limite de frecuencia se queda esperando por dentro y el
              // error real nunca sale; lo unico que se ve es un timeout.
              //
              // Aqui se quitan los 429 de la lista: si te limitan por
              // frecuencia queremos enterarnos DE INMEDIATO y decirtelo.
              // Los errores 5xx si se reintentan, que esos si son transitorios.
              attempts: 2,
              initialDelay: 0.5,
              maxDelay: 4,
              httpStatusCodes: [500, 502, 503, 504]
            }
          }
        })
      : null;
  }

  get disponible() {
    return Boolean(this.apiKey);
  }

  /**
   * Manda el prompt y devuelve el texto crudo de la respuesta.
   * @param {string} prompt
   * @returns {Promise<{texto: string, ms: number}>}
   */
  async preguntar(prompt) {
    if (!this.disponible) {
      throw new Error(
        'Falta GEMINI_API_KEY. Copia dashboard/.env.ejemplo a dashboard/.env y pon tu key.'
      );
    }

    // Antes de gastar una peticion, revisamos si vamos a chocar con el limite
    // de frecuencia. Avisar al instante es mucho mejor que esperar 30 s.
    const permiso = this.limitador.revisar();
    if (!permiso.permitido) {
      const e = new Error(permiso.motivo);
      e.esLimiteLocal = true;
      e.esperaSegundos = permiso.esperaSegundos;
      throw e;
    }

    const inicio = Date.now();

    // La API usa snake_case dentro de generation_config.
    let config = {
      temperature: 0.2,          // queremos comandos consistentes, no creativos
      max_output_tokens: 2048,
      top_p: 0.95,
      thinking_level: this.nivelPensamiento
    };

    // Hasta 3 intentos: si la API rechaza un parametro por el nombre, se
    // corrige (o se descarta) y se reintenta. Asi un cambio de nombres en el
    // SDK no rompe el chat: como mucho pierde una opcion de afinado.
    for (let intento = 0; intento < 3; intento++) {
      try {
        this.limitador.registrar();
        const interaction = await conTimeout(
          this.ai.interactions.create({
            model: this.modelo,
            input: prompt,
            generation_config: config
          }),
          this.timeoutMs,
          `El modelo ${this.modelo} no respondio en ${this.timeoutMs / 1000} s. ` +
          'Prueba con un modelo mas rapido en GEMINI_MODELO.'
        );
        return { texto: extraerTexto(interaction), ms: Date.now() - inicio };
      } catch (e) {
        // Si Google confirma que nos pasamos, sincronizamos el contador local
        // para no seguir insistiendo en vano.
        if (/429|quota|rate.?limit/i.test(e?.message ?? '')) {
          const limiteReportado = /limit: (\d+)/i.exec(e.message)?.[1];
          this.limitador.marcarRechazo(
            esLimiteDiario(e.message, limiteReportado, this.limitador.limites)
          );
        }
        const corregida = corregirConfig(config, e?.message ?? '');
        if (!corregida) throw new Error(mensajeDeError(e, this.modelo, this.limitador));
        console.warn(`[gemini] ajustando generation_config: ${e.message}`);
        config = corregida;
      }
    }

    throw new Error('No se pudo ajustar generation_config despues de 3 intentos');
  }
}

/**
 * Convierte los errores crudos de la API en algo legible en el chat.
 * El de cuota es el mas frecuente con el plan gratuito y su texto original
 * son seis lineas de enlaces.
 */
export function mensajeDeError(e, modelo, limitador) {
  const bruto = e?.message ?? String(e);

  // Nuestro propio freno: ya viene con el texto listo.
  if (e?.esLimiteLocal) return bruto;

  if (/429|quota|rate.?limit/i.test(bruto)) {
    const espera = /retry in ([\d.]+)s/i.exec(bruto)?.[1];
    const limite = /limit: (\d+)/i.exec(bruto)?.[1];
    const lim = limitador?.limites;
    const porDia = esLimiteDiario(bruto, limite, lim);

    if (porDia) {
      return `Se acabo la cuota DIARIA del plan gratuito para ${modelo}` +
        (limite ? ` (${limite} peticiones al dia)` : '') + '. ' +
        'Se renueva sola. Si necesitas seguir ahora, cambia GEMINI_MODELO en .env: ' +
        'models/gemini-3.1-flash-lite tiene 500 al dia en vez de 20. ' +
        'Los botones y la consola no dependen de la IA y siguen funcionando.';
    }

    return `Te frenaron por frecuencia: el plan gratuito permite ` +
      `${lim?.rpm ?? limite ?? 5} peticiones por MINUTO en ${modelo}. ` +
      (espera ? `Espera ~${Math.ceil(Number(espera))} s.` : 'Espera un minuto.') +
      ' No es que se haya acabado la cuota del dia: es solo el ritmo. ' +
      'Si vas a mandar muchos mensajes seguidos, usa models/gemini-3.1-flash-lite (15 por minuto).';
  }

  if (/no respondio en/i.test(bruto)) {
    return bruto + ' Si acabas de mandar varios mensajes seguidos, ' +
      'lo mas probable es que sea un limite de frecuencia y no lentitud del modelo.';
  }

  if (/401|403|API key|permission|API_KEY_INVALID/i.test(bruto)) {
    return 'La clave de Gemini no es valida o no tiene permisos. Revisa GEMINI_API_KEY en dashboard/.env';
  }

  if (/404/i.test(bruto)) {
    return `El modelo "${modelo}" no existe o ya no esta disponible. Cambia GEMINI_MODELO en dashboard/.env`;
  }

  if (/ENOTFOUND|ECONNREFUSED|network|fetch failed/i.test(bruto)) {
    return 'No hay conexion a internet para llegar a Gemini. La maqueta sigue funcionando por USB.';
  }

  return bruto.split('\n')[0].slice(0, 200);
}

/**
 * ¿El 429 es por el limite DIARIO o por el de POR MINUTO?
 *
 * Google no lo dice de forma uniforme: a veces el nombre de la metrica lleva
 * "per_day", a veces solo manda "limit: 20". La pista mas fiable es comparar
 * ese numero con los limites conocidos del modelo (20/dia frente a 5/minuto).
 */
export function esLimiteDiario(texto, limiteReportado, limitesModelo) {
  if (/per[_ -]?day|perday|\bRPD\b/i.test(texto)) return true;
  if (/per[_ -]?minute|perminute|\bRPM\b/i.test(texto)) return false;

  const n = Number(limiteReportado);
  if (Number.isFinite(n) && limitesModelo) {
    if (n === limitesModelo.rpd) return true;    // 20  -> diario
    if (n === limitesModelo.rpm) return false;   // 5   -> por minuto
  }
  return false;   // ante la duda, el de minuto: se resuelve esperando
}

/** Corta la espera si el modelo se demora demasiado. Con ms <= 0 no corta nada. */
function conTimeout(promesa, ms, mensaje) {
  if (!ms || ms <= 0) return promesa;
  let temporizador;
  const tope = new Promise((_, rechazar) => {
    temporizador = setTimeout(() => rechazar(new Error(mensaje)), ms);
  });
  return Promise.race([promesa, tope]).finally(() => clearTimeout(temporizador));
}

/**
 * Lee un error del tipo:
 *   Unknown parameter 'generation_config.topP'. Did you mean 'top_p'?
 * y devuelve la config corregida (renombrando o quitando el parametro).
 * Devuelve null si el error no es de ese tipo.
 */
export function corregirConfig(config, mensaje) {
  const m = /Unknown parameter '(?:generation_config\.)?([\w.]+)'(?:.*?Did you mean '([\w.]+)')?/i
    .exec(mensaje);
  if (!m) return null;

  const malo = m[1].split('.').pop();
  const bueno = m[2]?.split('.').pop();
  if (!(malo in config)) return null;      // no sabemos que arreglar

  const nueva = { ...config };
  const valor = nueva[malo];
  delete nueva[malo];
  if (bueno) nueva[bueno] = valor;         // renombrar
  return nueva;                            // sin sugerencia: simplemente se quita
}

/**
 * Saca el texto de la respuesta. El SDK expone `interaction.steps`; se recorre
 * de atras hacia adelante buscando el ultimo contenido de texto, y se cae a
 * varias formas alternativas por si cambia la version del SDK.
 */
function extraerTexto(interaction) {
  if (!interaction) return '';

  // Camino normal: el SDK expone el texto final ya juntado.
  if (typeof interaction.output_text === 'string' && interaction.output_text.trim()) {
    return interaction.output_text;
  }
  if (typeof interaction.text === 'string' && interaction.text.trim()) {
    return interaction.text;
  }

  // Respaldo: recorrer los steps de atras hacia adelante. Los de tipo
  // 'thought' son el razonamiento interno y no llevan la respuesta.
  const pasos = interaction.steps ?? [];
  for (let i = pasos.length - 1; i >= 0; i--) {
    if (pasos[i]?.type === 'thought') continue;
    const texto = textoDePaso(pasos[i]);
    if (texto) return texto;
  }

  const salida = interaction.output ?? interaction.response;
  if (typeof salida === 'string') return salida;

  return '';
}

function textoDePaso(paso) {
  if (!paso) return '';
  if (typeof paso === 'string') return paso;
  if (typeof paso.text === 'string' && paso.text.trim()) return paso.text;

  // content llega como arreglo de bloques [{ text, type }]
  const partes = paso.content?.parts ?? paso.parts ?? paso.content;
  if (Array.isArray(partes)) {
    const junto = partes
      .map((p) => (typeof p === 'string' ? p : p?.text ?? ''))
      .join('')
      .trim();
    if (junto) return junto;
  }
  if (typeof paso.content === 'string') return paso.content;
  return '';
}
