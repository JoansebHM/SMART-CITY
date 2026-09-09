/* ===========================================================================
   Ciudad Autoadaptable — logica del panel
   ---------------------------------------------------------------------------
   Recibe telemetria por WebSocket, la pinta, y manda comandos de vuelta.
   Los comandos son exactamente los mismos que acepta la consola serial:
   cada boton no es mas que un texto como "cny 1 on".
   =========================================================================== */

const $ = (sel) => document.querySelector(sel);

// ---------------------------------------------------------------------------
// Definicion del panel de botones (cada uno manda un comando de texto)
// ---------------------------------------------------------------------------
const GRUPOS = [
  {
    titulo: 'Escenarios rápidos',
    clase: 'escenario',
    botones: [
      ['Tráfico Calle 1', 'escenario trafico1'],
      ['Tráfico Calle 2', 'escenario trafico2'],
      ['Noche', 'escenario noche'],
      ['Día', 'escenario dia'],
      ['Madrugada', 'escenario madrugada'],
      ['Contaminación', 'escenario contaminacion'],
      ['Calle vacía', 'escenario vacio']
    ]
  },
  {
    titulo: 'Sensores de autos (CNY)',
    botones: [
      ['C1 todos ON', 'cny c1 on'],
      ['C1 todos OFF', 'cny c1 off'],
      ['C2 todos ON', 'cny c2 on'],
      ['C2 todos OFF', 'cny c2 off'],
      ['CNY1', 'cny 1 on'], ['CNY2', 'cny 2 on'], ['CNY3', 'cny 3 on'],
      ['CNY4', 'cny 4 on'], ['CNY5', 'cny 5 on'], ['CNY6', 'cny 6 on'],
      ['Todos AUTO', 'cny all auto']
    ]
  },
  {
    titulo: 'Ambiente',
    botones: [
      ['Forzar noche', 'noche on'],
      ['Forzar día', 'noche off'],
      ['Luz AUTO', 'noche auto'],
      ['CO₂ alto', 'co2 alto'],
      ['CO₂ bajo', 'co2 bajo'],
      ['CO₂ AUTO', 'co2 auto']
    ]
  },
  {
    // La hora se la decimos nosotros: la placa no tiene reloj. Con la hora en
    // 23h-4h Y los dos LDR bajos arranca la noche profunda (LY1+LR2).
    titulo: 'Hora del día',
    botones: [
      ['Son las 2:00', 'hora 2'],
      ['Son las 23:00', 'hora 23'],
      ['Son las 14:00', 'hora 14'],
      ['Olvidar hora', 'hora off']
    ]
  },
  {
    titulo: 'Peatones y pantalla',
    botones: [
      ['Peatón Calle 1', 'p1'],
      ['Peatón Calle 2', 'p2'],
      ['Cambiar pantalla', 'combo'],
      ['LCD off', 'lcd off'],
      ['LCD on', 'lcd on']
    ]
  },
  {
    titulo: 'Semáforo',
    botones: [
      ['Automático', 'sem auto'],
      ['Manual', 'sem manual'],
      ['Verde C1', 'fase v1'],
      ['Amarillo C1', 'fase a1'],
      ['Verde C2', 'fase v2'],
      ['Amarillo C2', 'fase a2'],
      ['Siguiente fase', 'fase sig']
    ]
  },
  {
    titulo: 'Ciclo',
    botones: [
      ['Verde corto (3 s)', 'set verdemin 3000'],
      ['Verde normal (5 s)', 'set verdemin 5000'],
      ['Amarillo corto', 'set amarillo 1500'],
      ['Amarillo normal', 'set amarillo 3000']
    ]
  },
  {
    titulo: 'Reiniciar',
    clase: 'peligro',
    botones: [
      ['LEDs AUTO', 'leds auto'],
      ['Apagar LEDs', 'leds off'],
      ['RESET total', 'reset']
    ]
  }
];

const NOMBRES_LED = ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'];
const COLORES_LED = ['--rojo', '--amarillo', '--verde', '--rojo', '--amarillo', '--verde'];

