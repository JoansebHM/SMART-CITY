/**
 * VALIDADOR — la ultima puerta antes del puerto serial.
 * ============================================================================
 * Ningun comando llega a la placa sin pasar por aqui. Se comprueba contra el
 * catalogo que el comando exista y que sus argumentos esten en rango.
 *
 * Esto es lo que hace que la capa de IA sea segura: por muy convencido que
 * este el modelo, si el comando no valida, no sale.
 */

import { POR_NOMBRE } from './catalogo.js';

// Debe coincidir con MAX_PROGRAMADOS en SmartCity_Maqueta.ino: es el numero de
// ranuras que la placa reserva para comandos diferidos ("en" y "secuencia").
const MAX_PROGRAMADOS = 8;

/**
 * @param {string} linea comando crudo, p.ej. "prio c1 60"
 * @returns {{ok: boolean, comando?: string, motivo?: string}}
 */
export function validarComando(linea) {
  const texto = String(linea ?? '').trim();
  if (!texto) return { ok: false, motivo: 'Comando vacio' };

  if (texto.includes('\n')) {
    return { ok: false, motivo: 'Un comando no puede tener saltos de linea' };
  }
  if (texto.length > 200) {
    return { ok: false, motivo: 'Comando demasiado largo' };
  }

  const partes = texto.split(/\s+/);
  const nombre = partes[0].toLowerCase();
  const def = POR_NOMBRE[nombre];
  if (!def) {
    return { ok: false, motivo: `El comando "${nombre}" no existe en el catalogo` };
  }

  // "en" y "secuencia" llevan otros comandos adentro: se validan aparte.
  if (nombre === 'en') return validarEn(texto, partes);
  if (nombre === 'secuencia') return validarSecuencia(texto);

  const args = partes.slice(1);
  const esperados = def.args ?? [];

  for (let i = 0; i < esperados.length; i++) {
    const esperado = esperados[i];
    const valor = args[i];

    if (valor === undefined) {
      if (esperado.opcional) continue;
      return { ok: false, motivo: `Falta un argumento. Sintaxis: ${def.sintaxis}` };
    }

    // Un argumento marcado restoDeLinea se traga todo lo que queda.
    if (esperado.restoDeLinea) break;

    const problema = validarArgumento(valor.toLowerCase(), esperado, def, args[0]?.toLowerCase());
    if (problema) return { ok: false, motivo: problema };
  }

  // Sobran argumentos y ninguno era de rienda suelta
  const ultimo = esperados[esperados.length - 1];
  if (args.length > esperados.length && !ultimo?.restoDeLinea) {
    return { ok: false, motivo: `Sobran argumentos. Sintaxis: ${def.sintaxis}` };
  }

  return { ok: true, comando: texto };
}

function validarArgumento(valor, esperado, def, arg0) {
  switch (esperado.tipo) {
    case 'enum':
      if (!esperado.valores.includes(valor)) {
        return `"${valor}" no es valido. Opciones: ${esperado.valores.join(', ')}`;
      }
      return null;

    case 'entero': {
      if (!/^\d+$/.test(valor)) return `"${valor}" deberia ser un numero entero`;
      const n = parseInt(valor, 10);

      // Comandos como "set" tienen una unidad distinta por parametro: el rango
      // real depende del argumento anterior, no del comando.
      const propio = esperado.rangoPorArg0?.[arg0];
      const min = propio ? propio[0] : esperado.min;
      const max = propio ? propio[1] : esperado.max;

      if (min !== undefined && n < min) {
        return `${n} es menor que el minimo${propio ? ` de "${arg0}"` : ''} (${min})`;
      }
      if (max !== undefined && n > max) {
        return `${n} es mayor que el maximo${propio ? ` de "${arg0}"` : ''} (${max})`;
      }
      return null;
    }

    case 'texto':
      if (esperado.patron && !esperado.patron.test(valor)) {
        return `"${valor}" no tiene la forma esperada. Sintaxis: ${def.sintaxis}`;
      }
      return null;

    default:
      return null;
  }
}

