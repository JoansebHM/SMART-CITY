# Control por lenguaje natural

Le hablas normal y la IA traduce lo que dices a comandos de la maqueta.

> «Viene una ambulancia con urgencia por la calle 10»
> → `prio c1 60`, `lcd texto AMBULANCIA CALLE 1`

> «Ya pasó, vuelve a la normalidad»
> → `prio off`

---

## 1. Lo único que tienes que hacer tú

```bash
cd dashboard
cp .env.ejemplo .env
```

Abre `.env` y pega tu clave de Gemini (se saca en
[aistudio.google.com/apikey](https://aistudio.google.com/apikey)):

```
GEMINI_API_KEY=tu_clave_aqui
```

Arranca normal con `npm start`. El panel del chat te dirá **«Gemini conectado»**
cuando la clave esté puesta.

Sin clave el sistema **no se cae**: el chat avisa que falta y todo lo demás
(botones, consola, telemetría, gráficas) sigue funcionando igual.

---

## 2. Cómo funciona

```
tu mensaje
    ↓
recuperador   elige 4-5 categorías del catálogo según lo que dijiste
    ↓
contexto      resumen corto del estado real + tus últimas 6 órdenes
    ↓
Gemini        recibe solo eso y devuelve un JSON con los comandos
    ↓
validador     cada comando se verifica contra el catálogo
    ↓
serial        los que pasan se envían a la placa, en orden
```

**La IA nunca ve el código fuente.** Solo ve
[`lib/ia/catalogo.js`](lib/ia/catalogo.js), que es el espejo legible de
`SmartCity_Maqueta/Comandos.ino`.

---

## 3. Por qué es rápido

El catálogo completo son ~1500 tokens. Mandarlo entero en cada mensaje es
desperdiciar latencia, así que el **recuperador** puntúa tu frase contra las
palabras clave de cada categoría y solo mete al prompt las que importan:

| Lo que dices | Categorías que entran | Tokens |
|---|---|---|
| «viene una ambulancia por la calle 10» | sistema, semáforo, emergencia | ~480 |
| «bájale la intensidad a las luces» | sistema, semáforo, luces | ~590 |
| «simula hora pico» | sistema, semáforo, escenarios, sensores | ~580 |
| *(catálogo completo, sin recuperador)* | todas | ~1480 |

Aproximadamente **un tercio** del prompt. Y todo esto es local: no cuesta
ninguna llamada extra al modelo.

La otra palanca es `GEMINI_PENSAMIENTO=low` en el `.env`. Traducir una orden a
un comando no necesita razonamiento profundo. Súbelo a `high` solo si le vas a
pedir cosas con varios pasos encadenados.

### El modelo importa mucho

Medido con el prompt real de este proyecto:

| Modelo | Latencia |
|---|---|
| `models/gemini-3.8-flash` | **~4 s** ← el que usa por defecto |
| `models/gemini-3.5-flash-lite` | ~27 s |
| `models/gemini-3-flash-preview` | ~37 s, a veces minutos |

**Evita los modelos `preview`**: su latencia es impredecible. Para ver qué
modelos tiene tu cuenta, `ai.models.list()`.

### Cuota del plan gratuito

El plan gratuito de Gemini permite unas **20 peticiones diarias por modelo**.
Al agotarse, la API responde 429 y el SDK se queda reintentando por dentro, lo
que se siente como una lentitud enorme. Por eso hay un `GEMINI_TIMEOUT_MS`
(30 s por defecto): corta la espera y te avisa en el chat en vez de dejarlo
colgado.

La cuota se renueva sola. Cada modelo tiene su propio contador, así que cambiar
de modelo en `.env` también te da cuota nueva.

---

## 4. La memoria

La IA recuerda tus **últimas 6 órdenes** con los comandos que mandó. Eso le
permite entender «ya pasó» o «devuélvelo como estaba».

Pero lo que de verdad la mantiene orientada no es la memoria, sino el **resumen
del estado real** que se arma de la telemetría en cada mensaje:

```
- Fase: Verde C1 (MANUAL)
- PRIORIDAD ACTIVA en Calle 1 (43 s restantes)
- Autos detectados: Calle 1 = 2, Calle 2 = 0
- Ambiente: dia, CO2 normal (1340)
- Brillo maestro reducido a 120/255
- El LCD muestra el mensaje: "AMBULANCIA CALLE 1"
```

Esto viene de la placa, no de lo que la IA cree recordar. Si algo se restauró
solo (por el watchdog, por ejemplo), aquí se refleja y la IA no se confunde.

El botón **«olvidar contexto»** borra la memoria conversacional.

---

## 5. Las tres garantías de seguridad

Estas viven en el **firmware**, no en la IA. Da igual lo convencido que esté el
modelo: si el comando no pasa, no pasa.

**1. Validador.** Ningún comando llega al puerto serial sin verificarse contra
el catálogo: que exista, y que sus argumentos estén en rango. Si el modelo
inventa `fase v9` o `rm -rf /`, se rechaza y se muestra tachado en el chat.

**2. Interlock (`seguro on`).** El firmware nunca deja las dos calles en verde
al mismo tiempo, ni siquiera si le mandas `led lg1 on` y `led lg2 on` seguidos.
Está activo por defecto.

**3. Watchdog.** Toda prioridad con duración arma un temporizador de respaldo.
Si nadie lo renueva —se cayó el WiFi, cerraste el navegador, se te olvidó decir
que la ambulancia pasó— la maqueta se restaura sola. Solo `prio c1 inf` no
vence, porque ahí lo pediste explícitamente.

---

## 6. Qué se ve en el chat

Cada respuesta muestra los comandos que la IA decidió mandar, cuánto tardó y
qué categorías del catálogo consultó. Es a propósito: en una sustentación vale
mucho más que se vea *«entendí ambulancia → consulté emergencia → mandé
`prio c1 60`»* que una caja negra que simplemente funciona.

Los comandos rechazados salen tachados en rojo, con el motivo al pasar el mouse.

Todo queda guardado en la tabla `conversaciones` de SQLite:
`GET /api/conversaciones`.

---

## 7. Enseñarle los nombres de tus calles

La maqueta solo conoce `c1` y `c2`. Para hablarle con los nombres reales, edita
`ALIAS_CALLES` al final de [`lib/ia/catalogo.js`](lib/ia/catalogo.js):

```js
export const ALIAS_CALLES = {
  c1: ['calle 1', 'calle 10', 'horizontal', 'principal'],
  c2: ['calle 2', 'carrera 43', 'vertical', 'secundaria']
};
```

Gana siempre el alias más largo que aparezca, así que «calle 10» no se confunde
con «calle 1».

---

## 8. Agregar un comando nuevo

Son dos archivos, siempre en este orden:

1. **`SmartCity_Maqueta/Comandos.ino`** — la implementación real y su entrada en
   el enrutador (`if (cmd == "loquesea")`).
2. **`dashboard/lib/ia/catalogo.js`** — su entrada en `COMANDOS`, con
   `sintaxis`, `que`, `args` y `ejemplos`.

Después verifica que no se te olvidó ninguno de los dos:

```bash
npm run verificar-catalogo
```

Te avisa si un comando existe en el firmware pero no en el catálogo (la IA
nunca lo usaría) o al revés (la IA mandaría algo que la placa rechaza).

---

## 9. Si el chat no responde bien

| Síntoma | Qué mirar |
|---|---|
| «falta GEMINI_API_KEY» | No creaste `.env` o está vacío |
| Manda un comando equivocado | Revisa el `que:` de ese comando en el catálogo — es lo que lee el modelo |
| No entiende una palabra tuya | Agrégala a las `claves` de esa categoría en el catálogo |
| Confunde las calles | Ajusta `ALIAS_CALLES` |
| Va lento | Casi siempre es cuota agotada, no el modelo. Mira la tabla de arriba |
| «Se acabó la cuota gratuita» | Espera a que se renueve, o cambia `GEMINI_MODELO` |
| «no respondió en 30 s» | Cuota agotada o modelo `preview`. Usa `models/gemini-3.8-flash` |
| Comandos tachados en rojo | El modelo inventó sintaxis; el motivo sale al pasar el mouse |