// Posiciones de los sensores en el SVG, para dibujar los carritos encima
const POS_SENSOR = [
  { x: 395, y: 150, h: true },  { x: 320, y: 150, h: true },  { x: 245, y: 150, h: true },
  { x: 150, y: 400, h: false }, { x: 150, y: 325, h: false }, { x: 150, y: 250, h: false }
];

// ---------------------------------------------------------------------------
// Estado local
// ---------------------------------------------------------------------------
const historial = [];       // {ts, co2, autos, luz}
const MAX_PUNTOS = 150;
let cfgActual = {};
let socket = null;

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
function conectar() {
  const protocolo = location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${protocolo}://${location.host}`);

  socket.addEventListener('message', (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch { return; }

    switch (m.tipo) {
      case 'tel':            aplicarTelemetria(m); break;
      case 'consola':        agregarLinea(m.nivel, m.texto); break;
      case 'enlace':         pintarEnlace(m); break;
      case 'historial':      cargarHistorial(m.datos); break;
      case 'eventosPrevios': m.datos.forEach((e) => agregarLinea(e.nivel, e.texto)); break;
      case 'ia':             manejarMensajeIa(m); break;
      case 'iaEstado':       pintarEstadoIa(m); break;
    }
  });

  socket.addEventListener('close', () => {
    pintarEnlace({ conectado: false, mensaje: 'Servidor desconectado' });
    setTimeout(conectar, 1500);   // reintento automatico
  });
}

function enviarComando(texto) {
  if (!texto) return;
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ tipo: 'cmd', texto }));
    agregarLinea('enviado', `> ${texto}`);
  } else {
    agregarLinea('error', '[ERROR] Sin conexión con el servidor');
  }
}

// ---------------------------------------------------------------------------
// Pintado del estado del enlace
// ---------------------------------------------------------------------------
function pintarEnlace({ conectado, mensaje, puerto }) {
  const pastilla = $('#pastillaEnlace');
  pastilla.classList.toggle('conectado', !!conectado);
  pastilla.classList.toggle('desconectado', !conectado);
  $('#textoEnlace').textContent = conectado ? (puerto ?? 'Conectado') : (mensaje ?? 'Desconectado');
}

