# Manual de la consola serial — Ciudad Autoadaptable

Con esta versión del código puedes controlar **todos** los sensores y actuadores
de la maqueta escribiendo comandos en el Monitor Serie, sin tener que tapar
sensores con la mano ni pasar carritos por encima.

---

## 1. Cómo conectarte

1. Sube `SmartCity_Maqueta/SmartCity_Maqueta.ino` a la placa.
2. Abre el **Monitor Serie** del Arduino IDE.
3. Configúralo así:
   - Velocidad: **115200 baudios**
   - Final de línea: **Nueva línea** (o "Ambos NL y CR")
4. Escribe `ayuda` y presiona Enter.

Reglas simples:
- Un comando por línea.
- No importan las mayúsculas.
- Todo lo que respondes el sistema empieza con `[OK]`, `[ERROR]` o `[EVENTO]`.

---

## 2. Idea clave: AUTO vs SIMULADO

Cada sensor tiene dos modos:

| Modo | Qué hace |
|---|---|
| **AUTO** | Lee el sensor real de la maqueta (es como venía antes). |
| **SIMULADO** | Ignora el sensor real y usa el valor que tú escribiste. |

Todo arranca en AUTO. En cuanto le das un valor por consola, ese sensor queda
simulado hasta que le digas `auto` otra vez.

Para devolver **todo** a la normalidad de un solo golpe:

```
reset
```

---

## 3. Los dos comandos que más vas a usar

```
ayuda      → lista de todos los comandos
estado     → foto completa del sistema en este momento
```

`estado` te muestra la fase del semáforo, los 6 sensores CNY, los LDR, el CO2,
los peatones pendientes, los LEDs y los tiempos. Los valores con `*` son los que
están simulados.

---

## 4. Simular carros (sensores CNY)

Los CNY detectan carros. `on` = hay un carro encima, `off` = vía libre.

```
cny 1 on          enciende el sensor CNY1
cny 4 off         apaga el sensor CNY4
cny 2 auto        CNY2 vuelve a leer el sensor real
cny c1 on         los 3 sensores de la Calle 1 (CNY1,2,3)
cny c2 on         los 3 sensores de la Calle 2 (CNY4,5,6)
cny all auto      los 6 vuelven a modo real
```

**Qué debe pasar:** entre más sensores `on` tenga una calle, más largo será su
verde (`5 s` mínimo + `2 s` por cada sensor activo).

---

## 5. Simular luz ambiente (LDR) y modo noche

Los LDR leen de 0 (oscuro) a 4095 (muy iluminado).

```
ldr 1 100         LDR1 muy oscuro
ldr 2 3000        LDR2 con mucha luz
ldr all 100       los dos oscuros
ldr all auto      vuelven a los sensores reales
```

O saltarte los LDR y forzar el resultado directamente:

```
noche on          fuerza modo noche
noche off         fuerza modo día
noche auto        que lo decidan los LDR
```

**Qué debe pasar:** el indicador de la pantalla pasa de `DIA` a `NOC` y la
telemetría marca `noche: true`.

> ⚠️ **El modo noche ya NO atenúa los LEDs.** Antes, tapar los dos sensores
> bajaba el brillo de todos los semáforos (PWM 255 → 60); esa atenuación se
> quitó a propósito. Hoy `modoNoche` sirve solo como indicador y como una de
> las dos condiciones de la noche profunda. `set brillonoche` se sigue
> aceptando por compatibilidad, pero no tiene ningún efecto: para atenuar de
> verdad usa el comando `brillo`.

---

## 5-bis. Hora, WiFi y envío de datos

La maqueta se conecta al WiFi al arrancar y pide la hora real en zona
**UTC-5**. Esa hora siembra un reloj interno que a partir de ahí **avanza
solo**, sin volver a consultar internet.

De dónde saca la hora, en orden:

1. **NTP** (`pool.ntp.org`) — el método principal. Es el protocolo hecho para
   esto: sin TLS, sin parsear nada, funciona en casi cualquier red.
2. **API HTTP** (`worldclockapi.com`) — solo si el puerto UDP 123 está
   bloqueado en esa red. Devuelve UTC sin segundos, así que se le restan 5 h.
3. **A mano** con `hora <0-23>`, si no hay internet.

> Si la hora aparece como `--:--:--`, mira el Monitor Serie: cada intento
> deja una línea `[HORA] ...` diciendo exactamente qué falló.

```
red               estado del WiFi, del reloj y de los envíos
red off           apaga la parte de red (sigue todo lo demás)
red on            la vuelve a encender
hora              qué hora tiene el reloj ahora mismo
hora 23           fuerza la hora a mano (para la demo)
resync            vuelve a la HORA REAL de internet
hora off          deja el reloj sin hora
```

