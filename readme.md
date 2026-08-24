# Documentación de Simulación — Ciudad Autoadaptable (ESP32)

Este documento sirve como guion de demostración: para cada variable y cada caso
del sistema se indica **qué hace**, **cómo provocarlo físicamente en la maqueta**
y **en qué línea exacta del código se ejecuta**, para poder mostrarlo en vivo.

> Todas las referencias de línea corresponden al archivo `SmartCity_Maqueta.ino`
> tal como está en la base de código actual.

---

## 1. Mapa de variables del sistema

### 1.1 Variables de configuración (constantes ajustables)

| Constante | Línea | Valor | Qué controla |
|---|---|---|---|
| `CNY_ACTIVO_EN_BAJO` | 110 | `true` | Si `true`, un CNY se considera "detectando" cuando su pin lee `LOW` |
| `VERDE_MINIMO` | 112 | 5000 ms | Tiempo mínimo garantizado de luz verde |
| `VERDE_MAXIMO` | 113 | 15000 ms | Tiempo máximo de verde aunque haya cola |
| `EXTENSION_POR_AUTO` | 114 | 2000 ms | Cuánto se alarga el verde por cada sensor CNY activo en esa calle |
| `TIEMPO_AMARILLO` | 115 | 3000 ms | Duración fija de la fase amarilla |
| `TIEMPO_TODO_ROJO` | 116 | 1000 ms | Pausa de seguridad con ambos semáforos en rojo |
| `UMBRAL_NOCHE` | 118 | 800 | Umbral de luz (ADC 0-4095) bajo el cual se activa el modo noche |
| `BRILLO_DIA` | 119 | 255 | Valor PWM de los LEDs en modo día |
| `BRILLO_NOCHE` | 120 | 60 | Valor PWM de los LEDs en modo noche (atenuado) |
| `UMBRAL_CO2_ALTO` | 121 | 2500 | Umbral de CO2 (ADC 0-4095) a partir del cual el aire se considera cargado |
| `DEBOUNCE_MS` | 123 | 200 ms | Antirrebote de cada botón |
| `VENTANA_COMBO` | 127 | 150 ms | Ventana para detectar que P1 y P2 se pulsaron juntos |
| `NUM_MODOS_PANTALLA` | 130 | 4 | Cantidad de pantallas disponibles en el LCD |

### 1.2 Variables de estado (cambian en tiempo real)

| Variable | Línea | Qué representa |
|---|---|---|
| `estadoActual` | 144 | Fase actual de la máquina de estados del cruce (ver sección 2) |
| `tiempoInicioFase` | 145 | Marca de tiempo (`millis()`) en que empezó la fase actual |
| `duracionVerdeCalculada` | 146 | Duración del verde vigente, ya ajustada por tráfico/CO2 |
| `solicitudPeaton1` / `solicitudPeaton2` | 151-152 | `true` mientras hay una petición de cruce pendiente de esa calle |
| `modoNoche` | 165 | `true` si el promedio de luz ambiental está por debajo del umbral |
| `co2Actual` | 166 | Última lectura cruda del sensor de CO2 |
| `luz1Actual` / `luz2Actual` | 170-171 | Última lectura cruda de LDR1 / LDR2 |
| `cny1Detecta` ... `cny6Detecta` | 172-173 | Estado booleano de cada sensor infrarrojo, actualizado cada vuelta del loop |
| `autosCalle1Actual` / `autosCalle2Actual` | 174-175 | Cantidad de sensores CNY activos (0 a 3) en cada calle |
| `modoPantalla` | 178 | Índice (0 a 3) de qué pantalla del LCD se está mostrando |

---

## 2. Máquina de estados del cruce

```
VERDE_CALLE1 → AMARILLO_CALLE1 → TODO_ROJO_1a2 →
VERDE_CALLE2 → AMARILLO_CALLE2 → TODO_ROJO_2a1 → (vuelve a VERDE_CALLE1)
```

