/**
 * RECUPERADOR — decide que parte del catalogo ve el modelo.
 * ============================================================================
 * El catalogo completo son ~35 comandos. Mandarlos todos en cada mensaje es
 * desperdiciar tokens y latencia. Aqui se puntua el mensaje del usuario contra
 * las palabras clave de cada categoria y solo entran al prompt las 2-3 que
 * importan.
 *
 * Todo esto es local: no cuesta ni una llamada al modelo.
 */

import { CATEGORIAS, ALIAS_CALLES } from './catalogo.js';

/** Quita tildes y baja a minusculas, para que "semáforo" empate con "semaforo". */
export function normalizar(texto) {
  return texto
    .toLowerCase()
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/\s+/g, ' ')
    .trim();
}

// Categorias que siempre se incluyen: sin ellas el modelo no puede deshacer
// lo que hizo antes ni consultar como quedo todo.
const SIEMPRE = ['sistema', 'semaforo'];

const MAX_CATEGORIAS = 5;

/**
 * Puntua cada categoria contra el mensaje y devuelve las mas relevantes.
 * @returns {{categorias: string[], puntajes: Object, calle: string|null}}
 */
export function elegirCategorias(mensaje) {
  const texto = normalizar(mensaje);
  const puntajes = {};

  for (const [nombre, def] of Object.entries(CATEGORIAS)) {
    let puntaje = 0;
    for (const clave of def.claves) {
      if (texto.includes(normalizar(clave))) {
        // Las claves de varias palabras son mas especificas: valen mas.
        puntaje += clave.includes(' ') ? 3 : 2;
      }
    }
    if (puntaje > 0) puntajes[nombre] = puntaje;
  }

  const ordenadas = Object.entries(puntajes)
    .sort((a, b) => b[1] - a[1])
    .map(([nombre]) => nombre);

  const elegidas = [];
  for (const c of ordenadas) {
    if (elegidas.length >= MAX_CATEGORIAS - SIEMPRE.length) break;
    if (!SIEMPRE.includes(c)) elegidas.push(c);
  }

  // Si el mensaje no dispara ninguna palabra clave, damos un conjunto util
  // por defecto en vez de dejar al modelo sin vocabulario.
  if (elegidas.length === 0) elegidas.push('luces', 'sensores', 'emergencia');

  return {
    categorias: [...SIEMPRE, ...elegidas],
    puntajes,
    calle: detectarCalle(texto)
  };
}

/**
 * Traduce el nombre real de una calle ("calle 10") al identificador que
 * entiende la maqueta (c1 / c2). Devuelve null si no se menciona ninguna.
 */
export function detectarCalle(textoNormalizado) {
  const texto = normalizar(textoNormalizado);
  let mejor = null;
  let largoMejor = 0;
  for (const [id, alias] of Object.entries(ALIAS_CALLES)) {
    for (const a of alias) {
      const na = normalizar(a);
      // Gana el alias mas largo que aparezca: "calle 10" antes que "calle 1".
      if (texto.includes(na) && na.length > largoMejor) {
        mejor = id;
        largoMejor = na.length;
      }
    }
  }
  return mejor;
}