// ---------------------------------------------------------------------------
// Pintado de la telemetria
// ---------------------------------------------------------------------------
function aplicarTelemetria(t) {
  cfgActual = t.cfg ?? cfgActual;

  // --- Fase y cuenta regresiva ---
  $('#faseTexto').textContent = t.faseTexto ?? '—';
  $('#restante').textContent = `${t.restante ?? 0} s`;
  $('#verdeCalc').textContent = `${((t.verdeCalc ?? 0) / 1000).toFixed(1)} s`;
  $('#avisoManual').classList.toggle('oculto', t.semAuto !== false);

  const duracion = duracionDeFase(t);
  const proporcion = duracion > 0 ? Math.max(0, Math.min(1, (t.restante * 1000) / duracion)) : 0;
  const barra = $('#barraFase');
  barra.style.width = `${proporcion * 100}%`;
  barra.style.background = `var(${colorDeFase(t.fase)})`;

  // --- Semaforos: cada bombillo usa el PWM real que reporta la placa ---
  (t.leds ?? []).forEach((pwm, i) => {
    const bombillo = document.getElementById(`led-${NOMBRES_LED[i]}`);
    if (!bombillo) return;
    const encendido = pwm > 0;
    bombillo.style.fill = encendido ? `var(${COLORES_LED[i]})` : '#2a3140';
    // El brillo del PWM se refleja en la opacidad: se ve el modo noche.
    bombillo.style.opacity = encendido ? (0.35 + (pwm / 255) * 0.65).toFixed(2) : '1';
  });

  // --- Sensores CNY en el mapa y en las tiras ---
  const cny = t.cny ?? [];
  const cnySim = t.cnySim ?? [];
  document.querySelectorAll('.sensor').forEach((g) => {
    const i = Number(g.dataset.cny);
    g.classList.toggle('activo', cny[i] === 1);
    g.classList.toggle('simulado', cnySim[i] === 1);
  });
  dibujarCarros(cny);
  pintarTiras(cny, cnySim);

  // --- Conteo de autos ---
  $('#autos1').textContent = t.autos?.[0] ?? 0;
  $('#autos2').textContent = t.autos?.[1] ?? 0;

  // --- Reloj y luz ambiente del mapa ---
  pintarReloj(t);

  // --- Medidores de ambiente ---
  medidor('co2', t.co2 ?? 0, 4095, t.co2Alto);
  medidor('ldr1', t.ldr?.[0] ?? 0, 4095, false);
  medidor('ldr2', t.ldr?.[1] ?? 0, 4095, false);

  // --- Insignias ---
  insignia('#insigniaNoche', t.noche, t.noche ? 'Noche' : 'Día', 'encendida');
  insignia('#insigniaCo2', t.co2Alto, t.co2Alto ? 'CO₂ ALTO' : 'CO₂ normal', 'alerta');
  const hayPeaton = (t.ped?.[0] === 1) || (t.ped?.[1] === 1);
  insignia('#insigniaPeaton', hayPeaton, hayPeaton ? 'Peatón esperando' : 'Sin peatones', 'encendida');

  // Hora indicada por consola (-1 = nadie la ha dicho) y el intermitente de
  // madrugada, que solo arranca si ademas los dos LDR ven poca luz.
  const hora = t.hora ?? -1;
  insignia('#insigniaHora', hora >= 0 && t.horaMadrugada,
           hora < 0 ? 'Sin hora' : `${hora}:00`, 'encendida');
  insignia('#insigniaNocheProfunda', !!t.nocheProfunda,
           t.nocheProfunda ? 'NOCHE PROFUNDA' : 'Ciclo normal', 'alerta');

  // --- Aviso de emergencia: lo mas importante de la pantalla cuando pasa ---
  const aviso = $('#avisoPrioridad');
  if (t.prioridad) {
    const resta = t.prioridadResta >= 0 ? `${t.prioridadResta} s` : 'indefinida';
    aviso.textContent = `PRIORIDAD Calle ${t.prioridad} · ${resta}`;
    aviso.classList.remove('oculto');
  } else {
    aviso.classList.add('oculto');
  }

  // --- Botones peatonales en el mapa ---
  $('#peaton1').classList.toggle('activo', t.ped?.[0] === 1);
  $('#peaton2').classList.toggle('activo', t.ped?.[1] === 1);

  // --- Sensores de ambiente en el mapa ---
  const brilloLdr = (v) => `hsl(45 90% ${Math.min(70, 15 + (v / 4095) * 55)}%)`;
  $('#ldr1Punto').style.fill = brilloLdr(t.ldr?.[0] ?? 0);
  $('#ldr2Punto').style.fill = brilloLdr(t.ldr?.[1] ?? 0);
  $('#co2Punto').style.fill = t.co2Alto ? 'var(--rojo)' : 'var(--naranja)';

  // --- Historial para la grafica ---
  historial.push({
    co2: t.co2 ?? 0,
    autos: (t.autos?.[0] ?? 0) + (t.autos?.[1] ?? 0),
    luz: ((t.ldr?.[0] ?? 0) + (t.ldr?.[1] ?? 0)) / 2
  });
  if (historial.length > MAX_PUNTOS) historial.shift();
  dibujarGrafica();
}

function duracionDeFase(t) {
  const c = cfgActual;
  if (t.fase === 'v1' || t.fase === 'v2') return t.verdeCalc ?? c.verdemin ?? 5000;
  if (t.fase === 'a1' || t.fase === 'a2') return c.amarillo ?? 3000;
  return c.todorojo ?? 1000;
}

function colorDeFase(fase) {
  if (fase === 'v1' || fase === 'v2') return '--verde';
  if (fase === 'a1' || fase === 'a2') return '--amarillo';
  return '--rojo';
}

function medidor(id, valor, maximo, alerta) {
  $(`#${id}Valor`).textContent = valor;
  const relleno = $(`#${id}Barra`);
  relleno.style.width = `${Math.min(100, (valor / maximo) * 100)}%`;
  relleno.classList.toggle('alerta', !!alerta);
}