Definida en el `enum EstadoCruce` (líneas 135-142) y ejecutada por
`actualizarMaquinaEstados()` (línea 356 en adelante), que se llama cada
vuelta del `loop()` (línea 233).

---

## 3. Casos de prueba — Tráfico (sensores CNY)

### Caso 3.1 — Verde se alarga por cola de autos
**Cómo simularlo:** cuando la Calle 1 esté en verde, tapa/activa manualmente
CNY1, CNY2 y CNY3 (con un objeto blanco) justo antes de que termine el rojo
total previo (fase `TODO_ROJO_2a1`), para que estén activos en el momento en
que se calcula el próximo verde.
**Qué debe pasar:** el verde de la Calle 1 dura más que el mínimo de 5 s
(hasta 15 s si los 3 sensores están activos).
**Línea que lo ejecuta:**
- Conteo de sensores activos: `leerSensoresDetalle()`, líneas 254-255
- Cálculo de la duración: `calcularDuracionVerde()`, línea 413-414
  (`VERDE_MINIMO + autosDetectados * EXTENSION_POR_AUTO`)
- Se invoca al cerrar `TODO_ROJO_2a1`: línea 406

### Caso 3.2 — Sin autos, el verde no se alarga
**Cómo simularlo:** deja los 3 CNY de una calle sin activar durante todo su
ciclo.
**Qué debe pasar:** el verde dura exactamente `VERDE_MINIMO` (5 s).
**Línea:** `calcularDuracionVerde()` línea 422, `if (duracion < VERDE_MINIMO) duracion = VERDE_MINIMO;`

### Caso 3.3 — Verde tope máximo
**Cómo simularlo:** activa los 3 CNY de una calle (3 × 2000 ms = 6000 ms de
extensión) sumado al mínimo de 5000 ms = 11000 ms, que ya es menor al
máximo de 15000, así que **no se alcanza a topar en condiciones normales**.
Para forzar el tope, sube temporalmente `EXTENSION_POR_AUTO` a un valor alto
(por ejemplo 6000) y recompila, o simplemente documenta que el sistema
respeta el techo de 15 s pase lo que pase.
**Línea:** `calcularDuracionVerde()` línea 421, `if (duracion > maximoPermitido) duracion = maximoPermitido;`

---

## 4. Casos de prueba — Peatones (P1 / P2)

### Caso 4.1 — Petición individual acorta el verde
**Cómo simularlo:** con la Calle 1 en verde y con cola de autos (verde
extendido), pulsa **solo P1** una vez.
**Qué debe pasar:** el verde de la Calle 1 se corta al llegar al mínimo de
5 s en vez de esperar el tiempo extendido completo.
**Línea:**
- Detección de pulsación individual: `leerBotones()`, líneas 303-309
- Aplicación del corte: `actualizarMaquinaEstados()`, caso `VERDE_CALLE1`,
  líneas 361-370 (`if (solicitudPeaton1 && limiteVerde > VERDE_MINIMO) limiteVerde = VERDE_MINIMO;`)
- La solicitud se "atiende" y se limpia al llegar a `TODO_ROJO_1a2`: línea 380

### Caso 4.2 — Petición en la calle que está en rojo
**Cómo simularlo:** con la Calle 2 en verde, pulsa **P1** (la calle que ya
está en rojo).
**Qué debe pasar:** `solicitudPeaton1` queda en `true` esperando; no pasa
nada visible hasta que la Calle 1 vuelva a tener turno de verde, momento en
el que ese verde se acortará al mínimo.
**Línea:** misma lógica del caso 4.1, solo que la bandera se guarda y se
consume en el próximo ciclo de `VERDE_CALLE1`.

---

## 5. Casos de prueba — Combo de pantallas (P1 + P2 juntos)

### Caso 5.1 — Cambiar de pantalla
**Cómo simularlo:** pulsa P1 y P2 **al mismo tiempo** (dentro de ~150 ms uno
del otro).
**Qué debe pasar:** el LCD se limpia y pasa a la siguiente pantalla
(M1→M2→M3→M4→M1...).
**Línea:**
- Detección de combo: `leerBotones()`, líneas 290-299
- Cambio de pantalla: `cambiarModoPantalla()`, línea 329

