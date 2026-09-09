/**
 * Placa simulada.
 *
 * Reimplementa en JavaScript la misma logica y el mismo juego de comandos del
 * sketch, y expone exactamente la misma interfaz que EnlaceSerial. Sirve para:
 *   - desarrollar y probar el dashboard sin tener la maqueta conectada,
 *   - tener un plan B si el USB falla el dia de la sustentacion.
 *
 * Se activa con:  npm run simular
 */

import { EventEmitter } from 'node:events';

const FASES = ['v1', 'a1', 'r1', 'v2', 'a2', 'r2'];
const NOMBRES = {
  v1: 'Verde C1', a1: 'Amar. C1', r1: 'Rojo 1>2',
  v2: 'Verde C2', a2: 'Amar. C2', r2: 'Rojo 2>1'
};

export class PlacaSimulada extends EventEmitter {
  constructor({ periodoTelemetria = 200 } = {}) {
    super();
    this.periodoTelemetria = periodoTelemetria;
    this.conectado = false;
    this.rutaActual = 'simulador';

    this.cfg = {
      verdemin: 5000, verdemax: 15000, extension: 2000,
      amarillo: 3000, todorojo: 1000,
      umbralnoche: 800, umbralco2: 2500,
      brillodia: 255, brillonoche: 60,
      // Aqui no cambia nada (los sensores son simulados), pero el firmware y el
      // catalogo lo aceptan: sin el, "set cnybajo 1" fallaba solo en el simulador.
      cnybajo: 0
    };

    this.fase = 'v1';
    this.inicioFase = Date.now();
    this.verdeCalc = this.cfg.verdemin;
    this.semAuto = true;

    this.modoCny = Array(6).fill('auto');   // auto | on | off
    this.simLdr = [-1, -1];
    this.simCo2 = -1;
    this.simNoche = -1;
    this.simLed = Array(6).fill(-1);
    this.ped = [false, false];
    this.pantalla = 1;
    this.lcdOn = true;
    this.botones = true;

    // --- Estado de la capa de comandos ampliada ---
    this.brilloMaestro = 255;
    this.parpadeo = Array(6).fill(0);
    this.parpadeoOn = Array(6).fill(true);
    this.parpadeoUlt = Array(6).fill(0);
    this.prioridad = 0;
    this.prioridadInicio = 0;
    this.prioridadDuracion = 0;
    this.watchdogLimite = 0;
    this.watchdogUltimo = 0;
    this.seguro = true;
    this.flujo = [0, 0];
    this.flujoUltimo = [0, 0];
    this.flujoApagarEn = Array(6).fill(0);
    this.ruido = false;
    this.lcdTexto = '';
    this.programados = [];          // [{cuando, comando}]
    this.slots = {};                // instantaneas

    // --- Hora indicada por consola y noche profunda (LY1 + LR2 intermitentes) ---
    this.horaIndicada = -1;
    this.horaMadrugada = false;
    this.nocheProfunda = false;
    this.parpNocheOn = false;
    this.parpNocheUlt = 0;
    this.avisoHora = false;
    this.INTERVALO_PARPADEO_NOCHE = 400;

    // --- Emergencia por CO2: congela el ciclo y evacua por la Calle 2 ---
    this.emergenciaCO2 = false;

    // Valores "fisicos" que van derivando solos, para que se vea vivo
    this.ldrReal = [640, 620];
    this.co2Real = 1300;
    this.cnyReal = Array(6).fill(false);
  }