/** "en <ms> <comando>" -> el comando interior tambien tiene que validar. */
function validarEn(texto, partes) {
  if (partes.length < 3) {
    return { ok: false, motivo: 'Sintaxis: en <milisegundos> <comando>' };
  }
  if (!/^\d+$/.test(partes[1])) {
    return { ok: false, motivo: 'El retardo de "en" debe ser un numero en milisegundos' };
  }
  if (parseInt(partes[1], 10) > 600000) {
    return { ok: false, motivo: 'El retardo maximo de "en" es 600000 ms (10 minutos)' };
  }
  const interior = partes.slice(2).join(' ');
  if (/^(en|secuencia)\b/i.test(interior)) {
    return { ok: false, motivo: 'No se permite anidar "en" ni "secuencia"' };
  }
  const r = validarComando(interior);
  if (!r.ok) return { ok: false, motivo: `Dentro de "en": ${r.motivo}` };
  return { ok: true, comando: texto };
}

/** "secuencia a ; espera 500 ; b" -> valida cada tramo. */
function validarSecuencia(texto) {
  const cuerpo = texto.replace(/^secuencia\s+/i, '');
  const tramos = cuerpo.split(';').map((t) => t.trim()).filter(Boolean);
  if (tramos.length === 0) {
    return { ok: false, motivo: 'Sintaxis: secuencia cmd1 ; espera 500 ; cmd2' };
  }

  // El limite real de la placa son MAX_PROGRAMADOS (8) comandos EN ESPERA.
  // No todos los tramos ocupan una ranura: los "espera" no son comandos, y los
  // que van antes de la primera espera se ejecutan de una y tampoco reservan
  // nada (ver cmdSecuencia en Comandos.ino).
  //
  // Contar tramos en vez de ranuras rechazaba secuencias que la maqueta si
  // podia ejecutar: una de 3 colores x 3 comandos + 2 esperas son 11 tramos
  // pero solo 6 ranuras.
  let esperaAcumulada = 0;
  let ranuras = 0;

  for (const tramo of tramos) {
    if (/^espera\s+\d+$/i.test(tramo)) { esperaAcumulada++; continue; }
    if (/^(en|secuencia)\b/i.test(tramo)) {
      return { ok: false, motivo: 'No se permite anidar "en" ni "secuencia"' };
    }
    const r = validarComando(tramo);
    if (!r.ok) {
      // La confusion mas comun es separar con comas dentro de un paso. El error
      // que sale por defecto ("sobran argumentos") no lo deja ver.
      if (tramo.includes(',')) {
        return {
          ok: false,
          motivo: `En el paso "${tramo}": los comandos se separan con ";", no con comas. ` +
                  `Escribelo como "${tramo.split(',').map((t) => t.trim()).join(' ; ')}".`
        };
      }
      return { ok: false, motivo: `En el paso "${tramo}": ${r.motivo}` };
    }
    if (esperaAcumulada > 0) ranuras++;
  }

  if (ranuras > MAX_PROGRAMADOS) {
    return {
      ok: false,
      motivo: `La secuencia deja ${ranuras} comandos en espera y la placa solo ` +
              `guarda ${MAX_PROGRAMADOS}. Los pasos antes de la primera "espera" no ` +
              `cuentan: reparte la secuencia o usa menos pasos diferidos.`
    };
  }

  return { ok: true, comando: texto };
}

/** Valida una lista y separa lo aceptado de lo rechazado. */
export function validarLista(comandos) {
  const aceptados = [];
  const rechazados = [];
  for (const c of comandos ?? []) {
    const r = validarComando(c);
    if (r.ok) aceptados.push(r.comando);
    else rechazados.push({ comando: String(c), motivo: r.motivo });
  }
  return { aceptados, rechazados };
}