**Para deshacer una hora forzada en una prueba:** `resync`. Es el comando que
devuelve el reloj a la hora real. También responde a `hora sync`, `hora real`
y `hora ahora`, y en el dashboard es el botón **"Hora real"**.

> No lo confundas con `hora off`: ese no restaura nada, deja el reloj *sin*
> hora (y por tanto desactiva la noche profunda).

En el dashboard, la insignia muestra de dónde salió la hora — `14:37 · NTP`
frente a `23:00 · MANUAL` — para que se vea de un vistazo si estás mirando la
hora real o una forzada.

**Dónde verlo:** pantalla **M5** del LCD (`pantalla 5`), que muestra
`HH:MM:SS`, el origen de la hora (API / NTP / MANUAL), la IP y el contador de
envíos. La hora en formato corto también sale en la pantalla M1.

**Envío de datos:** cada 5 segundos se manda un POST a
`grupo1.requestcatcher.com/post` con el número de vehículos de cada calle,
tanto en la query string como en un cuerpo JSON con el detalle. Ábrelo en el
navegador y activa sensores CNY para verlo llegar en vivo.

> El reloj avanza solo, así que `hora 23` no fija la madrugada para siempre:
> si dejas correr la maqueta, acabará saliendo de la franja 23h-4h igual que
> en la vida real. Para la demo de noche profunda, usa `escenario madrugada`.

> La red **nunca bloquea el cruce**: todo el HTTP corre en una tarea aparte,
> en el otro núcleo del ESP32-S3. Si no hay WiFi, la maqueta funciona igual y
> reintenta por su cuenta.

---

## 6. Simular calidad del aire (CO2)

```
co2 alto          pone el CO2 por encima del umbral
co2 bajo          aire limpio
co2 3000          un valor exacto (0 a 4095)
co2 auto          vuelve al sensor real
```

**Qué debe pasar:** con CO2 alto, el verde máximo se recorta (aunque haya cola
de carros) para que el tráfico rote más rápido.

---

## 7. Peatones y pantalla LCD

```
p1                simula que un peatón pulsó el botón de la Calle 1
p2                lo mismo para la Calle 2
combo             pasa a la siguiente pantalla del LCD
pantalla 3        salta directo a la pantalla M3
lcd off           apaga la pantalla
lcd on            la vuelve a encender
botones off       ignora los pulsadores físicos (solo consola)
botones on        los vuelve a habilitar
```

**Qué debe pasar con `p1`/`p2`:** si esa calle está en verde extendido, el verde
se corta al mínimo de 5 s.

Los botones físicos siguen funcionando igual que antes: uno solo = peatón,
los dos al tiempo = cambiar pantalla.

---

## 8. Controlar el semáforo a mano

Por defecto el semáforo corre solo. Puedes pausarlo:

```
sem manual        congela el ciclo automático
sem auto          lo vuelve a soltar
```

Con el ciclo pausado, mueves las fases tú:

```
fase v1           Verde Calle 1
fase a1           Amarillo Calle 1
fase r1           Todo rojo (pasando de Calle 1 a Calle 2)
fase v2           Verde Calle 2
fase a2           Amarillo Calle 2
fase r2           Todo rojo (pasando de Calle 2 a Calle 1)
fase sig          avanza a la siguiente fase del ciclo
```

---

## 9. Controlar los LEDs directamente

Nombres de los LEDs: `lr1 ly1 lg1` (Semáforo 1) y `lr2 ly2 lg2` (Semáforo 2).
La letra del medio es el color: **r**ojo, **y** amarillo, **g** verde.

```
led lg1 on        enciende el verde del Semáforo 1 y lo deja fijo
led lr2 off       apaga el rojo del Semáforo 2
led ly1 120       brillo intermedio (PWM 0-255)
led lg1 auto      vuelve a obedecer al semáforo
leds off          apaga los 6 LEDs
leds auto         los 6 vuelven a obedecer al semáforo
```

> Ojo: un LED forzado ya no responde a la máquina de estados hasta que le pongas
> `auto`. Sirve para probar que cada LED funciona.

---

## 10. Cambiar tiempos y umbrales sin recompilar

```
set verdemin 3000       verde mínimo (ms)
set verdemax 20000      verde máximo (ms)
set extension 4000      cuánto suma cada carro detectado (ms)
set amarillo 2000       duración del amarillo (ms)
set todorojo 500        pausa de seguridad en rojo (ms)

set umbralnoche 1200    a partir de qué luz se considera noche
set umbralco2 2000      a partir de qué valor el aire es "alto"
set brillodia 255       PWM de los LEDs (el único que se aplica)
set brillonoche 30      SIN EFECTO: la atenuación nocturna está desactivada
set cnybajo 0           invierte la lógica de los CNY (0 o 1)
```