  async conectar() {
    this.conectado = true;
    this.emit('estado', {
      conectado: true,
      puerto: 'simulador',
      mensaje: 'Placa SIMULADA activa (no hay hardware conectado)'
    });
    this.emit('consola', { nivel: 'info', texto: '=== CIUDAD AUTOADAPTABLE - MODO SIMULADO ===' });

    this.tickLogica = setInterval(() => this.#avanzar(), 50);
    this.tickTelemetria = setInterval(() => this.#emitirTelemetria(), this.periodoTelemetria);
    this.tickFisica = setInterval(() => this.#derivarSensores(), 700);
  }

  // --- Sensores reales simulados: derivan suavemente y a veces pasa un carro ---
  #derivarSensores() {
    const mover = (v, min, max, paso) =>
      Math.max(min, Math.min(max, v + Math.round((Math.random() - 0.5) * paso)));
    this.ldrReal[0] = mover(this.ldrReal[0], 400, 3200, 120);
    this.ldrReal[1] = mover(this.ldrReal[1], 400, 3200, 120);
    this.co2Real = mover(this.co2Real, 600, 2400, 200);
    for (let i = 0; i < 6; i++) {
      if (Math.random() < 0.18) this.cnyReal[i] = !this.cnyReal[i];
    }
  }

  get cny() {
    return this.modoCny.map((m, i) =>
      m === 'on' ? 1 : m === 'off' ? 0 : this.cnyReal[i] ? 1 : 0
    );
  }

  get ldr() {
    return [
      this.simLdr[0] >= 0 ? this.simLdr[0] : this.ldrReal[0],
      this.simLdr[1] >= 0 ? this.simLdr[1] : this.ldrReal[1]
    ];
  }

  get co2() {
    return this.simCo2 >= 0 ? this.simCo2 : this.co2Real;
  }

  get noche() {
    if (this.simNoche === 0) return false;
    if (this.simNoche === 1) return true;
    const l = this.ldr;
    return (l[0] + l[1]) / 2 < this.cfg.umbralnoche;
  }

  get autos() {
    const c = this.cny;
    return [c[0] + c[1] + c[2], c[3] + c[4] + c[5]];
  }

  get leds() {
    // 0=LR1 1=LY1 2=LG1 3=LR2 4=LY2 5=LG2
    const on = [false, false, false, false, false, false];

    // Los dos modos forzados mandan sobre la fase y sobre los override manuales.
    const forzado = this.emergenciaCO2 || this.nocheProfunda;

    if (this.emergenciaCO2) {
      on[5] = true;   // LG2: evacuacion por la Calle 2
      on[0] = true;   // LR1
    } else if (this.nocheProfunda) {
      on[1] = this.parpNocheOn;   // LY1
      on[3] = this.parpNocheOn;   // LR2
    } else {
      switch (this.fase) {
        case 'v1': on[2] = on[3] = true; break;
        case 'a1': on[1] = on[3] = true; break;
        case 'r1': on[0] = on[3] = true; break;
        case 'v2': on[0] = on[5] = true; break;
        case 'a2': on[0] = on[4] = true; break;
        case 'r2': on[0] = on[3] = true; break;
      }
    }
    const brillo = this.noche ? this.cfg.brillonoche : this.cfg.brillodia;
    const ahora = Date.now();

    const salida = on.map((encendido, i) => {
      let v = forzado
        ? (encendido ? brillo : 0)
        : (this.simLed[i] >= 0 ? this.simLed[i] : encendido ? brillo : 0);
      v = Math.round((v * this.brilloMaestro) / 255);
      if (!forzado && this.parpadeo[i] > 0) {
        if (ahora - this.parpadeoUlt[i] >= this.parpadeo[i]) {
          this.parpadeoUlt[i] = ahora;
          this.parpadeoOn[i] = !this.parpadeoOn[i];
        }
        if (!this.parpadeoOn[i]) v = 0;
      }
      return Math.max(0, Math.min(255, v));
    });

    // Interlock: nunca los dos verdes a la vez
    if (this.seguro && salida[2] > 0 && salida[5] > 0) {
      const ganaC1 = this.prioridad === 1 || (this.prioridad === 0 && this.fase === 'v1');
      if (ganaC1) salida[5] = 0; else salida[2] = 0;
    }
    return salida;
  }

  #duracionFase() {
    if (this.fase === 'v1' || this.fase === 'v2') return this.verdeCalc;
    if (this.fase === 'a1' || this.fase === 'a2') return this.cfg.amarillo;
    return this.cfg.todorojo;
  }

  get restante() {
    if (!this.semAuto) return 0;
    return Math.max(0, Math.ceil((this.#duracionFase() - (Date.now() - this.inicioFase)) / 1000));
  }

  #calcularVerde(autos) {
    let d = this.cfg.verdemin + autos * this.cfg.extension;
    let max = this.cfg.verdemax;
    if (this.co2 >= this.cfg.umbralco2) max = this.cfg.verdemin + this.cfg.extension * 2;
    return Math.min(Math.max(d, this.cfg.verdemin), max);
  }

  #avanzar() {
    this.#atenderProgramados();
    this.#atenderFlujo();
    this.#atenderWatchdog();
    this.#atenderPrioridad();
    this.#atenderEmergenciaCO2();

    // La emergencia por CO2 manda sobre todo: ni ciclo ni noche profunda.
    if (this.emergenciaCO2) return;
    this.#atenderNocheProfunda();

    if (!this.semAuto) return;
    const t = Date.now() - this.inicioFase;

    switch (this.fase) {
      case 'v1': {
        let limite = this.verdeCalc;
        if (this.ped[0] && limite > this.cfg.verdemin) limite = this.cfg.verdemin;
        if (t >= limite) this.#cambiarFase('a1');
        break;
      }
      case 'a1':
        if (t >= this.cfg.amarillo) this.#cambiarFase('r1');
        break;
      case 'r1':
        if (t >= this.cfg.todorojo) {
          this.ped[0] = false;
          this.verdeCalc = this.#calcularVerde(this.autos[1]);
          this.#cambiarFase('v2');
        }
        break;
      case 'v2': {
        let limite = this.verdeCalc;
        if (this.ped[1] && limite > this.cfg.verdemin) limite = this.cfg.verdemin;
        if (t >= limite) this.#cambiarFase('a2');
        break;
      }
      case 'a2':
        if (t >= this.cfg.amarillo) this.#cambiarFase('r2');
        break;
      case 'r2':
        if (t >= this.cfg.todorojo) {
          this.ped[1] = false;
          this.verdeCalc = this.#calcularVerde(this.autos[0]);
          this.#cambiarFase('v1');
        }
        break;
    }
  }

  #cambiarFase(f) {
    this.fase = f;
    this.inicioFase = Date.now();
  }

  // --- Rutinas que en el firmware corren desde el loop() ---

  #atenderProgramados() {
    const ahora = Date.now();
    const vencidos = this.programados.filter((p) => ahora >= p.cuando);
    this.programados = this.programados.filter((p) => ahora < p.cuando);
    for (const p of vencidos) this.#procesar(p.comando);
  }