### Caso 5.2 — Pulsación no simultánea NO cambia de pantalla
**Cómo simularlo:** pulsa P1, espera más de 150 ms, y luego pulsa P2.
**Qué debe pasar:** en vez de cambiar de pantalla, se registran **dos
solicitudes peatonales independientes** (una por calle).
**Línea:** la ventana de 150 ms se evalúa en línea 294
(`if (diferencia <= VENTANA_COMBO)`); si no se cumple, cada botón cae en su
rama individual (líneas 303 y 311).

---

## 6. Casos de prueba — Luz ambiental (LDR1 / LDR2) y modo noche

### Caso 6.1 — Activar modo noche
**Cómo simularlo:** cubre ambos LDR (LDR1 y LDR2) con la mano o un objeto
opaco para simular oscuridad.
**Qué debe pasar:** `modoNoche` pasa a `true`, y los LEDs de ambos semáforos
bajan su brillo (PWM de 255 a 60).
**Línea:**
- Cálculo de modo noche: `actualizarModoNoche()`, líneas 339-341
- Aplicación del brillo reducido: `escribirLuz()`, línea 472
  (`int brillo = modoNoche ? BRILLO_NOCHE : BRILLO_DIA;`)

### Caso 6.2 — Volver a modo día
**Cómo simularlo:** destapa los LDR y exponlos a luz normal.
**Qué debe pasar:** `modoNoche` vuelve a `false` y los LEDs recuperan brillo
completo en el siguiente cambio de fase (los LEDs solo se reescriben cuando
cambia el estado del semáforo, en `aplicarSemaforos()`).
**Nota para la demo:** si tapas y destapas el LDR sin que el semáforo
cambie de fase, el brillo no se actualiza en vivo porque `escribirLuz()`
solo se llama desde `aplicarSemaforos()` (línea 435), que a su vez solo se
dispara en cada `cambiarEstado()` (línea 426). Para ver el cambio de brillo
inmediatamente, hay que esperar al siguiente cambio de fase del semáforo.

---

## 7. Casos de prueba — Calidad de aire (CO2)

### Caso 7.1 — CO2 alto reduce el verde máximo
**Cómo simularlo:** acerca una fuente de CO2 real (por ejemplo, exhala cerca
del sensor) hasta que la lectura cruda supere 2500 (puedes verificarlo en la
pantalla M4 o por el monitor Serial).
**Qué debe pasar:** aunque haya cola de autos en las 3 CNY de una calle, el
verde ya no llega hasta 15 s; su tope baja a `VERDE_MINIMO + EXTENSION_POR_AUTO*2` = 9 s.
**Línea:** `calcularDuracionVerde()`, líneas 416-419
(`if (co2Actual >= UMBRAL_CO2_ALTO) maximoPermitido = VERDE_MINIMO + (EXTENSION_POR_AUTO * 2);`)

### Caso 7.2 — CO2 normal
**Cómo simularlo:** deja el sensor en aire ambiente normal.
**Qué debe pasar:** el tope de verde vuelve a ser el máximo normal de 15 s.
**Línea:** misma función, rama `else` implícita (`maximoPermitido = VERDE_MAXIMO`, línea 416).

---

## 8. Caso de prueba — Arranque del sistema

### Caso 8.1 — Estado inicial seguro
**Cómo simularlo:** reinicia el ESP32 (botón RESET o power cycle).
**Qué debe pasar:** el LCD muestra el mensaje de bienvenida 1.5 s, luego el
cruce arranca siempre en `VERDE_CALLE1` con duración mínima, sin depender de
lecturas previas.
**Línea:** `setup()`, líneas 213-222 (mensaje LCD y arranque en
`VERDE_CALLE1` con `duracionVerdeCalculada = VERDE_MINIMO`).