Útil para la demo: baja los tiempos para que un ciclo completo dure pocos
segundos y se vea todo rápido.

---

## 11. Ver los datos en vivo por Serial

```
mon on            imprime una línea de telemetría cada segundo
mon 300           cada 300 ms
mon off           apaga la telemetría
mon texto         formato legible (el de abajo)
mon json          formato JSON, el que consume el dashboard web
json              imprime un solo snapshot JSON del estado completo
```

La línea se ve así:

```
[MON] fase=Verde C1 t=4s cny=110000 autos=2/0 ldr=612,590 co2=1340 noche=NO ped=--
```

- `cny=110000` → estado de CNY1..CNY6 (1 = detectando).
- `autos=2/0` → carros en Calle 1 / Calle 2.
- `ped=1-` → hay peatón pendiente en la Calle 1.

> Con `mon json` la misma información sale como una línea JSON por muestra.
> Toda línea que empieza por `{` es telemetría estructurada; cualquier otra
> línea es texto de consola. Eso es lo que usa el dashboard web
> (ver [`dashboard/README.md`](dashboard/README.md)).

---

## 12. Escenarios rápidos (un solo comando)

Atajos armados para la demostración:

```
escenario trafico1        cola en la Calle 1, Calle 2 vacía
escenario trafico2        cola en la Calle 2, Calle 1 vacía
escenario noche           LDR oscuros, LEDs atenuados
escenario dia             LDR iluminados, LEDs a full
escenario contaminacion   CO2 alto + cola en las dos calles
escenario vacio           sin carros y aire limpio (verde mínimo)
```

---

## 12-bis. Comandos avanzados

Estos son los que hacen posible el control por IA. Viven en
[`SmartCity_Maqueta/Comandos.ino`](SmartCity_Maqueta/Comandos.ino).

### Emergencias

```
prio c1 60          da paso prioritario a la Calle 1 durante 60 segundos
prio c2 inf         igual pero indefinido, hasta que lo canceles
prio off            termina la prioridad y restaura TODO como estaba antes
panico              las dos calles en rojo, ciclo detenido
apagar              apaga los seis LEDs
```

`prio` guarda el estado completo antes de anular nada, así que `prio off`
devuelve el sistema **exactamente** como estaba, incluidas simulaciones,
brillo y tiempos.

### Luces y efectos

```
brillo 40%          atenúa TODOS los LEDs al 40%
brillo 255          brillo pleno
parpadeo all 400    los seis LEDs parpadean cada 400 ms
parpadeo sem1 off   deja de parpadear el semáforo 1
fade lg1 0 2000     baja el verde 1 hasta apagarse en 2 segundos
```

> **`parpadeo` no enciende LEDs.** Solo los apaga a intervalos: alterna el LED
> entre el brillo que ya tiene y cero. Sobre un LED apagado (después de
> `leds off`, `apagar` o `led x off`) no se ve absolutamente nada, aunque la
> consola conteste `[OK]`. Enciéndelo primero:
>
> ```
> led ly1 on ; parpadeo ly1 500
> ```
>
> Para el amarillo intermitente de ambas calles ya existe `escenario
> intermitente`, que enciende los dos amarillos y los pone a parpadear.

> **`led`, `leds`, `fade` y `apagar` dejan el LED fijo.** A partir de ahí ese
> LED ignora el ciclo del semáforo, aunque las fases sigan avanzando por
> dentro. Se devuelve con `led <x> auto`, `leds auto` o `reset`.

### Tráfico dinámico

```
flujo c1 30         llegan ~30 autos por minuto a la Calle 1, solos
flujo off           corta el flujo automático
ruido on            lecturas con pequeñas fluctuaciones realistas
```

### Instantáneas y seguridad

```
guardar 1           guarda todo el estado en el slot 1
restaurar 1         lo devuelve exactamente
watchdog 120        si nadie renueva, en 2 min se restaura solo
watchdog off        desarma el watchdog
seguro on           interlock: NUNCA dos verdes al tiempo (por defecto)
seguro off          permite verdes simultáneos (solo para probar LEDs)
```

### Acciones diferidas

```
en 30000 prio off               dentro de 30 s termina la prioridad
secuencia leds off ; espera 500 ; leds auto
cancelar                        cancela lo programado que no se ha ejecutado
```

### Pantalla y diagnóstico

```
lcd texto AMBULANCIA CALLE 1    mensaje libre en el LCD de la maqueta
lcd limpiar                     vuelve a las pantallas normales
test leds                       barrido encendiendo los 6 LEDs uno a uno
test sensores                   imprime las lecturas crudas de todos los pines
```

### Escenarios nuevos