/* ---------------------------------------------------------------------------
   RELOJ DIGITAL Y LUZ DEL MAPA
   ---------------------------------------------------------------------------
   La placa no tiene reloj: solo sabe la hora que le dijimos con "hora <0-23>".
   El panel muestra esa hora y le cuenta los minutos desde que la recibio, pero
   los deja clavados en :59 para no ensenar nunca una hora distinta de la que
   cree el firmware (que es la que decide la noche profunda).
   El tinte del mapa es solo cosmetico: la logica sigue mirando los LDR.
--------------------------------------------------------------------------- */
let horaMostrada = -1;
let horaRecibidaEn = 0;

const NOMBRE_FRANJA = {
  sinhora: 'sin hora', madrugada: 'madrugada',
  dia: 'día', atardecer: 'atardecer', noche: 'noche'
};

function franjaDeHora(h) {
  if (h < 0) return 'sinhora';
  if (h >= 23 || h <= 4) return 'madrugada';   // misma franja que la noche profunda
  if (h <= 17) return 'dia';
  if (h <= 20) return 'atardecer';
  return 'noche';
}

function pintarReloj(t) {
  const h = t.hora ?? -1;
  if (h !== horaMostrada) {
    horaMostrada = h;
    horaRecibidaEn = Date.now();
  }

  const franja = franjaDeHora(h);
  $('#mapa').dataset.franja = franja;
  $('#relojFranja').textContent = NOMBRE_FRANJA[franja];

  if (h < 0) {
    $('#relojHora').textContent = '--:--';
    return;
  }
  const minutos = Math.min(59, Math.floor((Date.now() - horaRecibidaEn) / 60000));
  $('#relojHora').textContent =
    `${String(h).padStart(2, '0')}:${String(minutos).padStart(2, '0')}`;
}

function insignia(sel, activa, texto, clase) {
  const el = $(sel);
  el.textContent = texto;
  el.className = `insignia${activa ? ` ${clase}` : ''}`;
}

function pintarTiras(cny, cnySim) {
  const cont = $('#tirasCny');
  if (cont.children.length === 0) {
    for (let i = 0; i < 6; i++) {
      const d = document.createElement('div');
      d.className = 'tira';
      d.textContent = i + 1;
      cont.appendChild(d);
    }
  }
  [...cont.children].forEach((el, i) => {
    el.classList.toggle('activa', cny[i] === 1);
    el.classList.toggle('simulada', cnySim[i] === 1);
  });
}

/** Dibuja un carrito sobre cada sensor que este detectando. */
function dibujarCarros(cny) {
  const capa = $('#carros');
  capa.innerHTML = '';
  cny.forEach((activo, i) => {
    if (!activo) return;
    const p = POS_SENSOR[i];
    const ancho = p.h ? 30 : 18;
    const alto = p.h ? 18 : 30;
    const r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    r.setAttribute('x', p.x - ancho / 2);
    r.setAttribute('y', p.y - alto / 2);
    r.setAttribute('width', ancho);
    r.setAttribute('height', alto);
    r.setAttribute('rx', 4);
    r.setAttribute('class', 'carro');
    capa.appendChild(r);
  });
}

// ---------------------------------------------------------------------------
// Grafica de historial (SVG, sin librerias)
// ---------------------------------------------------------------------------
function cargarHistorial(filas) {
  historial.length = 0;
  for (const f of filas) {
    historial.push({
      co2: f.co2 ?? 0,
      autos: (f.autos1 ?? 0) + (f.autos2 ?? 0),
      luz: ((f.ldr1 ?? 0) + (f.ldr2 ?? 0)) / 2
    });
  }
  dibujarGrafica();
}

