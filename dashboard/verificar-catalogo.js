/**
 * Comprueba que el catalogo que lee la IA y el firmware no se desincronicen.
 *
 * El riesgo real de tener el vocabulario en dos sitios es que alguien agregue
 * un comando en Comandos.ino y se le olvide el catalogo (la IA nunca lo usaria),
 * o al reves (la IA mandaria un comando que la placa no entiende).
 *
 *   npm run verificar-catalogo
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { COMANDOS, CATEGORIAS } from './lib/ia/catalogo.js';
import { validarComando } from './lib/ia/validador.js';
import { PlacaSimulada } from './lib/simulador.js';

const aqui = dirname(fileURLToPath(import.meta.url));
const rutaFirmware = join(aqui, '..', 'SmartCity_Maqueta', 'Comandos.ino');

const firmware = readFileSync(rutaFirmware, 'utf8');

// Los comandos del firmware salen del enrutador: if (cmd == "xxx")
const enFirmware = new Set(
  [...firmware.matchAll(/cmd\s*==\s*"([a-z0-9?]+)"/g)].map((m) => m[1])
);

// Alias que existen en el firmware pero no vale la pena documentar a la IA
const ALIAS = new Set(['help', '?', 'status', 'auto', 'next']);

const enCatalogo = new Set(COMANDOS.map((c) => c.nombre));

const faltanEnCatalogo = [...enFirmware].filter((c) => !enCatalogo.has(c) && !ALIAS.has(c));
const faltanEnFirmware = [...enCatalogo].filter((c) => !enFirmware.has(c));

// Toda categoria usada debe estar declarada, y toda declarada debe usarse
const categoriasUsadas = new Set(COMANDOS.map((c) => c.categoria));
const categoriasDeclaradas = new Set(Object.keys(CATEGORIAS));
const catSinDeclarar = [...categoriasUsadas].filter((c) => !categoriasDeclaradas.has(c));
const catSinUsar = [...categoriasDeclaradas].filter((c) => !categoriasUsadas.has(c));

let problemas = 0;
const fallo = (titulo, lista) => {
  if (lista.length === 0) return;
  problemas += lista.length;
  console.error(`\n  ${titulo}`);
  for (const x of lista) console.error(`    - ${x}`);
};

console.log(`Comandos en el firmware : ${enFirmware.size}`);
console.log(`Comandos en el catalogo : ${enCatalogo.size}`);
console.log(`Categorias              : ${categoriasDeclaradas.size}`);

fallo('En el firmware pero NO en el catalogo (la IA no los conoce):', faltanEnCatalogo);
fallo('En el catalogo pero NO en el firmware (la placa los rechazaria):', faltanEnFirmware);
fallo('Categorias usadas sin declarar:', catSinDeclarar);
fallo('Categorias declaradas sin usar:', catSinUsar);

// Cada comando debe traer lo minimo para servirle al modelo
const incompletos = COMANDOS.filter((c) => !c.sintaxis || !c.que || !c.ejemplos?.length)
  .map((c) => `${c.nombre}: le falta sintaxis, descripcion o ejemplos`);
fallo('Comandos incompletos en el catalogo:', incompletos);

// Los ejemplos son lo que el modelo copia: si uno no pasa el validador, el
// catalogo le esta ensenando a mandar comandos que nunca van a salir.
const ejemplosMalos = [];
for (const c of COMANDOS) {
  for (const ej of c.ejemplos ?? []) {
    const r = validarComando(ej);
    if (!r.ok) ejemplosMalos.push(`${c.nombre} -> "${ej}": ${r.motivo}`);
  }
}
fallo('Ejemplos del catalogo que NO pasan el validador:', ejemplosMalos);

// Y ademas la placa simulada tiene que aceptarlos de verdad: el validador solo
// comprueba la forma, no que el comando exista en la implementacion.
const placa = new PlacaSimulada();
placa.conectado = true;
const rechazadosPorLaPlaca = [];
placa.on('consola', (m) => {
  if (m.nivel === 'error') rechazadosPorLaPlaca.push(m.texto);
});
for (const c of COMANDOS) {
  for (const ej of c.ejemplos ?? []) {
    const antes = rechazadosPorLaPlaca.length;
    placa.enviar(ej);
    if (rechazadosPorLaPlaca.length > antes) {
      rechazadosPorLaPlaca[antes] = `"${ej}": ${rechazadosPorLaPlaca[antes]}`;
    }
  }
}
fallo('Ejemplos que el simulador rechaza (catalogo desincronizado):', rechazadosPorLaPlaca);

if (problemas === 0) {
  console.log('\nCatalogo y firmware estan sincronizados.');
  process.exit(0);
} else {
  console.error(`\n${problemas} problema(s) encontrado(s).`);
  process.exit(1);
}