```
escenario intermitente          amarillo parpadeante en ambas (madrugada)
escenario horapico              tráfico continuo en las dos calles
```


---

## 13. Guion sugerido de demostración

```
1)  reset                     → arranca limpio
2)  mon on                    → que se vean los datos corriendo
3)  set verdemin 3000         → ciclos más cortos para la demo
4)  escenario vacio           → muestra el verde mínimo
5)  escenario trafico1        → muestra cómo el verde de C1 se alarga
6)  p1                        → el peatón corta el verde al mínimo
7)  escenario contaminacion   → el CO2 alto recorta el verde aunque haya cola
8)  escenario noche           → los LEDs se atenúan de inmediato
9)  pantalla 2 / pantalla 4   → recorre las pantallas del LCD
10) sem manual + fase v2      → mueve el semáforo a mano
11) led lg1 on                → prueba de LED individual
12) reset                     → todo vuelve a los sensores reales
```

---

## 14. Tabla resumen de comandos

| Comando | Para qué |
|---|---|
| `ayuda` | Lista de comandos |
| `estado` | Estado completo del sistema |
| `reset` | Todo vuelve a AUTO |
| `cny <1-6\|c1\|c2\|all> <on\|off\|auto>` | Simular carros |
| `ldr <1\|2\|all> <0-4095\|auto>` | Simular luz |
| `noche <on\|off\|auto>` | Forzar día o noche |
| `co2 <0-4095\|alto\|bajo\|auto>` | Simular calidad del aire |
| `p1` / `p2` | Simular botón peatonal |
| `combo` / `pantalla <1-5>` | Cambiar pantalla LCD (M5 = red/hora) |
| `lcd <on\|off>` | Encender/apagar LCD |
| `botones <on\|off>` | Habilitar/ignorar pulsadores reales |
| `sem <auto\|manual>` | Ciclo automático o manual |
| `fase <v1\|a1\|r1\|v2\|a2\|r2\|sig>` | Saltar a una fase |
| `led <lr1..lg2> <on\|off\|0-255\|auto>` | Controlar un LED |
| `leds <auto\|on\|off>` | Controlar los 6 LEDs |
| `set <parametro> <valor>` | Cambiar tiempos y umbrales |
| `mon <on\|off\|json\|texto\|ms>` | Telemetría continua |
| `json` | Un snapshot JSON del estado |
| `escenario <nombre>` | Escenarios armados |
| `red` / `red <on\|off>` | WiFi, reloj UTC-5 y telemetría |
| `hora <0-23\|sync\|off>` / `resync` | Poner en hora el reloj interno |

---

## 15. Si algo no responde

### El Monitor Serie no muestra NADA (ESP32-S3)

Esta es la causa #1 en el ESP32-S3 Dev Module. La placa tiene **dos puertos USB**
y el `Serial` del código puede estar saliendo por el que no estás mirando.

En el Arduino IDE, menú **Herramientas**, revisa:

| Opción | Valor |
|---|---|
| **USB CDC On Boot** | **Enabled** |
| USB Mode | USB-OTG (TinyUSB) |
| Upload Mode | UART0 / Hardware CDC |

Con `USB CDC On Boot: Disabled` (que es el valor por defecto en varias versiones
del core), `Serial` sale por los pines UART0 y **no** por el USB nativo — por eso
no ves nada aunque el programa esté corriendo bien.

Después de cambiarlo: **vuelve a subir el sketch** (no basta con reiniciar),
cierra y reabre el Monitor Serie, y vuelve a escoger el puerto en Herramientas.

Si tu cable está en el puerto marcado **UART / COM** (el del chip CP2102 o
CH340), entonces déjalo en `Disabled`. La regla es: puerto **USB** nativo →
`Enabled`; puerto **UART** → `Disabled`.

### Otras causas

- **Ves el banner de arranque pero los comandos no responden:** era el salto de
  línea. Ya está resuelto en el código (ejecuta el comando aunque el Monitor esté
  en "Sin ajuste de línea"), pero lo ideal sigue siendo dejarlo en "Nueva línea".
- **No ves el banner pero sí responde a comandos:** el mensaje se imprimió
  mientras el puerto USB se reconectaba. Escribe `estado` y sigue normal.
- **Elegiste mal la placa:** debe ser *ESP32S3 Dev Module*, no *ESP32 Dev Module*.
- **Velocidad:** 115200 baudios.
- **Un sensor no cambia con la mano:** probablemente lo dejaste simulado.
  Escribe `estado` y busca los `*`, o corre `reset`.
- **Un LED se quedó pegado:** está forzado. Usa `leds auto`.
- **El semáforo no avanza:** está en modo manual. Usa `sem auto`.
- **Los CNY detectan al revés:** prueba `set cnybajo 0`.