function dibujarGrafica() {
  const svg = $('#lienzoGrafica');
  const ancho = 600, alto = 160;
  if (historial.length < 2) { svg.innerHTML = ''; return; }

  const linea = (clave, maximo, color) => {
    const puntos = historial.map((p, i) => {
      const x = (i / (MAX_PUNTOS - 1)) * ancho;
      const y = alto - Math.min(1, p[clave] / maximo) * (alto - 12) - 6;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    });
    return `<polyline points="${puntos.join(' ')}" fill="none"
            stroke="var(${color})" stroke-width="2"
            stroke-linejoin="round" stroke-linecap="round"/>`;
  };

  const rejilla = [0.25, 0.5, 0.75]
    .map((f) => `<line x1="0" y1="${alto * f}" x2="${ancho}" y2="${alto * f}"
                  stroke="var(--borde)" stroke-width="1"/>`)
    .join('');

  svg.innerHTML =
    rejilla +
    linea('co2', 4095, '--naranja') +
    linea('autos', 6, '--verde') +
    linea('luz', 4095, '--azul');
}

// ---------------------------------------------------------------------------
// Consola
// ---------------------------------------------------------------------------
function agregarLinea(nivel, texto) {
  const salida = $('#salidaConsola');
  const pegadoAbajo = salida.scrollHeight - salida.scrollTop - salida.clientHeight < 40;

  const fila = document.createElement('div');
  fila.className = `linea-consola ${nivel ?? 'info'}`;
  const hora = new Date().toLocaleTimeString('es-CO', { hour12: false });
  fila.innerHTML = `<span class="hora">${hora}</span><span class="txt"></span>`;
  fila.querySelector('.txt').textContent = texto;
  salida.appendChild(fila);

  while (salida.children.length > 400) salida.removeChild(salida.firstChild);
  if (pegadoAbajo) salida.scrollTop = salida.scrollHeight;
}

// ---------------------------------------------------------------------------
// Construccion del panel de botones
// ---------------------------------------------------------------------------
function construirControles() {
  const cont = $('#gruposControles');
  for (const grupo of GRUPOS) {
    const div = document.createElement('div');
    div.className = 'grupo';
    div.innerHTML = `<div class="grupo-titulo">${grupo.titulo}</div>`;
    const fila = document.createElement('div');
    fila.className = 'fila-botones';
    for (const [etiqueta, comando] of grupo.botones) {
      const b = document.createElement('button');
      b.className = `btn${grupo.clase ? ` ${grupo.clase}` : ''}`;
      b.textContent = etiqueta;
      b.title = comando;
      b.addEventListener('click', () => enviarComando(comando));
      fila.appendChild(b);
    }
    div.appendChild(fila);
    cont.appendChild(div);
  }
}

// ---------------------------------------------------------------------------
// Arranque
// ---------------------------------------------------------------------------
construirControles();
conectar();

$('#formComando').addEventListener('submit', (e) => {
  e.preventDefault();
  const entrada = $('#entradaComando');
  enviarComando(entrada.value.trim());
  entrada.value = '';
});

$('#limpiarConsola').addEventListener('click', () => {
  $('#salidaConsola').innerHTML = '';
});

/* ===========================================================================
   CHAT CON LA IA
   ---------------------------------------------------------------------------
   El navegador solo manda el texto y pinta lo que vuelve. Toda la traduccion
   a comandos pasa en el servidor (lib/ia/). Se muestran a proposito los
   comandos que la IA decidio mandar: en una demostracion vale mucho mas ver
   "entendi ambulancia -> prio c1 60" que una caja negra.
   =========================================================================== */

const SUGERENCIAS = [
  'Viene una ambulancia con urgencia por la calle 10',
  'Ya pasó la ambulancia, vuelve a la normalidad',
  'Bájale la intensidad a las luces a la mitad',
  'Simula hora pico en las dos calles',
  'Pon los semáforos en amarillo intermitente',
  'Hay un accidente, deja todo en rojo'
];

let iaDisponible = false;

function pintarSugerencias() {
  const cont = $('#sugerencias');
  for (const texto of SUGERENCIAS) {
    const b = document.createElement('button');
    b.className = 'sugerencia';
    b.type = 'button';
    b.textContent = texto;
    b.addEventListener('click', () => {
      $('#entradaChat').value = texto;
      $('#formChat').requestSubmit();
    });
    cont.appendChild(b);
  }
}

