# Dashboard web — Ciudad Autoadaptable

Panel de control en el navegador para la maqueta. Muestra el cruce en vivo y
permite disparar todas las simulaciones con botones, en vez de escribir
comandos en el Monitor Serie.

```
ESP32-S3 ──USB──> servidor Node ──WebSocket──> navegador
                       │
                       └──> SQLite (datos/ciudad.db)
```

No usa WiFi. Eso es a propósito: los sensores LDR1, LDR2 y CO2 están en pines
del ADC2, que deja de funcionar cuando el WiFi del ESP32 está activo. Al pasar
los datos por USB, esas tres lecturas siguen siendo reales.

---

## 1. Requisitos

- **Node.js 22.5 o superior** (usa `node:sqlite`, que viene incluido).
  Verifica con `node --version`.
- La placa con el sketch **actualizado** (el que trae el comando `mon json`).

---

## 2. Puesta en marcha

```bash
cd dashboard
npm install
npm start
```

Abre **http://localhost:3000**.

> **Antes de arrancar, cierra el Monitor Serie del Arduino IDE.** Un puerto
> serial solo admite un programa a la vez. Si está abierto verás "el puerto
> está ocupado", pero **no hace falta reiniciar el servidor**: reintenta cada
> 2 segundos y entra solo en cuanto cierres el monitor.

### Sin la placa conectada

```bash
npm run simular
```

Levanta una placa simulada que reproduce la misma lógica y los mismos comandos.
Sirve para preparar la demo o como plan B si el USB falla.

### Otras opciones

```bash
npm run puertos                       lista los puertos seriales detectados
npm start -- --puerto /dev/cu.usbmodem11201    fuerza un puerto
npm start -- --http 8080              cambia el puerto web
npm start -- --periodo 100            telemetría más rápida (ms)
npm start -- --guardar-cada 1000      guarda en la base con menos frecuencia
```

---

## 3. Qué muestra el panel

| Zona | Qué contiene |
|---|---|
| **Vista del cruce** | Los dos semáforos con su brillo real (se ve el modo noche), carritos sobre los CNY activos, botones peatonales y sensores de ambiente. El borde morado punteado marca lo que está **simulado**. |
| **Fase actual** | Fase, cuenta regresiva y verde calculado. Avisa si el semáforo está en manual. |
| **Tráfico** | Autos por calle y el estado de los 6 sensores CNY. |
| **Ambiente** | Barras de CO₂ y de los dos LDR, con insignias de noche / CO₂ alto / peatón. |
| **Histórico** | CO₂, autos y luz de los últimos minutos. |
| **Panel de simulación** | Los botones. Cada uno manda un comando de texto (pasa el mouse por encima para verlo). |
| **Consola** | Las respuestas de la placa tal como salen por Serial. |

---

## 4. Persistencia

Todo queda en `datos/ciudad.db` (SQLite). Cada arranque del servidor abre una
**sesión** nueva, así no se mezclan las corridas.

Tres tablas: `telemetria` (una fila por muestra), `eventos` (mensajes de la
placa) y `comandos` (qué se envió, cuándo y desde dónde).

Para el informe del proyecto, el botón **Exportar CSV** de la barra superior
baja toda la telemetría de la sesión actual.

---

## 5. API HTTP

Además del WebSocket, el servidor expone:

| Ruta | Qué devuelve |
|---|---|
| `GET /api/estado` | Estado del enlace y última telemetría |
| `GET /api/historial?limite=300` | Historial de la sesión actual |
| `GET /api/eventos?limite=200` | Mensajes de la placa |
| `GET /api/comandos?limite=200` | Comandos enviados |
| `GET /api/sesiones` | Todas las sesiones grabadas |
| `GET /api/resumen` | Promedios y máximos de la sesión |
| `GET /api/exportar.csv` | Descarga la telemetría en CSV |
| `POST /api/comando` | Envía un comando (`{"comando":"cny 1 on"}`) |

Ejemplo:

```bash
curl -X POST http://localhost:3000/api/comando \
     -H 'Content-Type: application/json' \
     -d '{"comando":"escenario noche"}'
```

---

## 6. Verlo desde el celular

El servidor escucha en toda la red. Averigua la IP del portátil
(`ipconfig getifaddr en0` en macOS) y abre `http://ESA_IP:3000` desde el
celular, con los dos en la misma red.

---

## 7. Si algo falla

| Síntoma | Causa |
|---|---|
| "El puerto está ocupado" | El Monitor Serie del Arduino IDE está abierto. Ciérralo y el puente entra solo. Para ver qué proceso lo tiene: `lsof /dev/cu.usbmodem*` |
| "No se encontró ninguna placa" | Revisa el cable USB y corre `npm run puertos`. |
| Conecta pero no llegan datos | La placa tiene el sketch viejo. Sube el que trae `mon json`. |
| Todo en cero y sin consola | Revisa `USB CDC On Boot: Enabled` (ver `COMANDOS.md`). |
| `node:sqlite` no existe | Node muy viejo. Necesitas 22.5+. |

---

## 8. Estructura

```
dashboard/
├── server.js              puente serial↔WebSocket + API + archivos estáticos
├── lib/
│   ├── db.js              SQLite: esquema y consultas
│   ├── enlaceSerial.js    puerto serial, autodetección y reconexión
│   └── simulador.js       placa simulada (misma lógica, en JS)
├── public/
│   ├── index.html         estructura y SVG del cruce
│   ├── estilos.css
│   └── app.js             WebSocket, pintado y panel de botones
└── datos/ciudad.db        base de datos (se crea sola)
```

El detalle de cada comando está en [`../COMANDOS.md`](../COMANDOS.md). Los
botones del panel no son más que atajos a esos mismos comandos.