---

## 9. Las 4 pantallas del LCD (16x4)

Se navega entre ellas con el combo P1+P2 (caso 5.1). Función que decide cuál
dibujar: `actualizarLCD()`, línea 481, `switch(modoPantalla)` línea 486.

### Pantalla M1 — Resumen general
Función: `dibujarPantallaResumen()`, línea 555.

| Fila | Contenido | Ejemplo |
|---|---|---|
| 0 | Fase corta de cada calle (VER/AMA/ROJ) + indicador de pantalla | `C1:VER C2:ROJ   M1` |
| 1 | Segundos restantes de la fase actual + estado del CO2 | `Resta:4s CO2:OK` |
| 2 | Cantidad de autos detectados en cada calle | `Autos C1:2 C2:0` |
| 3 | Si es de noche + si hay alguna petición peatonal pendiente | `Noche:NO Ped:SI` |

### Pantalla M2 — Detalle Calle 1
Función: `dibujarPantallaCalle1()`, línea 563.

| Fila | Contenido | Ejemplo |
|---|---|---|
| 0 | Título + indicador de pantalla | `--CALLE 1--   M2` |
| 1 | Valor crudo de LDR1 + si es de día o de noche | `LDR1:612 DIA` |
| 2 | Estado individual de CNY1, CNY2, CNY3 (`C`=carro, `_`=libre) | `S1:C S2:_ S3:C` |
| 3 | Total de autos en esta calle + si P1 tiene solicitud pendiente | `Autos:2 Bot:NO` |

### Pantalla M3 — Detalle Calle 2
Función: `dibujarPantallaCalle2()`, línea 573. Igual que M2 pero con
LDR2 y CNY4/CNY5/CNY6.

| Fila | Contenido | Ejemplo |
|---|---|---|
| 0 | Título + indicador de pantalla | `--CALLE 2--   M3` |
| 1 | Valor crudo de LDR2 + día/noche | `LDR2:590 DIA` |
| 2 | Estado individual de CNY4, CNY5, CNY6 | `S4:_ S5:_ S6:C` |
| 3 | Total de autos + estado de P2 | `Autos:1 Bot:SI` |

### Pantalla M4 — Sistema / Ambiente
Función: `dibujarPantallaSistema()`, línea 583.

| Fila | Contenido | Ejemplo |
|---|---|---|
| 0 | Título + indicador de pantalla | `--SISTEMA--   M4` |
| 1 | Valor crudo del sensor de CO2 | `CO2 crudo:1340` |
| 2 | Valores crudos de ambos LDR juntos | `Luz1:612 Luz2:590` |
| 3 | Nombre completo de la fase actual + segundos restantes | `Verde C1 4s` |

---

## 10. Guion sugerido de demostración (orden recomendado)

1. **Arranque:** muestra el reinicio y el mensaje de bienvenida (caso 8.1).
2. **Ciclo normal:** deja correr un ciclo completo sin tocar nada, mostrando
   pantalla M1 para ver la alternancia de fases.
3. **Tráfico:** activa los 3 CNY de la calle que está por entrar en verde
   (caso 3.1), cambia a pantalla M2/M3 con el combo para mostrar el detalle
   sensor por sensor mientras el verde se extiende.
4. **Peatón:** pulsa un botón individual con la calle en verde extendido
   (caso 4.1) y muestra cómo se corta antes.
5. **Combo de pantallas:** demuestra el cambio de pantalla con P1+P2 juntos
   (caso 5.1) y luego demuestra que presionarlos por separado NO cambia de
   pantalla sino que registra peticiones peatonales (caso 5.2).
6. **Noche:** tapa los LDR (caso 6.1) y espera al siguiente cambio de fase
   para mostrar la atenuación de brillo.
7. **CO2:** exhala cerca del sensor (caso 7.1) y muestra en pantalla M4 cómo
   sube el valor crudo, y en el siguiente ciclo cómo el verde máximo se
   reduce aunque haya cola de autos.
