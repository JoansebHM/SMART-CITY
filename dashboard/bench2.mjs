import { GoogleGenAI } from '@google/genai';
import { renderCategorias } from './lib/ia/catalogo.js';
import { elegirCategorias } from './lib/ia/recuperador.js';

const ai = new GoogleGenAI({ apiKey: process.env['GEMINI_API_KEY'] });
const mensaje = 'llena todos los espacios de vehiculos de la calle 2';
const sel = elegirCategorias(mensaje);
const prompt = `Eres el controlador de una maqueta de semaforos.
Responde SOLO con {"comandos":["..."],"respuesta":"frase corta"}.
Usa unicamente estos comandos.

COMANDOS
${renderCategorias(sel.categorias)}

ORDEN: "${mensaje}"
JSON:`;

console.log('prompt ~', Math.round(prompt.length/4), 'tokens\n');

for (const nivel of ['low', undefined, 'low']) {
  const cfg = { temperature: 0.2, max_output_tokens: 2048, top_p: 0.95 };
  if (nivel) cfg.thinking_level = nivel;
  const t = Date.now();
  const r = await ai.interactions.create({ model: 'models/gemini-3-flash-preview', input: prompt, generation_config: cfg });
  let txt = '';
  for (let i=(r.steps??[]).length-1;i>=0 && !txt;i--){
    const p=r.steps[i]; txt = p?.text ?? (Array.isArray(p?.content?.parts)? p.content.parts.map(x=>x?.text??'').join(''):'');
  }
  console.log(`thinking=${nivel ?? '(sin)'}  ${((Date.now()-t)/1000).toFixed(1)}s  -> ${txt.replace(/\s+/g,' ').slice(0,80)}`);
}