function burbuja(clase, contenido) {
  const hilo = $('#hiloChat');
  const div = document.createElement('div');
  div.className = `burbuja ${clase}`;
  if (typeof contenido === 'string') div.textContent = contenido;
  else div.appendChild(contenido);
  hilo.appendChild(div);
  hilo.scrollTop = hilo.scrollHeight;
  return div;
}

function quitarPensando() {
  document.querySelectorAll('.burbuja.pensando').forEach((el) => el.remove());
}

function pintarRespuestaIa(m) {
  quitarPensando();

  const cuerpo = document.createElement('div');
  const frase = document.createElement('div');
  frase.textContent = m.texto || 'Listo.';
  cuerpo.appendChild(frase);

  if (m.comandos?.length || m.rechazados?.length) {
    const chips = document.createElement('div');
    chips.className = 'comandos-ia';
    for (const c of m.comandos ?? []) {
      const chip = document.createElement('span');
      chip.className = 'chip-cmd';
      chip.textContent = c;
      chips.appendChild(chip);
    }
    for (const r of m.rechazados ?? []) {
      const chip = document.createElement('span');
      chip.className = 'chip-cmd rechazado';
      chip.textContent = r.comando;
      chip.title = r.motivo;
      chips.appendChild(chip);
    }
    cuerpo.appendChild(chips);
  }

  if (m.diagnostico) {
    const meta = document.createElement('div');
    meta.className = 'meta-ia';
    const d = m.diagnostico;
    if (d.ms) meta.appendChild(etiquetaMeta(`${(d.ms / 1000).toFixed(1)} s`));
    if (d.categorias) meta.appendChild(etiquetaMeta(`catálogo: ${d.categorias.join(', ')}`));
    if (d.tokensPrompt) meta.appendChild(etiquetaMeta(`~${d.tokensPrompt} tokens`));
    cuerpo.appendChild(meta);
  }

  burbuja('asistente', cuerpo);
}

function etiquetaMeta(texto) {
  const s = document.createElement('span');
  s.textContent = texto;
  return s;
}

function manejarMensajeIa(m) {
  switch (m.rol) {
    case 'usuario':   burbuja('usuario', m.texto); break;
    case 'pensando': {
      const d = burbuja('pensando', '');
      d.innerHTML = '<span class="puntos">pensando</span>';
      break;
    }
    case 'asistente': pintarRespuestaIa(m); break;
    case 'error':     quitarPensando(); burbuja('error', m.texto); break;
    case 'sistema':   burbuja('sistema', m.texto); break;
  }
}

function pintarEstadoIa({ disponible, modelo, uso }) {
  iaDisponible = disponible;
  const p = $('#pastillaIa');

  if (!disponible) {
    p.textContent = 'falta GEMINI_API_KEY';
    p.className = 'pastilla-mini inactiva';
    return;
  }

  // Se muestra el consumo para que veas venir el limite antes de chocar.
  const corto = (modelo ?? '').replace(/^models\//, '');
  if (uso) {
    const { minuto, dia, limites } = uso;
    p.textContent = `${corto} · ${minuto}/${limites.rpm} por min · ${dia}/${limites.rpd} hoy`;
    const apretado = minuto >= limites.rpm - 1 || dia >= limites.rpd - 2;
    p.className = `pastilla-mini ${apretado ? 'inactiva' : 'activa'}`;
    p.title = apretado ? 'Estas cerca del limite del plan gratuito' : '';
  } else {
    p.textContent = corto;
    p.className = 'pastilla-mini activa';
  }
}

function enviarChat(texto) {
  if (!texto) return;
  if (socket?.readyState !== WebSocket.OPEN) {
    burbuja('error', 'Sin conexión con el servidor.');
    return;
  }
  socket.send(JSON.stringify({ tipo: 'ia', texto }));
}

pintarSugerencias();

$('#formChat').addEventListener('submit', (e) => {
  e.preventDefault();
  const entrada = $('#entradaChat');
  enviarChat(entrada.value.trim());
  entrada.value = '';
});

$('#olvidarIa').addEventListener('click', () => {
  if (socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ tipo: 'iaOlvidar' }));
  }
});