  #atenderFlujo() {
    const ahora = Date.now();
    for (let i = 0; i < 6; i++) {
      if (this.flujoApagarEn[i] && ahora >= this.flujoApagarEn[i]) {
        this.flujoApagarEn[i] = 0;
        this.modoCny[i] = 'off';
      }
    }
    for (let calle = 0; calle < 2; calle++) {
      if (this.flujo[calle] <= 0) continue;
      const intervalo = 60000 / this.flujo[calle];
      if (ahora - this.flujoUltimo[calle] < intervalo) continue;
      this.flujoUltimo[calle] = ahora;
      const sensor = calle * 3 + Math.floor(Math.random() * 3);
      this.modoCny[sensor] = 'on';
      this.flujoApagarEn[sensor] = ahora + 1200 + Math.random() * 2000;
    }
  }

  // CO2 por encima del umbral: se congela el ciclo, se fuerza LG2+LR1 y el LCD
  // avisa. Al normalizarse, todo vuelve al comportamiento normal.
  #atenderEmergenciaCO2() {
    const peligro = this.co2 >= this.cfg.umbralco2;

    if (peligro && !this.emergenciaCO2) {
      this.emergenciaCO2 = true;
      this.nocheProfunda = false;
      this.emit('consola', { nivel: 'evento', texto: '[EVENTO] EMERGENCIA CO2: nivel critico. Evacuacion por la Calle 2.' });
    } else if (!peligro && this.emergenciaCO2) {
      this.emergenciaCO2 = false;
      this.inicioFase = Date.now();
      this.emit('consola', { nivel: 'evento', texto: '[EVENTO] CO2 normalizado. Reanudando operacion normal.' });
    }

    if (this.emergenciaCO2 && this.fase !== 'v2') this.#cambiarFase('v2');
  }

  // Noche profunda: hace falta la hora en 23h-4h Y los DOS LDR por debajo del
  // umbral. Mientras dure, LY1 y LR2 parpadean juntos.
  #atenderNocheProfunda() {
    const l = this.ldr;
    const ambosBajos = l[0] < this.cfg.umbralnoche && l[1] < this.cfg.umbralnoche;

    if (this.horaMadrugada && ambosBajos) {
      if (!this.nocheProfunda) {
        this.nocheProfunda = true;
        this.parpNocheOn = true;
        this.parpNocheUlt = Date.now();
        this.emit('consola', { nivel: 'evento', texto: '[EVENTO] NOCHE PROFUNDA: intermitente LY1 + LR2.' });
      }
      this.avisoHora = false;
      const ahora = Date.now();
      if (ahora - this.parpNocheUlt >= this.INTERVALO_PARPADEO_NOCHE) {
        this.parpNocheOn = !this.parpNocheOn;
        this.parpNocheUlt = ahora;
      }
      return;
    }

    if (this.nocheProfunda) {
      this.nocheProfunda = false;
      this.inicioFase = Date.now();
      this.emit('consola', { nivel: 'evento', texto: '[EVENTO] Fin de la noche profunda. Ciclo normal reanudado.' });
    }

    if (this.horaMadrugada && !ambosBajos) {
      if (!this.avisoHora) {
        this.avisoHora = true;
        this.emit('consola', { nivel: 'evento', texto: '[EVENTO] ADVERTENCIA: hora en 23h-4h pero los LDR no ven poca luz en ambas vias.' });
      }
    } else {
      this.avisoHora = false;
    }
  }

  #atenderWatchdog() {
    if (this.watchdogLimite === 0) return;
    if (Date.now() - this.watchdogUltimo < this.watchdogLimite) return;
    this.watchdogLimite = 0;
    if (this.prioridad !== 0) this.#terminarPrioridad('WATCHDOG: nadie renovo la prioridad');
    else { this.#procesar('reset'); this.emit('consola', { nivel: 'evento', texto: '[EVENTO] WATCHDOG: todo vuelve a AUTO.' }); }
  }

  #atenderPrioridad() {
    if (this.prioridad === 0) return;
    if (this.prioridadDuracion > 0 && Date.now() - this.prioridadInicio >= this.prioridadDuracion) {
      this.#terminarPrioridad('La prioridad se cumplio y vencio');
      return;
    }
    const objetivo = this.prioridad === 1 ? 'v1' : 'v2';
    if (this.fase !== objetivo) this.#cambiarFase(objetivo);
  }

  #terminarPrioridad(motivo) {
    this.prioridad = 0;
    this.prioridadDuracion = 0;
    this.watchdogLimite = 0;
    this.lcdTexto = '';
    this.#restaurar(0);
    this.emit('consola', { nivel: 'evento', texto: `[EVENTO] ${motivo}. Sistema restaurado.` });
  }

  #guardar(slot) {
    this.slots[slot] = {
      modoCny: [...this.modoCny], simLed: [...this.simLed], parpadeo: [...this.parpadeo],
      simLdr: [...this.simLdr], simCo2: this.simCo2, simNoche: this.simNoche,
      semAuto: this.semAuto, botones: this.botones, brilloMaestro: this.brilloMaestro,
      cfg: { ...this.cfg }, pantalla: this.pantalla
    };
  }

  #restaurar(slot) {
    const s = this.slots[slot];
    if (!s) return false;
    this.modoCny = [...s.modoCny]; this.simLed = [...s.simLed]; this.parpadeo = [...s.parpadeo];
    this.simLdr = [...s.simLdr]; this.simCo2 = s.simCo2; this.simNoche = s.simNoche;
    this.semAuto = s.semAuto; this.botones = s.botones; this.brilloMaestro = s.brilloMaestro;
    this.cfg = { ...s.cfg }; this.pantalla = s.pantalla;
    this.inicioFase = Date.now();
    return true;
  }

  #emitirTelemetria() {
    this.emit('telemetria', {
      tipo: 'tel',
      ms: Date.now() % 100000000,
      fase: this.fase,
      faseTexto: NOMBRES[this.fase],
      semAuto: this.semAuto,
      restante: this.restante,
      verdeCalc: this.verdeCalc,
      cny: this.cny,
      cnySim: this.modoCny.map((m) => (m === 'auto' ? 0 : 1)),
      autos: this.autos,
      ldr: this.ldr,
      ldrSim: [this.simLdr[0] >= 0 ? 1 : 0, this.simLdr[1] >= 0 ? 1 : 0],
      co2: this.co2,
      co2Sim: this.simCo2 >= 0,
      co2Alto: this.co2 >= this.cfg.umbralco2,
      co2Emergencia: this.emergenciaCO2,
      noche: this.noche,
      nocheSim: this.simNoche,
      hora: this.horaIndicada,
      horaMadrugada: this.horaMadrugada,
      nocheProfunda: this.nocheProfunda,
      ped: [this.ped[0] ? 1 : 0, this.ped[1] ? 1 : 0],
      leds: this.leds,
      ledSim: this.simLed,
      parpadeo: this.parpadeo,
      brilloMaestro: this.brilloMaestro,
      pantalla: this.pantalla,
      lcdPresente: true,
      lcdOn: this.lcdOn,
      lcdTexto: this.lcdTexto,
      botones: this.botones,
      prioridad: this.prioridad,
      prioridadResta: this.prioridad === 0 || this.prioridadDuracion === 0
        ? -1
        : Math.max(0, Math.round((this.prioridadDuracion - (Date.now() - this.prioridadInicio)) / 1000)),
      seguro: this.seguro,
      watchdog: Math.round(this.watchdogLimite / 1000),
      programados: this.programados.length,
      flujo: this.flujo,
      ruido: this.ruido,
      cfg: { ...this.cfg }
    });
  }

  #ok(m) { this.emit('consola', { nivel: 'ok', texto: `[OK] ${m}` }); }
  #err(m) { this.emit('consola', { nivel: 'error', texto: `[ERROR] ${m}` }); }

  /** Misma firma que EnlaceSerial.enviar: true si el comando se acepto. */
  enviar(linea) {
    if (!this.conectado) return false;
    this.#procesar(linea);
    return true;
  }

  /** Mismo parser de comandos que el firmware, en su version esencial. */
  #procesar(lineaOriginal) {
    const original = String(lineaOriginal).trim();
    const linea = original.toLowerCase();
    const [cmd, a1, a2, a3] = linea.split(/\s+/);
    const num = (s) => (/^\d+$/.test(s ?? '') ? parseInt(s, 10) : null);
    const resto = (n) => original.split(/\s+/).slice(n).join(' ');
    const calleDe = (t) => (t === 'c1' || t === '1' ? 1 : t === 'c2' || t === '2' ? 2 : 0);
    const LEDS = ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'];

    switch (cmd) {
      // ---------------- emergencia ----------------
      case 'prio': {
        if (a1 === 'off' || a1 === 'fin') {
          if (this.prioridad === 0) return this.#err('No hay ninguna prioridad activa');
          return this.#terminarPrioridad('Prioridad cancelada por comando');
        }
        const calle = calleDe(a1);
        if (!calle) return this.#err('Uso: prio <c1|c2> <segundos|inf>  /  prio off');
        this.#guardar(0);
        this.prioridad = calle;
        this.prioridadInicio = Date.now();
        if (a2 === 'inf' || a2 === 'indefinido' || a2 === undefined) {
          this.prioridadDuracion = 0;
          this.watchdogLimite = 0;
          this.#ok(`PRIORIDAD indefinida para Calle ${calle}. Usa 'prio off' para terminarla.`);
        } else {
          const seg = Math.max(1, num(a2) ?? 1);
          this.prioridadDuracion = seg * 1000;
          this.watchdogLimite = (seg + 5) * 1000;
          this.watchdogUltimo = Date.now();
          this.#ok(`PRIORIDAD ${seg} s para Calle ${calle}`);
        }
        this.semAuto = false;
        this.#cambiarFase(calle === 1 ? 'v1' : 'v2');
        return;
      }
      case 'panico':
        this.prioridad = 0; this.semAuto = false;
        this.simLed = Array(6).fill(-1); this.parpadeo = Array(6).fill(0);
        this.#cambiarFase('r1');
        return this.#ok('PANICO: ambas calles en rojo, ciclo detenido.');
      case 'apagar':
        this.simLed = Array(6).fill(0); this.parpadeo = Array(6).fill(0);
        return this.#ok('Todos los LEDs apagados.');

      // ---------------- sistema ----------------
      case 'guardar': {
        const slot = num(a1) ?? 1;
        if (slot < 0 || slot > 2) return this.#err('Slot valido: 0 a 2');
        this.#guardar(slot);
        return this.#ok(`Estado completo guardado en el slot ${slot}`);
      }
      case 'restaurar': {
        const slot = num(a1) ?? 1;
        if (slot < 0 || slot > 2) return this.#err('Slot valido: 0 a 2');
        return this.#restaurar(slot)
          ? this.#ok(`Estado restaurado desde el slot ${slot}`)
          : this.#err(`El slot ${slot} esta vacio`);
      }
      case 'watchdog':
        if (a1 === 'off' || a1 === '0') { this.watchdogLimite = 0; return this.#ok('Watchdog desactivado'); }
        if (num(a1) === null) return this.#err('Uso: watchdog <segundos|off>');
        this.watchdogLimite = num(a1) * 1000;
        this.watchdogUltimo = Date.now();
        return this.#ok(`Watchdog armado: ${a1} s`);
      case 'seguro':
        if (a1 === 'on')  { this.seguro = true;  return this.#ok('Interlock ACTIVO'); }
        if (a1 === 'off') { this.seguro = false; return this.#ok('Interlock DESACTIVADO'); }
        return this.#err('Uso: seguro <on|off>');

      // ---------------- luces ----------------
      case 'brillo': {
        let v;
        if (String(a1).endsWith('%')) {
          const n = num(String(a1).slice(0, -1));
          if (n === null) return this.#err('Uso: brillo <0-255> o brillo <0-100>%');
          v = Math.round((n * 255) / 100);
        } else {
          v = num(a1);
          if (v === null) return this.#err('Uso: brillo <0-255> o brillo <0-100>%');
        }
        this.brilloMaestro = Math.max(0, Math.min(255, v));
        return this.#ok(`Brillo maestro = ${this.brilloMaestro}`);
      }
      case 'parpadeo': {
        const ms = a2 === 'off' || a2 === '0' ? 0 : Math.max(50, num(a2) ?? -1);
        if (ms < 0) return this.#err('Uso: parpadeo <led|sem1|sem2|all> <ms|off>');
        let rango;
        if (a1 === 'all' || a1 === 'todos') rango = [0, 1, 2, 3, 4, 5];
        else if (a1 === 'sem1') rango = [0, 1, 2];
        else if (a1 === 'sem2') rango = [3, 4, 5];
        else if (LEDS.includes(a1)) rango = [LEDS.indexOf(a1)];
        else return this.#err('Uso: parpadeo <led|sem1|sem2|all> <ms|off>');
        rango.forEach((i) => { this.parpadeo[i] = ms; this.parpadeoOn[i] = true; this.parpadeoUlt[i] = Date.now(); });
        return this.#ok(`Parpadeo de ${a1} ${ms === 0 ? 'apagado' : `cada ${ms} ms`}`);
      }
      case 'fade': {
        const i = LEDS.indexOf(a1);
        if (i < 0 || num(a2) === null || num(a3) === null) return this.#err('Uso: fade <led> <0-255> <ms>');
        this.simLed[i] = Math.max(0, Math.min(255, num(a2)));   // el simulador salta al destino
        return this.#ok(`Fade de ${a1} hasta ${this.simLed[i]} en ${a3} ms`);
      }

      // ---------------- sensores ----------------
      case 'flujo': {
        if (a1 === 'off') {
          this.flujo = [0, 0]; this.flujoApagarEn = Array(6).fill(0);
          return this.#ok('Flujo de trafico automatico apagado');
        }
        const calle = calleDe(a1);
        if (!calle || num(a2) === null) return this.#err('Uso: flujo <c1|c2> <autos/min>  /  flujo off');
        this.flujo[calle - 1] = Math.max(0, Math.min(120, num(a2)));
        this.flujoUltimo[calle - 1] = Date.now();
        return this.#ok(`Flujo en Calle ${calle}: ${a2} autos/min`);
      }
      case 'ruido':
        if (a1 === 'on' || a1 === 'off') { this.ruido = a1 === 'on'; return this.#ok(`Ruido ${a1}`); }
        return this.#err('Uso: ruido <on|off>');

      // ---------------- peatones ----------------
      case 'peaton': {
        if (a1 === 'limpiar' || a1 === 'cancelar') {
          this.ped = [false, false];
          return this.#ok('Solicitudes peatonales canceladas');
        }
        const calle = calleDe(a1);
        if (!calle) return this.#err('Uso: peaton <c1|c2|limpiar>');
        this.ped[calle - 1] = true;
        return this.emit('consola', { nivel: 'evento', texto: `[EVENTO] Peaton solicito cruce en Calle ${calle}` });
      }

      // ---------------- programacion ----------------
      case 'en': {
        const ms = num(a1);
        const interior = resto(2);
        if (ms === null || !interior) return this.#err('Uso: en <ms> <comando>');
        this.programados.push({ cuando: Date.now() + ms, comando: interior });
        return this.#ok(`Programado para dentro de ${ms} ms: ${interior}`);
      }
      case 'secuencia': {
        const cuerpo = resto(1);
        if (!cuerpo) return this.#err('Uso: secuencia cmd1 ; espera 500 ; cmd2');
        let desfase = 0, cuantos = 0;
        for (const tramo of cuerpo.split(';').map((t) => t.trim()).filter(Boolean)) {
          const m = /^espera\s+(\d+)$/i.exec(tramo);
          if (m) { desfase += parseInt(m[1], 10); continue; }
          if (desfase === 0) this.#procesar(tramo);
          else this.programados.push({ cuando: Date.now() + desfase, comando: tramo });
          cuantos++;
        }
        return this.#ok(`Secuencia aceptada: ${cuantos} comandos`);
      }
      case 'cancelar': {
        const n = this.programados.length;
        this.programados = [];
        return this.#ok(`Cancelados ${n} comandos programados`);
      }

      // ---------------- diagnostico ----------------
      case 'test':
        if (a1 === 'leds') {
          let t = 0;
          for (const nombre of LEDS) {
            this.programados.push({ cuando: Date.now() + t, comando: 'leds off' });
            this.programados.push({ cuando: Date.now() + t + 30, comando: `led ${nombre} on` });
            t += 400;
          }
          this.programados.push({ cuando: Date.now() + t, comando: 'leds auto' });
          return this.#ok('Test de LEDs: barrido de 6 LEDs (~2.5 s)');
        }
        if (a1 === 'sensores') {
          return this.#ok(`CNY: ${this.cny.join('')} | LDR: ${this.ldr.join(' ')} | CO2: ${this.co2}`);
        }
        return this.#err('Uso: test <leds|sensores>');
      case 'ayuda':
        return this.#ok('Consulta COMANDOS.md o escribe "estado".');
      case 'cny': {
        // El firmware acepta 1/0 como alias de on/off (ver cmdCny); el catalogo
        // tambien los declara validos, asi que aqui deben pasar igual.
        const modo = a2 === 'on' || a2 === '1' ? 'on'
          : a2 === 'off' || a2 === '0' ? 'off'
          : a2 === 'auto' ? 'auto' : null;
        if (!modo) return this.#err('Uso: cny <1-6|c1|c2|all> <on|off|auto>');
        const rango =
          a1 === 'all' ? [0, 1, 2, 3, 4, 5] :
          a1 === 'c1' ? [0, 1, 2] :
          a1 === 'c2' ? [3, 4, 5] :
          num(a1) >= 1 && num(a1) <= 6 ? [num(a1) - 1] : null;
        if (!rango) return this.#err('Uso: cny <1-6|c1|c2|all> <on|off|auto>');
        rango.forEach((i) => (this.modoCny[i] = modo));
        return this.#ok(`CNY ${a1} -> ${a2}`);
      }
      case 'ldr': {
        const v = a2 === 'auto' ? -1 : Math.min(4095, num(a2) ?? -2);
        if (v === -2) return this.#err('Uso: ldr <1|2|all> <0-4095|auto>');
        if (a1 === 'all') { this.simLdr = [v, v]; return this.#ok(`LDR1 y LDR2 -> ${a2}`); }
        if (a1 === '1' || a1 === '2') { this.simLdr[+a1 - 1] = v; return this.#ok(`LDR${a1} -> ${a2}`); }
        return this.#err('Uso: ldr <1|2|all> <0-4095|auto>');
      }
      case 'co2': {
        if (a1 === 'auto') { this.simCo2 = -1; return this.#ok('CO2 -> lectura real'); }
        if (a1 === 'alto') { this.simCo2 = this.cfg.umbralco2 + 500; return this.#ok(`CO2 -> ${this.simCo2} (ALTO)`); }
        if (a1 === 'bajo') { this.simCo2 = 100; return this.#ok('CO2 -> 100 (BAJO)'); }
        const v = num(a1);
        if (v === null) return this.#err('Uso: co2 <0-4095|auto|alto|bajo>');
        this.simCo2 = Math.min(4095, v);
        return this.#ok(`CO2 -> ${this.simCo2}`);
      }
      case 'noche': {
        if (a1 === 'on') { this.simNoche = 1; return this.#ok('Modo noche FORZADO'); }
        if (a1 === 'off') { this.simNoche = 0; return this.#ok('Modo dia FORZADO'); }
        if (a1 === 'auto') { this.simNoche = -1; return this.#ok('Dia/noche segun LDR'); }
        return this.#err('Uso: noche <on|off|auto>');
      }
      case 'hora': {
        if (a1 === undefined) {
          if (this.horaIndicada < 0) return this.#ok('Sin hora indicada. Uso: hora <0-23>');
          return this.#ok(`Hora indicada: ${this.horaIndicada}h` +
            (this.horaMadrugada ? ' (dentro de 23h-4h)' : ' (fuera de 23h-4h)'));
        }
        if (a1 === 'off' || a1 === 'auto' || a1 === 'limpiar') {
          this.horaIndicada = -1;
          this.horaMadrugada = false;
          this.avisoHora = false;
          return this.#ok('Hora olvidada: no habra noche profunda');
        }
        const h = num(a1);
        if (h === null) return this.#err('Uso: hora <0-23>  /  hora off');
        if (h > 23) return this.#err('Hora fuera de rango. Usa 0 a 23.');
        this.horaIndicada = h;
        this.horaMadrugada = h >= 23 || h <= 4;
        this.avisoHora = false;
        return this.#ok(`Hora recibida: ${h}h` +
          (this.horaMadrugada ? ' (dentro de 23h-4h: madrugada)' : ' (fuera de ese rango)'));
      }
      case 'p1': this.ped[0] = true; return this.emit('consola', { nivel: 'evento', texto: '[EVENTO] Peaton solicito cruce en Calle 1' });
      case 'p2': this.ped[1] = true; return this.emit('consola', { nivel: 'evento', texto: '[EVENTO] Peaton solicito cruce en Calle 2' });
      case 'combo': this.pantalla = (this.pantalla % 4) + 1; return this.emit('consola', { nivel: 'evento', texto: `[EVENTO] Pantalla LCD -> M${this.pantalla}` });
      case 'pantalla': {
        const n = num(a1);
        if (n >= 1 && n <= 4) { this.pantalla = n; return this.#ok(`Pantalla LCD -> M${n}`); }
        return this.#err('Uso: pantalla <1-4>');
      }
      case 'lcd':
        if (a1 === 'texto' || a1 === 'msg') {
          const mensaje = resto(2);
          if (!mensaje) return this.#err('Uso: lcd texto <mensaje>');
          this.lcdTexto = mensaje;
          return this.#ok(`LCD muestra: ${mensaje}`);
        }
        if (a1 === 'limpiar' || a1 === 'clear') {
          this.lcdTexto = '';
          return this.#ok('LCD vuelve a las pantallas normales');
        }
        if (a1 === 'on' || a1 === 'off') { this.lcdOn = a1 === 'on'; return this.#ok(`LCD ${a1}`); }
        return this.#err('Uso: lcd <on|off|limpiar> / lcd texto <mensaje>');
      case 'botones':
        if (a1 === 'on' || a1 === 'off') { this.botones = a1 === 'on'; return this.#ok(`Botones fisicos ${a1}`); }
        return this.#err('Uso: botones <on|off>');
      case 'sem':
        if (a1 === 'auto') { this.prioridad = 0; this.semAuto = true; this.inicioFase = Date.now(); return this.#ok('Semaforo AUTOMATICO'); }
        if (a1 === 'manual' || a1 === 'pausa') { this.semAuto = false; return this.#ok('Semaforo MANUAL'); }
        return this.#err('Uso: sem <auto|manual>');
      case 'fase': {
        const destino = a1 === 'sig' || a1 === 'next'
          ? FASES[(FASES.indexOf(this.fase) + 1) % 6]
          : FASES.includes(a1) ? a1 : null;
        if (!destino) return this.#err('Uso: fase <v1|a1|r1|v2|a2|r2|sig>');
        this.#cambiarFase(destino);
        return this.#ok(`Fase -> ${NOMBRES[destino]}`);
      }
      case 'led': {
        const nombres = ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'];
        const i = nombres.indexOf(a1);
        if (i < 0) return this.#err('LED valido: lr1 ly1 lg1 lr2 ly2 lg2');
        const v = a2 === 'auto' ? -1 : a2 === 'on' ? 255 : a2 === 'off' ? 0 : num(a2);
        if (v === null) return this.#err('Uso: led <lr1..lg2> <on|off|0-255|auto>');
        this.simLed[i] = Math.min(255, v);
        return this.#ok(`${a1} -> ${a2}`);
      }
      case 'leds': {
        const v = a1 === 'auto' ? -1 : a1 === 'on' ? 255 : a1 === 'off' ? 0 : null;
        if (v === null) return this.#err('Uso: leds <auto|on|off>');
        this.simLed = Array(6).fill(v);
        return this.#ok(`Todos los LEDs -> ${a1}`);
      }
      case 'set': {
        const v = num(a2);
        if (v === null || !(a1 in this.cfg)) return this.#err(`Parametro desconocido: '${a1}'`);
        this.cfg[a1] = v;
        return this.#ok(`${a1} = ${v}`);
      }
      case 'escenario': {
        const e = {
          trafico1: () => { this.modoCny = ['on', 'on', 'on', 'off', 'off', 'off']; },
          trafico2: () => { this.modoCny = ['off', 'off', 'off', 'on', 'on', 'on']; },
          noche: () => { this.simLdr = [100, 100]; this.simNoche = -1; },
          dia: () => { this.simLdr = [3000, 3000]; this.simNoche = -1; },
          madrugada: () => {
            this.simLdr = [100, 100]; this.simNoche = -1;
            this.horaIndicada = 2; this.horaMadrugada = true; this.avisoHora = false;
          },
          contaminacion: () => { this.simCo2 = this.cfg.umbralco2 + 500; this.modoCny = Array(6).fill('on'); },
          vacio: () => { this.modoCny = Array(6).fill('off'); this.simCo2 = 100; },
          intermitente: () => {
            this.semAuto = false;
            this.simLed = Array(6).fill(0);
            this.simLed[1] = 255; this.simLed[4] = 255;
            this.parpadeo = Array(6).fill(0);
            this.parpadeo[1] = 700; this.parpadeo[4] = 700;
          },
          horapico: () => {
            this.flujo = [40, 35];
            this.ruido = true;
            this.modoCny = Array(6).fill('auto');
          }
        }[a1];
        if (!e) return this.#err('Escenarios: trafico1 trafico2 noche dia madrugada contaminacion vacio intermitente horapico');
        e();
        return this.#ok(`Escenario: ${a1}`);
      }
      case 'reset':
      case 'auto':
        this.modoCny = Array(6).fill('auto');
        this.simLed = Array(6).fill(-1);
        this.parpadeo = Array(6).fill(0);
        this.flujoApagarEn = Array(6).fill(0);
        this.simLdr = [-1, -1];
        this.simCo2 = -1;
        this.simNoche = -1;
        this.brilloMaestro = 255;
        this.flujo = [0, 0];
        this.ruido = false;
        this.prioridad = 0;
        this.prioridadDuracion = 0;
        this.watchdogLimite = 0;
        this.seguro = true;
        this.semAuto = true;
        this.botones = true;
        this.ped = [false, false];
        this.lcdTexto = '';
        this.programados = [];
        this.horaIndicada = -1;
        this.horaMadrugada = false;
        this.nocheProfunda = false;
        this.avisoHora = false;
        return this.#ok('Todo en AUTO');
      case 'mon':
      case 'json':
        return; // el simulador ya emite telemetria continuamente
      case 'estado':
        return this.#ok(`Fase ${NOMBRES[this.fase]} | autos ${this.autos.join('/')} | CO2 ${this.co2}`);
      default:
        return this.#err(`Comando desconocido: '${cmd}'`);
    }
  }

  cerrar() {
    clearInterval(this.tickLogica);
    clearInterval(this.tickTelemetria);
    clearInterval(this.tickFisica);
  }
}
