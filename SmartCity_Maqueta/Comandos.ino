/* ============================================================================
   CIUDAD AUTOADAPTABLE - CAPA DE COMANDOS
   ============================================================================
   Este archivo es el UNICO lugar donde se define lo que la maqueta sabe hacer.
   El sketch principal (SmartCity_Maqueta.ino) tiene el hardware, la maquina de
   estados y la pantalla; aqui esta todo el vocabulario.

   El Arduino IDE junta automaticamente los .ino de la carpeta, asi que las
   variables globales del archivo principal se ven desde aqui sin necesidad
   de headers.

   CONTRATO DEL PROTOCOLO (lo usa el puente y el dashboard):
     - Entra: una linea de texto por comando.
     - Sale : toda linea que empieza por '{' es telemetria JSON;
              cualquier otra linea es texto de consola con prefijo
              [OK] / [ERROR] / [EVENTO].

   CATEGORIAS (el catalogo del puente usa exactamente estos nombres, para
   poder mandarle al modelo solo la parte que le interesa):
     sistema, semaforo, emergencia, luces, sensores, ambiente,
     peatones, pantalla, tiempos, escenarios, programacion, diagnostico
   ========================================================================= */

// Apagado automatico de los carros que genera el comando "flujo"
unsigned long flujoApagarEn[6] = { 0, 0, 0, 0, 0, 0 };

// ============================================================================
// LECTURA DE LA CONSOLA
// ============================================================================
void atenderSerial() {
  static unsigned long ultimoCaracter = 0;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    ultimoCaracter = millis();
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        procesarComando(bufferSerial);
        bufferSerial = "";
      }
    } else {
      if (bufferSerial.length() < 120) bufferSerial += c;
    }
  }

  // Red de seguridad: si el Monitor Serie esta en "Sin ajuste de linea" nunca
  // llega '\n'. Si hay texto pendiente y llevan 150 ms sin llegar caracteres,
  // se ejecuta igual.
  if (bufferSerial.length() > 0 && (millis() - ultimoCaracter) > 150) {
    procesarComando(bufferSerial);
    bufferSerial = "";
  }
}

// ============================================================================
// UTILIDADES DE TEXTO
// ============================================================================

// Devuelve la palabra numero n (0,1,2,3...) de la linea.
String palabra(String linea, int n) {
  int inicio = 0;
  int cuenta = 0;
  while (inicio < (int)linea.length()) {
    while (inicio < (int)linea.length() && linea.charAt(inicio) == ' ') inicio++;
    int fin = linea.indexOf(' ', inicio);
    if (fin < 0) fin = linea.length();
    if (cuenta == n) return linea.substring(inicio, fin);
    cuenta++;
    inicio = fin;
  }
  return "";
}

// Devuelve todo lo que sigue despues de la palabra n (sin partirlo).
// Se usa para "lcd texto Hola mundo" y para "secuencia a ; b ; c".
String restoDeLinea(String linea, int desde) {
  int inicio = 0;
  int cuenta = 0;
  while (inicio < (int)linea.length()) {
    while (inicio < (int)linea.length() && linea.charAt(inicio) == ' ') inicio++;
    if (cuenta == desde) {
      String r = linea.substring(inicio);
      r.trim();
      return r;
    }
    int fin = linea.indexOf(' ', inicio);
    if (fin < 0) return "";
    cuenta++;
    inicio = fin;
  }
  return "";
}

bool esNumero(String s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s.charAt(i))) return false;
  }
  return true;
}

void avisoOk(String msg)    { Serial.println("[OK] " + msg); }
void avisoError(String msg) { Serial.println("[ERROR] " + msg); }
void avisoEvento(String msg){ Serial.println("[EVENTO] " + msg); }

int limitar(int valor, int minimo, int maximo) {
  if (valor < minimo) return minimo;
  if (valor > maximo) return maximo;
  return valor;
}

// Traduce "c1"/"1"/"calle1" -> 1 y "c2"/"2"/"calle2" -> 2. Devuelve 0 si no aplica.
int calleDesdeTexto(String t) {
  if (t == "c1" || t == "1" || t == "calle1") return 1;
  if (t == "c2" || t == "2" || t == "calle2") return 2;
  return 0;
}

// Indice del LED por nombre: lr1 ly1 lg1 lr2 ly2 lg2. -1 si no existe.
int indiceLed(String nombre) {
  for (int i = 0; i < 6; i++) if (nombre == NOM_LED[i]) return i;
  return -1;
}

/* ============================================================================
   ENRUTADOR DE COMANDOS
   ------------------------------------------------------------------------
   Toda linea nueva entra aqui. Se conserva el texto original (para el LCD,
   que si distingue mayusculas) y se trabaja con una copia en minusculas.
   ========================================================================= */
void procesarComando(String lineaOriginal) {
  lineaOriginal.trim();
  if (lineaOriginal.length() == 0) return;

  String linea = lineaOriginal;
  linea.toLowerCase();

  Serial.println("> " + linea);

  String cmd = palabra(linea, 0);
  String a1  = palabra(linea, 1);
  String a2  = palabra(linea, 2);
  String a3  = palabra(linea, 3);

  // ---- sistema ----
  if (cmd == "ayuda" || cmd == "help" || cmd == "?")  { mostrarAyuda(); return; }
  if (cmd == "estado" || cmd == "status")             { mostrarEstado(); return; }
  if (cmd == "json")                                  { emitirJson(); return; }
  if (cmd == "reset" || cmd == "auto")                { resetSimulaciones(); return; }
  if (cmd == "guardar")                               { cmdGuardar(a1); return; }
  if (cmd == "restaurar")                             { cmdRestaurar(a1); return; }
  if (cmd == "watchdog")                              { cmdWatchdog(a1); return; }
  if (cmd == "seguro")                                { cmdSeguro(a1); return; }
  if (cmd == "mon")                                   { cmdMon(a1); return; }

  // ---- semaforo ----
  if (cmd == "sem")                                   { cmdSem(a1); return; }
  if (cmd == "fase")                                  { cmdFase(a1); return; }

  // ---- emergencia ----
  if (cmd == "prio")                                  { cmdPrioridad(a1, a2); return; }
  if (cmd == "panico")                                { cmdPanico(); return; }
  if (cmd == "apagar")                                { cmdApagar(); return; }

  // ---- luces ----
  if (cmd == "led")                                   { cmdLed(a1, a2); return; }
  if (cmd == "leds")                                  { cmdLeds(a1); return; }
  if (cmd == "brillo")                                { cmdBrillo(a1); return; }
  if (cmd == "parpadeo")                              { cmdParpadeo(a1, a2); return; }
  if (cmd == "fade")                                  { cmdFade(a1, a2, a3); return; }

  // ---- sensores ----
  if (cmd == "cny")                                   { cmdCny(a1, a2); return; }
  if (cmd == "flujo")                                 { cmdFlujo(a1, a2); return; }
  if (cmd == "ruido")                                 { cmdRuido(a1); return; }

  // ---- ambiente ----
  if (cmd == "ldr")                                   { cmdLdr(a1, a2); return; }
  if (cmd == "co2")                                   { cmdCo2(a1); return; }
  if (cmd == "noche")                                 { cmdNoche(a1); return; }

  // ---- peatones ----
  if (cmd == "p1")                                    { pedirPeaton(1); return; }
  if (cmd == "p2")                                    { pedirPeaton(2); return; }
  if (cmd == "peaton")                                { cmdPeaton(a1); return; }
  if (cmd == "botones")                               { cmdBotones(a1); return; }

  // ---- pantalla ----
  if (cmd == "combo")                                 { cambiarModoPantalla(); return; }
  if (cmd == "pantalla")                              { cmdPantalla(a1); return; }
  if (cmd == "lcd")                                   { cmdLcd(a1, lineaOriginal); return; }

  // ---- tiempos ----
  if (cmd == "set")                                   { cmdSet(a1, a2); return; }

  // ---- escenarios ----
  if (cmd == "escenario")                             { cmdEscenario(a1); return; }

  // ---- programacion ----
  if (cmd == "en")                                    { cmdEn(a1, restoDeLinea(lineaOriginal, 2)); return; }
  if (cmd == "secuencia")                             { cmdSecuencia(restoDeLinea(lineaOriginal, 1)); return; }
  if (cmd == "cancelar")                              { cmdCancelar(); return; }

  // ---- diagnostico ----
  if (cmd == "test")                                  { cmdTest(a1); return; }

  avisoError("Comando desconocido: '" + cmd + "'. Escribe 'ayuda'.");
}

/* ============================================================================
   CATEGORIA: EMERGENCIA
   ========================================================================= */

// prio <c1|c2> <segundos|inf>   -> sostiene el verde de esa calle
// prio off                      -> devuelve el control al ciclo normal
void cmdPrioridad(String cual, String cuanto) {
  if (cual == "off" || cual == "fin") {
    if (prioridadCalle == 0) { avisoError("No hay ninguna prioridad activa"); return; }
    terminarPrioridad("Prioridad cancelada por comando");
    return;
  }

  int calle = calleDesdeTexto(cual);
  if (calle == 0) { avisoError("Uso: prio <c1|c2> <segundos|inf>  /  prio off"); return; }

  // Antes de anular nada, guardamos el estado en el slot 0 para poder
  // devolverlo despues exactamente como estaba.
  guardarInstantanea(0);

  prioridadCalle = calle;
  prioridadInicio = millis();

  if (cuanto == "inf" || cuanto == "indefinido" || cuanto == "") {
    prioridadDuracion = 0;
    watchdogLimite = 0;              // el usuario pidio explicitamente que no expire
    avisoOk("PRIORIDAD indefinida para Calle " + String(calle) +
            ". Usa 'prio off' para terminarla.");
  } else if (esNumero(cuanto)) {
    long seg = cuanto.toInt();
    if (seg < 1) seg = 1;
    prioridadDuracion = (unsigned long)seg * 1000UL;
    // Red de seguridad: aunque falle todo lo demas, a los 5 s de vencer
    // el watchdog restaura el sistema.
    armarWatchdog((unsigned long)seg + 5);
    avisoOk("PRIORIDAD " + String(seg) + " s para Calle " + String(calle));
  } else {
    avisoError("Uso: prio <c1|c2> <segundos|inf>");
    return;
  }

  semaforoAutomatico = false;
  estadoActual = (calle == 1) ? VERDE_CALLE1 : VERDE_CALLE2;
  tiempoInicioFase = millis();
  aplicarSemaforos();
}

// Se llama desde el loop: mantiene el verde y vigila el vencimiento.
void atenderPrioridad() {
  if (prioridadCalle == 0) return;

  if (prioridadDuracion > 0 && (millis() - prioridadInicio) >= prioridadDuracion) {
    terminarPrioridad("La prioridad se cumplio y vencio");
    return;
  }

  EstadoCruce objetivo = (prioridadCalle == 1) ? VERDE_CALLE1 : VERDE_CALLE2;
  if (estadoActual != objetivo) {
    estadoActual = objetivo;
    tiempoInicioFase = millis();
  }
}

void terminarPrioridad(String motivo) {
  prioridadCalle = 0;
  prioridadDuracion = 0;
  watchdogLimite = 0;
  lcdTextoLibre = "";
  if (lcdPresente) lcd.clear();
  restaurarInstantanea(0);
  avisoEvento(motivo + ". Sistema restaurado.");
}

// panico: todo en rojo, ciclo detenido. Es el estado seguro del cruce.
void cmdPanico() {
  prioridadCalle = 0;
  semaforoAutomatico = false;
  estadoActual = TODO_ROJO_1a2;
  tiempoInicioFase = millis();
  for (int i = 0; i < 6; i++) { simLed[i] = -1; parpadeoPeriodo[i] = 0; fadeActivo[i] = false; }
  aplicarSemaforos();
  avisoOk("PANICO: ambas calles en rojo, ciclo detenido. 'sem auto' para reanudar.");
}

// apagar: todos los LEDs apagados (el cruce queda "sin energia")
void cmdApagar() {
  for (int i = 0; i < 6; i++) { simLed[i] = 0; parpadeoPeriodo[i] = 0; fadeActivo[i] = false; }
  aplicarSemaforos();
  avisoOk("Todos los LEDs apagados. 'leds auto' para devolverlos al semaforo.");
}

/* ============================================================================
   CATEGORIA: SISTEMA (instantaneas, watchdog, interlock)
   ========================================================================= */

void guardarInstantanea(int slot) {
  if (slot < 0 || slot >= MAX_SLOTS) return;
  Instantanea &s = instantaneas[slot];
  s.usada = true;
  for (int i = 0; i < 6; i++) {
    s.modoCny[i] = modoCny[i];
    s.simLed[i] = simLed[i];
    s.parpadeoPeriodo[i] = parpadeoPeriodo[i];
  }
  s.simLdr[0] = simLdr[0];  s.simLdr[1] = simLdr[1];
  s.simCo2 = simCo2;
  s.simNoche = simNoche;
  s.semaforoAutomatico = semaforoAutomatico;
  s.botonesFisicosActivos = botonesFisicosActivos;
  s.brilloMaestro = brilloMaestro;
  s.verdeMinimo = verdeMinimo;
  s.verdeMaximo = verdeMaximo;
  s.extensionPorAuto = extensionPorAuto;
  s.tiempoAmarillo = tiempoAmarillo;
  s.tiempoTodoRojo = tiempoTodoRojo;
  s.umbralNoche = umbralNoche;
  s.brilloDia = brilloDia;
  s.brilloNoche = brilloNoche;
  s.umbralCo2Alto = umbralCo2Alto;
  s.modoPantalla = modoPantalla;
}

bool restaurarInstantanea(int slot) {
  if (slot < 0 || slot >= MAX_SLOTS) return false;
  Instantanea &s = instantaneas[slot];
  if (!s.usada) return false;
  for (int i = 0; i < 6; i++) {
    modoCny[i] = s.modoCny[i];
    simLed[i] = s.simLed[i];
    parpadeoPeriodo[i] = s.parpadeoPeriodo[i];
    fadeActivo[i] = false;
  }
  simLdr[0] = s.simLdr[0];  simLdr[1] = s.simLdr[1];
  simCo2 = s.simCo2;
  simNoche = s.simNoche;
  semaforoAutomatico = s.semaforoAutomatico;
  botonesFisicosActivos = s.botonesFisicosActivos;
  brilloMaestro = s.brilloMaestro;
  verdeMinimo = s.verdeMinimo;
  verdeMaximo = s.verdeMaximo;
  extensionPorAuto = s.extensionPorAuto;
  tiempoAmarillo = s.tiempoAmarillo;
  tiempoTodoRojo = s.tiempoTodoRojo;
  umbralNoche = s.umbralNoche;
  brilloDia = s.brilloDia;
  brilloNoche = s.brilloNoche;
  umbralCo2Alto = s.umbralCo2Alto;
  modoPantalla = s.modoPantalla;
  tiempoInicioFase = millis();
  aplicarSemaforos();
  return true;
}

void cmdGuardar(String slotTexto) {
  int slot = esNumero(slotTexto) ? slotTexto.toInt() : 1;
  if (slot < 0 || slot >= MAX_SLOTS) { avisoError("Slot valido: 0 a " + String(MAX_SLOTS - 1)); return; }
  guardarInstantanea(slot);
  avisoOk("Estado completo guardado en el slot " + String(slot));
}

void cmdRestaurar(String slotTexto) {
  int slot = esNumero(slotTexto) ? slotTexto.toInt() : 1;
  if (slot < 0 || slot >= MAX_SLOTS) { avisoError("Slot valido: 0 a " + String(MAX_SLOTS - 1)); return; }
  if (restaurarInstantanea(slot)) avisoOk("Estado restaurado desde el slot " + String(slot));
  else avisoError("El slot " + String(slot) + " esta vacio");
}

void armarWatchdog(unsigned long segundos) {
  watchdogLimite = segundos * 1000UL;
  watchdogUltimo = millis();
}

void cmdWatchdog(String valor) {
  if (valor == "off" || valor == "0") {
    watchdogLimite = 0;
    avisoOk("Watchdog desactivado");
    return;
  }
  if (esNumero(valor)) {
    armarWatchdog(valor.toInt());
    avisoOk("Watchdog armado: si nadie lo renueva, en " + valor + " s el sistema se restaura solo");
    return;
  }
  avisoError("Uso: watchdog <segundos|off>");
}

// Se llama desde el loop.
void atenderWatchdog() {
  if (watchdogLimite == 0) return;
  if ((millis() - watchdogUltimo) < watchdogLimite) return;

  watchdogLimite = 0;
  if (prioridadCalle != 0) {
    terminarPrioridad("WATCHDOG: nadie renovo la prioridad");
  } else {
    resetSimulaciones();
    avisoEvento("WATCHDOG: se vencio el plazo, todo vuelve a AUTO.");
  }
}

void cmdSeguro(String valor) {
  if (valor == "on")  { seguroActivo = true;  avisoOk("Interlock ACTIVO: nunca habra dos verdes al tiempo"); return; }
  if (valor == "off") { seguroActivo = false; avisoOk("Interlock DESACTIVADO: se permiten verdes simultaneos (solo para pruebas)"); return; }
  avisoError("Uso: seguro <on|off>");
}

/* ============================================================================
   CATEGORIA: LUCES
   ========================================================================= */

// brillo <0-255|0-100%>  -> atenuacion global de todos los LEDs
void cmdBrillo(String valor) {
  if (valor.endsWith("%")) {
    String num = valor.substring(0, valor.length() - 1);
    if (!esNumero(num)) { avisoError("Uso: brillo <0-255> o brillo <0-100>%"); return; }
    brilloMaestro = limitar((num.toInt() * 255) / 100, 0, 255);
    avisoOk("Brillo maestro = " + String(brilloMaestro) + " (" + num + "%)");
    return;
  }
  if (!esNumero(valor)) { avisoError("Uso: brillo <0-255> o brillo <0-100>%"); return; }
  brilloMaestro = limitar(valor.toInt(), 0, 255);
  avisoOk("Brillo maestro = " + String(brilloMaestro));
}

// parpadeo <led|sem1|sem2|all> <ms|off>
void cmdParpadeo(String cual, String periodo) {
  unsigned long ms;
  if (periodo == "off" || periodo == "0") ms = 0;
  else if (esNumero(periodo)) {
    ms = periodo.toInt();
    if (ms < 50) ms = 50;
  } else { avisoError("Uso: parpadeo <lr1..lg2|sem1|sem2|all> <ms|off>"); return; }

  int desde = -1, hasta = -1;
  if (cual == "all" || cual == "todos") { desde = 0; hasta = 5; }
  else if (cual == "sem1") { desde = 0; hasta = 2; }
  else if (cual == "sem2") { desde = 3; hasta = 5; }
  else {
    int i = indiceLed(cual);
    if (i < 0) { avisoError("Uso: parpadeo <lr1..lg2|sem1|sem2|all> <ms|off>"); return; }
    desde = i; hasta = i;
  }

  for (int i = desde; i <= hasta; i++) {
    parpadeoPeriodo[i] = ms;
    parpadeoEncendido[i] = true;
    parpadeoUltimo[i] = millis();
  }
  avisoOk("Parpadeo de " + cual + (ms == 0 ? " apagado" : " cada " + String(ms) + " ms"));
}

// fade <led> <destino 0-255> <ms>  -> rampa suave de brillo
void cmdFade(String cual, String destino, String duracion) {
  int i = indiceLed(cual);
  if (i < 0) { avisoError("LED valido: lr1 ly1 lg1 lr2 ly2 lg2"); return; }
  if (!esNumero(destino) || !esNumero(duracion)) {
    avisoError("Uso: fade <lr1..lg2> <0-255> <ms>");
    return;
  }
  fadeDesde[i] = ledActual[i];
  fadeHasta[i] = limitar(destino.toInt(), 0, 255);
  fadeDuracion[i] = (unsigned long)duracion.toInt();
  if (fadeDuracion[i] < 50) fadeDuracion[i] = 50;
  fadeInicio[i] = millis();
  fadeActivo[i] = true;
  avisoOk("Fade de " + cual + " hasta " + String(fadeHasta[i]) + " en " + duracion + " ms");
}

void cmdLed(String cual, String valor) {
  int indice = indiceLed(cual);
  if (indice < 0) { avisoError("LED valido: lr1 ly1 lg1 lr2 ly2 lg2"); return; }

  fadeActivo[indice] = false;
  if (valor == "auto") { simLed[indice] = -1;  avisoOk(cual + " -> sigue al semaforo"); return; }
  if (valor == "on")   { simLed[indice] = 255; avisoOk(cual + " -> ENCENDIDO fijo"); return; }
  if (valor == "off")  { simLed[indice] = 0;   avisoOk(cual + " -> APAGADO fijo"); return; }
  if (esNumero(valor)) {
    simLed[indice] = limitar(valor.toInt(), 0, 255);
    avisoOk(cual + " -> PWM " + String(simLed[indice]));
    return;
  }
  avisoError("Uso: led <lr1..lg2> <on|off|0-255|auto>");
}

void cmdLeds(String valor) {
  int v;
  if (valor == "auto")     v = -1;
  else if (valor == "off") v = 0;
  else if (valor == "on")  v = 255;
  else { avisoError("Uso: leds <auto|on|off>"); return; }

  for (int i = 0; i < 6; i++) { simLed[i] = v; fadeActivo[i] = false; }
  avisoOk(v == -1 ? "Todos los LEDs siguen al semaforo"
                  : (v == 0 ? "Todos los LEDs APAGADOS" : "Todos los LEDs ENCENDIDOS"));
}

/* ============================================================================
   CATEGORIA: SENSORES (trafico)
   ========================================================================= */

void cmdCny(String cual, String valor) {
  ModoCny modo;
  if      (valor == "on"   || valor == "1") modo = CNY_ON;
  else if (valor == "off"  || valor == "0") modo = CNY_OFF;
  else if (valor == "auto")                 modo = CNY_AUTO;
  else { avisoError("Uso: cny <1-6|all|c1|c2> <on|off|auto>"); return; }

  int desde = -1, hasta = -1;
  if (cual == "all" || cual == "todos") { desde = 0; hasta = 5; }
  else if (cual == "c1") { desde = 0; hasta = 2; }
  else if (cual == "c2") { desde = 3; hasta = 5; }
  else if (esNumero(cual)) {
    int n = cual.toInt();
    if (n >= 1 && n <= 6) { desde = n - 1; hasta = n - 1; }
  }
  if (desde < 0) { avisoError("Uso: cny <1-6|all|c1|c2> <on|off|auto>"); return; }

  for (int i = desde; i <= hasta; i++) { modoCny[i] = modo; flujoApagarEn[i] = 0; }
  avisoOk("CNY " + cual + " -> " + valor);
}

// flujo <c1|c2|off> <autos por minuto>  -> trafico que llega y se va solo
void cmdFlujo(String cual, String tasa) {
  if (cual == "off") {
    flujoPorMinuto[0] = 0; flujoPorMinuto[1] = 0;
    for (int i = 0; i < 6; i++) flujoApagarEn[i] = 0;
    avisoOk("Flujo de trafico automatico apagado");
    return;
  }
  int calle = calleDesdeTexto(cual);
  if (calle == 0 || !esNumero(tasa)) {
    avisoError("Uso: flujo <c1|c2> <autos por minuto>  /  flujo off");
    return;
  }
  flujoPorMinuto[calle - 1] = limitar(tasa.toInt(), 0, 120);
  flujoUltimo[calle - 1] = millis();
  avisoOk("Flujo en Calle " + String(calle) + ": " + tasa + " autos/min");
}

// Se llama desde el loop: hace aparecer y desaparecer carros solos.
void atenderFlujoTrafico() {
  unsigned long ahora = millis();

  // Apagar los carros cuyo tiempo ya paso
  for (int i = 0; i < 6; i++) {
    if (flujoApagarEn[i] != 0 && ahora >= flujoApagarEn[i]) {
      flujoApagarEn[i] = 0;
      modoCny[i] = CNY_OFF;
    }
  }

  // Hacer aparecer carros nuevos segun la tasa de cada calle
  for (int calle = 0; calle < 2; calle++) {
    if (flujoPorMinuto[calle] <= 0) continue;
    unsigned long intervalo = 60000UL / (unsigned long)flujoPorMinuto[calle];
    if (ahora - flujoUltimo[calle] < intervalo) continue;
    flujoUltimo[calle] = ahora;

    int base = calle * 3;
    int sensor = base + (int)random(0, 3);
    modoCny[sensor] = CNY_ON;
    flujoApagarEn[sensor] = ahora + (unsigned long)random(1200, 3200);
  }
}

void cmdRuido(String valor) {
  if (valor == "on")  { ruidoSensores = true;  avisoOk("Ruido de sensores ACTIVO (lecturas mas realistas)"); return; }
  if (valor == "off") { ruidoSensores = false; avisoOk("Ruido de sensores apagado"); return; }
  avisoError("Uso: ruido <on|off>");
}

/* ============================================================================
   CATEGORIA: AMBIENTE
   ========================================================================= */

void cmdLdr(String cual, String valor) {
  int v;
  if (valor == "auto") v = -1;
  else if (esNumero(valor)) v = limitar(valor.toInt(), 0, 4095);
  else { avisoError("Uso: ldr <1|2|all> <0-4095|auto>"); return; }

  if (cual == "all" || cual == "todos") {
    simLdr[0] = v; simLdr[1] = v;
    avisoOk("LDR1 y LDR2 -> " + valor);
    return;
  }
  if (cual == "1" || cual == "2") {
    simLdr[cual.toInt() - 1] = v;
    avisoOk("LDR" + cual + " -> " + valor);
    return;
  }
  avisoError("Uso: ldr <1|2|all> <0-4095|auto>");
}

void cmdCo2(String valor) {
  if (valor == "auto") { simCo2 = -1; avisoOk("CO2 -> lectura real"); return; }
  if (valor == "alto") { simCo2 = umbralCo2Alto + 500; avisoOk("CO2 -> " + String(simCo2) + " (ALTO)"); return; }
  if (valor == "bajo") { simCo2 = 100; avisoOk("CO2 -> 100 (BAJO)"); return; }
  if (esNumero(valor)) {
    simCo2 = limitar(valor.toInt(), 0, 4095);
    avisoOk("CO2 -> " + String(simCo2));
    return;
  }
  avisoError("Uso: co2 <0-4095|auto|alto|bajo>");
}

void cmdNoche(String valor) {
  if (valor == "on")   { simNoche = 1;  avisoOk("Modo noche FORZADO"); return; }
  if (valor == "off")  { simNoche = 0;  avisoOk("Modo dia FORZADO");   return; }
  if (valor == "auto") { simNoche = -1; avisoOk("Dia/noche segun LDR"); return; }
  avisoError("Uso: noche <on|off|auto>");
}

/* ============================================================================
   CATEGORIA: PEATONES
   ========================================================================= */

void cmdPeaton(String cual) {
  if (cual == "limpiar" || cual == "cancelar") {
    solicitudPeaton1 = false;
    solicitudPeaton2 = false;
    avisoOk("Solicitudes peatonales canceladas");
    return;
  }
  int calle = calleDesdeTexto(cual);
  if (calle == 0) { avisoError("Uso: peaton <c1|c2|limpiar>"); return; }
  pedirPeaton(calle);
}

void cmdBotones(String valor) {
  if (valor == "on")  { botonesFisicosActivos = true;  avisoOk("Botones fisicos habilitados"); return; }
  if (valor == "off") { botonesFisicosActivos = false; avisoOk("Botones fisicos ignorados (usa p1/p2)"); return; }
  avisoError("Uso: botones <on|off>");
}

/* ============================================================================
   CATEGORIA: PANTALLA
   ========================================================================= */

void cmdPantalla(String valor) {
  if (esNumero(valor)) {
    int n = valor.toInt();
    if (n >= 1 && n <= NUM_MODOS_PANTALLA) {
      modoPantalla = n - 1;
      lcdTextoLibre = "";
      if (lcdPresente) lcd.clear();
      avisoOk("Pantalla LCD -> M" + String(n));
      return;
    }
  }
  avisoError("Uso: pantalla <1-4>");
}

// lcd on|off  |  lcd texto <mensaje>  |  lcd limpiar
void cmdLcd(String accion, String lineaOriginal) {
  if (accion == "texto" || accion == "msg") {
    String mensaje = restoDeLinea(lineaOriginal, 2);
    if (mensaje.length() == 0) { avisoError("Uso: lcd texto <mensaje>"); return; }
    lcdTextoLibre = mensaje;
    lcdTextoHasta = 0;                 // permanente hasta 'lcd limpiar'
    if (lcdPresente) lcd.clear();
    avisoOk("LCD muestra: " + mensaje);
    return;
  }
  if (accion == "limpiar" || accion == "clear") {
    lcdTextoLibre = "";
    if (lcdPresente) lcd.clear();
    avisoOk("LCD vuelve a las pantallas normales");
    return;
  }
  if (!lcdPresente) { avisoError("No se detecto ningun LCD en el bus I2C"); return; }
  if (accion == "on")  { lcdEncendido = true;  lcd.backlight(); avisoOk("LCD encendido"); return; }
  if (accion == "off") { lcdEncendido = false; lcd.clear(); lcd.noBacklight(); avisoOk("LCD apagado"); return; }
  avisoError("Uso: lcd <on|off|limpiar>  /  lcd texto <mensaje>");
}

/* ============================================================================
   CATEGORIA: SEMAFORO
   ========================================================================= */

void cmdSem(String valor) {
  if (valor == "auto") {
    prioridadCalle = 0;
    semaforoAutomatico = true;
    tiempoInicioFase = millis();
    avisoOk("Semaforo AUTOMATICO (maquina de estados corriendo)");
    return;
  }
  if (valor == "manual" || valor == "pausa") {
    semaforoAutomatico = false;
    avisoOk("Semaforo MANUAL (usa 'fase ...' para moverlo)");
    return;
  }
  avisoError("Uso: sem <auto|manual>");
}

void cmdFase(String valor) {
  EstadoCruce nuevo;
  if      (valor == "v1") nuevo = VERDE_CALLE1;
  else if (valor == "a1") nuevo = AMARILLO_CALLE1;
  else if (valor == "r1") nuevo = TODO_ROJO_1a2;
  else if (valor == "v2") nuevo = VERDE_CALLE2;
  else if (valor == "a2") nuevo = AMARILLO_CALLE2;
  else if (valor == "r2") nuevo = TODO_ROJO_2a1;
  else if (valor == "sig" || valor == "next") nuevo = (EstadoCruce)((estadoActual + 1) % 6);
  else { avisoError("Uso: fase <v1|a1|r1|v2|a2|r2|sig>"); return; }

  cambiarEstado(nuevo);
  avisoOk("Fase -> " + nombreFaseLarga());
}

/* ============================================================================
   CATEGORIA: TIEMPOS
   ========================================================================= */

void cmdSet(String parametro, String valor) {
  if (!esNumero(valor)) { avisoError("Uso: set <parametro> <numero>. Ver 'ayuda'."); return; }
  long v = valor.toInt();

  if      (parametro == "verdemin")    { verdeMinimo = v;      avisoOk("verdemin = " + String(v) + " ms"); }
  else if (parametro == "verdemax")    { verdeMaximo = v;      avisoOk("verdemax = " + String(v) + " ms"); }
  else if (parametro == "extension")   { extensionPorAuto = v; avisoOk("extension = " + String(v) + " ms/auto"); }
  else if (parametro == "amarillo")    { tiempoAmarillo = v;   avisoOk("amarillo = " + String(v) + " ms"); }
  else if (parametro == "todorojo")    { tiempoTodoRojo = v;   avisoOk("todorojo = " + String(v) + " ms"); }
  else if (parametro == "umbralnoche") { umbralNoche = v;      avisoOk("umbralnoche = " + String(v)); }
  else if (parametro == "umbralco2")   { umbralCo2Alto = v;    avisoOk("umbralco2 = " + String(v)); }
  else if (parametro == "brillodia")   { brilloDia = limitar(v, 0, 255);   avisoOk("brillodia = " + String(brilloDia)); }
  else if (parametro == "brillonoche") { brilloNoche = limitar(v, 0, 255); avisoOk("brillonoche = " + String(brilloNoche)); }
  else if (parametro == "cnybajo")     { cnyActivoEnBajo = (v != 0);       avisoOk("cnybajo = " + String(cnyActivoEnBajo ? "1" : "0")); }
  else avisoError("Parametro desconocido: '" + parametro + "'");
}

/* ============================================================================
   CATEGORIA: PROGRAMACION (comandos diferidos y secuencias)
   ========================================================================= */

bool programar(unsigned long dentroDeMs, String comando) {
  for (int i = 0; i < MAX_PROGRAMADOS; i++) {
    if (!programadoActivo[i]) {
      programadoActivo[i] = true;
      programadoCuando[i] = millis() + dentroDeMs;
      programadoTexto[i] = comando;
      return true;
    }
  }
  return false;
}

// en <ms> <comando>   -> ejecuta ese comando dentro de N milisegundos
void cmdEn(String cuando, String comando) {
  if (!esNumero(cuando) || comando.length() == 0) {
    avisoError("Uso: en <milisegundos> <comando>");
    return;
  }
  if (programar((unsigned long)cuando.toInt(), comando)) {
    avisoOk("Programado para dentro de " + cuando + " ms: " + comando);
  } else {
    avisoError("No hay espacio para mas comandos programados (max " + String(MAX_PROGRAMADOS) + ")");
  }
}

// secuencia cmd1 ; espera 500 ; cmd2 ; ...
// "espera <ms>" no es un comando: solo corre el reloj de la secuencia.
void cmdSecuencia(String texto) {
  if (texto.length() == 0) { avisoError("Uso: secuencia cmd1 ; espera 500 ; cmd2"); return; }

  unsigned long desfase = 0;
  int cuantos = 0;
  int inicio = 0;

  while (inicio <= (int)texto.length()) {
    int corte = texto.indexOf(';', inicio);
    String parte = (corte < 0) ? texto.substring(inicio) : texto.substring(inicio, corte);
    parte.trim();

    if (parte.length() > 0) {
      String copia = parte;
      copia.toLowerCase();
      if (palabra(copia, 0) == "espera") {
        String ms = palabra(copia, 1);
        if (esNumero(ms)) desfase += (unsigned long)ms.toInt();
      } else {
        if (desfase == 0) {
          procesarComando(parte);       // el primero corre de una
        } else if (!programar(desfase, parte)) {
          avisoError("Secuencia truncada: no caben mas comandos programados");
          return;
        }
        cuantos++;
      }
    }

    if (corte < 0) break;
    inicio = corte + 1;
  }
  avisoOk("Secuencia aceptada: " + String(cuantos) + " comandos");
}

void cmdCancelar() {
  int cuantos = 0;
  for (int i = 0; i < MAX_PROGRAMADOS; i++) {
    if (programadoActivo[i]) { programadoActivo[i] = false; cuantos++; }
  }
  avisoOk("Cancelados " + String(cuantos) + " comandos programados");
}

// Se llama desde el loop.
void atenderProgramados() {
  unsigned long ahora = millis();
  for (int i = 0; i < MAX_PROGRAMADOS; i++) {
    if (!programadoActivo[i]) continue;
    if (ahora < programadoCuando[i]) continue;
    programadoActivo[i] = false;
    String comando = programadoTexto[i];
    programadoTexto[i] = "";
    procesarComando(comando);
  }
}

/* ============================================================================
   CATEGORIA: ESCENARIOS
   ========================================================================= */

void cmdEscenario(String cual) {
  if (cual == "trafico1") {
    for (int i = 0; i < 3; i++) modoCny[i] = CNY_ON;
    for (int i = 3; i < 6; i++) modoCny[i] = CNY_OFF;
    avisoOk("Escenario: cola de autos en Calle 1");
    return;
  }
  if (cual == "trafico2") {
    for (int i = 0; i < 3; i++) modoCny[i] = CNY_OFF;
    for (int i = 3; i < 6; i++) modoCny[i] = CNY_ON;
    avisoOk("Escenario: cola de autos en Calle 2");
    return;
  }
  if (cual == "noche") {
    simLdr[0] = 100; simLdr[1] = 100; simNoche = -1;
    avisoOk("Escenario: es de noche");
    return;
  }
  if (cual == "dia") {
    simLdr[0] = 3000; simLdr[1] = 3000; simNoche = -1;
    avisoOk("Escenario: es de dia");
    return;
  }
  if (cual == "contaminacion") {
    simCo2 = umbralCo2Alto + 500;
    for (int i = 0; i < 6; i++) modoCny[i] = CNY_ON;
    avisoOk("Escenario: aire cargado y cola en las dos calles");
    return;
  }
  if (cual == "vacio") {
    for (int i = 0; i < 6; i++) modoCny[i] = CNY_OFF;
    simCo2 = 100;
    avisoOk("Escenario: calle vacia y aire limpio");
    return;
  }
  if (cual == "intermitente") {
    // Modo tipico de madrugada: amarillo parpadeante en las dos calles.
    semaforoAutomatico = false;
    for (int i = 0; i < 6; i++) simLed[i] = 0;
    simLed[1] = 255; simLed[4] = 255;
    parpadeoPeriodo[1] = 700; parpadeoPeriodo[4] = 700;
    avisoOk("Escenario: amarillo intermitente en ambas calles");
    return;
  }
  if (cual == "horapico") {
    flujoPorMinuto[0] = 40; flujoPorMinuto[1] = 35;
    ruidoSensores = true;
    for (int i = 0; i < 6; i++) modoCny[i] = CNY_AUTO;
    avisoOk("Escenario: hora pico, trafico continuo en ambas calles");
    return;
  }
  avisoError("Escenarios: trafico1 trafico2 noche dia contaminacion vacio intermitente horapico");
}

/* ============================================================================
   CATEGORIA: DIAGNOSTICO
   ========================================================================= */

void cmdTest(String que) {
  if (que == "leds") {
    // Barrido: enciende un LED a la vez, 400 ms cada uno, y al final vuelve.
    String orden[6] = { "lr1", "ly1", "lg1", "lr2", "ly2", "lg2" };
    unsigned long t = 0;
    for (int i = 0; i < 6; i++) {
      programar(t, "leds off");
      programar(t + 30, "led " + orden[i] + " on");
      t += 400;
    }
    programar(t, "leds auto");
    avisoOk("Test de LEDs: barrido de 6 LEDs (~2.5 s)");
    return;
  }
  if (que == "sensores") {
    Serial.print(F("[OK] CNY crudos: "));
    for (int i = 0; i < 6; i++) { Serial.print(digitalRead(PIN_CNY[i])); Serial.print(' '); }
    Serial.print(F(" | LDR: ")); Serial.print(analogRead(LDR1));
    Serial.print(' '); Serial.print(analogRead(LDR2));
    Serial.print(F(" | CO2: ")); Serial.print(analogRead(CO2));
    Serial.print(F(" | P1/P2: ")); Serial.print(digitalRead(P1));
    Serial.print(' '); Serial.println(digitalRead(P2));
    return;
  }
  avisoError("Uso: test <leds|sensores>");
}

/* ============================================================================
   RESET GLOBAL
   ========================================================================= */

void resetSimulaciones() {
  for (int i = 0; i < 6; i++) {
    modoCny[i] = CNY_AUTO;
    simLed[i] = -1;
    parpadeoPeriodo[i] = 0;
    parpadeoEncendido[i] = true;
    fadeActivo[i] = false;
    flujoApagarEn[i] = 0;
  }
  simLdr[0] = -1; simLdr[1] = -1;
  simCo2 = -1;
  simNoche = -1;
  brilloMaestro = 255;
  flujoPorMinuto[0] = 0; flujoPorMinuto[1] = 0;
  ruidoSensores = false;
  prioridadCalle = 0;
  prioridadDuracion = 0;
  watchdogLimite = 0;
  seguroActivo = true;
  semaforoAutomatico = true;
  botonesFisicosActivos = true;
  solicitudPeaton1 = false;
  solicitudPeaton2 = false;
  lcdTextoLibre = "";
  if (lcdPresente) lcd.clear();
  for (int i = 0; i < MAX_PROGRAMADOS; i++) programadoActivo[i] = false;
  tiempoInicioFase = millis();
  avisoOk("Todo en AUTO: sensores reales, LEDs siguiendo al semaforo.");
}

/* ============================================================================
   TELEMETRIA
   ========================================================================= */

void cmdMon(String valor) {
  if (valor == "off")   { monitorContinuo = false; avisoOk("Monitor continuo APAGADO"); return; }
  if (valor == "json")  { monitorJson = true;  monitorContinuo = true; avisoOk("Telemetria en JSON cada " + String(periodoMonitor) + " ms"); return; }
  if (valor == "texto") { monitorJson = false; monitorContinuo = true; avisoOk("Telemetria en texto cada " + String(periodoMonitor) + " ms"); return; }
  if (valor == "on")    { monitorContinuo = true;  avisoOk("Monitor continuo cada " + String(periodoMonitor) + " ms"); return; }
  if (esNumero(valor)) {
    long v = valor.toInt();
    if (v < 50) v = 50;
    periodoMonitor = v;
    monitorContinuo = true;
    avisoOk("Monitor continuo cada " + String(v) + " ms");
    return;
  }
  avisoError("Uso: mon <on|off|json|texto|milisegundos>");
}

void imprimirMonitorContinuo() {
  if (!monitorContinuo) return;
  if (millis() - ultimoMonitor < periodoMonitor) return;
  ultimoMonitor = millis();

  if (monitorJson) { emitirJson(); return; }

  Serial.print(F("[MON] fase="));  Serial.print(nombreFaseLarga());
  Serial.print(F(" t="));          Serial.print(tiempoRestanteFase());
  Serial.print(F("s cny="));
  for (int i = 0; i < 6; i++) Serial.print(cnyDetectaEstado[i] ? '1' : '0');
  Serial.print(F(" autos="));      Serial.print(autosCalle1Actual);
  Serial.print('/');               Serial.print(autosCalle2Actual);
  Serial.print(F(" ldr="));        Serial.print(luz1Actual);
  Serial.print(',');               Serial.print(luz2Actual);
  Serial.print(F(" co2="));        Serial.print(co2Actual);
  Serial.print(F(" noche="));      Serial.print(modoNoche ? "SI" : "NO");
  Serial.print(F(" prio="));       Serial.print(prioridadCalle);
  Serial.println();
}

// --- Helpers de JSON: la coma va AL INICIO, asi nunca queda una colgando
//     antes de '}' y no hacen falta argumentos por defecto (que rompen el
//     auto-prototipado del Arduino IDE). El primer campo se imprime a mano.

void jsonCampo(const char* nombre, long valor) {
  Serial.print(",\""); Serial.print(nombre); Serial.print("\":");
  Serial.print(valor);
}

void jsonCampoBool(const char* nombre, bool valor) {
  Serial.print(",\""); Serial.print(nombre); Serial.print("\":");
  Serial.print(valor ? "true" : "false");
}

void jsonCampoTexto(const char* nombre, String valor) {
  valor.replace("\\", "");
  valor.replace("\"", "'");
  Serial.print(",\""); Serial.print(nombre); Serial.print("\":\"");
  Serial.print(valor); Serial.print('"');
}

void jsonArreglo(const char* nombre, int* datos, int cantidad) {
  Serial.print(",\""); Serial.print(nombre); Serial.print("\":[");
  for (int i = 0; i < cantidad; i++) {
    if (i > 0) Serial.print(',');
    Serial.print(datos[i]);
  }
  Serial.print(']');
}

String codigoFase() {
  switch (estadoActual) {
    case VERDE_CALLE1:    return "v1";
    case AMARILLO_CALLE1: return "a1";
    case TODO_ROJO_1a2:   return "r1";
    case VERDE_CALLE2:    return "v2";
    case AMARILLO_CALLE2: return "a2";
    case TODO_ROJO_2a1:   return "r2";
  }
  return "?";
}

long segundosRestantesPrioridad() {
  if (prioridadCalle == 0 || prioridadDuracion == 0) return -1;
  long r = (long)prioridadDuracion - (long)(millis() - prioridadInicio);
  return r > 0 ? r / 1000 : 0;
}

void emitirJson() {
  int cnyValores[6], cnySimulados[6], parpadeos[6];
  for (int i = 0; i < 6; i++) {
    cnyValores[i]   = cnyDetectaEstado[i] ? 1 : 0;
    cnySimulados[i] = (modoCny[i] == CNY_AUTO) ? 0 : 1;
    parpadeos[i]    = (int)parpadeoPeriodo[i];
  }
  int autos[2]  = { autosCalle1Actual, autosCalle2Actual };
  int ldr[2]    = { luz1Actual, luz2Actual };
  int ldrSim[2] = { simLdr[0] >= 0 ? 1 : 0, simLdr[1] >= 0 ? 1 : 0 };
  int ped[2]    = { solicitudPeaton1 ? 1 : 0, solicitudPeaton2 ? 1 : 0 };

  int programados = 0;
  for (int i = 0; i < MAX_PROGRAMADOS; i++) if (programadoActivo[i]) programados++;

  Serial.print(F("{\"tipo\":\"tel\""));
  jsonCampo("ms", (long)millis());

  jsonCampoTexto("fase", codigoFase());
  jsonCampoTexto("faseTexto", nombreFaseLarga());
  jsonCampoBool("semAuto", semaforoAutomatico);
  jsonCampo("restante", tiempoRestanteFase());
  jsonCampo("verdeCalc", (long)duracionVerdeCalculada);

  jsonArreglo("cny", cnyValores, 6);
  jsonArreglo("cnySim", cnySimulados, 6);
  jsonArreglo("autos", autos, 2);
  jsonArreglo("ldr", ldr, 2);
  jsonArreglo("ldrSim", ldrSim, 2);
  jsonCampo("co2", co2Actual);
  jsonCampoBool("co2Sim", simCo2 >= 0);
  jsonCampoBool("co2Alto", co2Actual >= umbralCo2Alto);
  jsonCampoBool("noche", modoNoche);
  jsonCampo("nocheSim", simNoche);
  jsonArreglo("ped", ped, 2);

  jsonArreglo("leds", ledActual, 6);
  jsonArreglo("ledSim", simLed, 6);
  jsonArreglo("parpadeo", parpadeos, 6);
  jsonCampo("brilloMaestro", brilloMaestro);
  jsonCampo("pantalla", modoPantalla + 1);
  jsonCampoBool("lcdPresente", lcdPresente);
  jsonCampoBool("lcdOn", lcdEncendido);
  jsonCampoTexto("lcdTexto", lcdTextoLibre);
  jsonCampoBool("botones", botonesFisicosActivos);

  // --- Lo que la IA necesita saber para no repetir ordenes ni olvidarlas ---
  jsonCampo("prioridad", prioridadCalle);
  jsonCampo("prioridadResta", segundosRestantesPrioridad());
  jsonCampoBool("seguro", seguroActivo);
  jsonCampo("watchdog", (long)(watchdogLimite / 1000));
  jsonCampo("programados", programados);
  jsonArreglo("flujo", flujoPorMinuto, 2);
  jsonCampoBool("ruido", ruidoSensores);

  Serial.print(F(",\"cfg\":{\"verdemin\":"));
  Serial.print(verdeMinimo);
  jsonCampo("verdemax", (long)verdeMaximo);
  jsonCampo("extension", (long)extensionPorAuto);
  jsonCampo("amarillo", (long)tiempoAmarillo);
  jsonCampo("todorojo", (long)tiempoTodoRojo);
  jsonCampo("umbralnoche", umbralNoche);
  jsonCampo("umbralco2", umbralCo2Alto);
  jsonCampo("brillodia", brilloDia);
  jsonCampo("brillonoche", brilloNoche);
  Serial.print('}');

  Serial.println('}');
}

/* ============================================================================
   ESTADO Y AYUDA EN TEXTO
   ========================================================================= */

void mostrarEstado() {
  Serial.println(F("--------------------------------------------"));
  Serial.print(F("Fase actual      : ")); Serial.print(nombreFaseLarga());
  Serial.print(F("  (restan "));          Serial.print(tiempoRestanteFase());
  Serial.println(F(" s)"));
  Serial.print(F("Semaforo         : ")); Serial.println(semaforoAutomatico ? F("AUTOMATICO") : F("MANUAL"));

  if (prioridadCalle != 0) {
    Serial.print(F("PRIORIDAD        : Calle ")); Serial.print(prioridadCalle);
    long r = segundosRestantesPrioridad();
    if (r < 0) Serial.println(F("  (indefinida)"));
    else { Serial.print(F("  (restan ")); Serial.print(r); Serial.println(F(" s)")); }
  }

  Serial.print(F("CNY (1..6)       : "));
  for (int i = 0; i < 6; i++) {
    Serial.print(cnyDetectaEstado[i] ? 'C' : '_');
    Serial.print(modoCny[i] == CNY_AUTO ? ' ' : '*');
  }
  Serial.println(F("   (* = simulado)"));

  Serial.print(F("Autos C1 / C2    : ")); Serial.print(autosCalle1Actual);
  Serial.print(F(" / ")); Serial.println(autosCalle2Actual);

  Serial.print(F("LDR1 / LDR2      : ")); Serial.print(luz1Actual);
  Serial.print(F(" / ")); Serial.println(luz2Actual);

  Serial.print(F("Modo noche       : ")); Serial.println(modoNoche ? F("SI") : F("NO"));
  Serial.print(F("CO2              : ")); Serial.print(co2Actual);
  Serial.println(co2Actual >= umbralCo2Alto ? F("  ALTO") : F("  OK"));

  Serial.print(F("Brillo maestro   : ")); Serial.println(brilloMaestro);
  Serial.print(F("Interlock seguro : ")); Serial.println(seguroActivo ? F("ON") : F("OFF"));
  Serial.print(F("Watchdog         : "));
  if (watchdogLimite == 0) Serial.println(F("desactivado"));
  else { Serial.print(watchdogLimite / 1000); Serial.println(F(" s")); }

  Serial.print(F("LEDs (PWM)       : "));
  for (int i = 0; i < 6; i++) {
    Serial.print(NOM_LED[i]); Serial.print('='); Serial.print(ledActual[i]); Serial.print(' ');
  }
  Serial.println();

  int programados = 0;
  for (int i = 0; i < MAX_PROGRAMADOS; i++) if (programadoActivo[i]) programados++;
  Serial.print(F("Programados      : ")); Serial.println(programados);

  Serial.print(F("Flujo C1 / C2    : ")); Serial.print(flujoPorMinuto[0]);
  Serial.print(F(" / ")); Serial.print(flujoPorMinuto[1]); Serial.println(F(" autos/min"));

  Serial.print(F("Tiempos (ms)     : verdemin=")); Serial.print(verdeMinimo);
  Serial.print(F(" verdemax="));  Serial.print(verdeMaximo);
  Serial.print(F(" extension=")); Serial.print(extensionPorAuto);
  Serial.print(F(" amarillo="));  Serial.print(tiempoAmarillo);
  Serial.print(F(" todorojo="));  Serial.println(tiempoTodoRojo);
  Serial.println(F("--------------------------------------------"));
}

void mostrarAyuda() {
  Serial.println(F("=============== COMANDOS ==============="));
  Serial.println(F("-- sistema --"));
  Serial.println(F("ayuda | estado | json | reset"));
  Serial.println(F("guardar <0-2> | restaurar <0-2>"));
  Serial.println(F("watchdog <seg|off> | seguro <on|off>"));
  Serial.println(F("mon <on|off|json|texto|ms>"));
  Serial.println(F("-- semaforo --"));
  Serial.println(F("sem <auto|manual> | fase <v1|a1|r1|v2|a2|r2|sig>"));
  Serial.println(F("-- emergencia --"));
  Serial.println(F("prio <c1|c2> <seg|inf> | prio off | panico | apagar"));
  Serial.println(F("-- luces --"));
  Serial.println(F("led <lr1..lg2> <on|off|0-255|auto> | leds <auto|on|off>"));
  Serial.println(F("brillo <0-255|N%> | parpadeo <led|sem1|sem2|all> <ms|off>"));
  Serial.println(F("fade <led> <0-255> <ms>"));
  Serial.println(F("-- sensores --"));
  Serial.println(F("cny <1-6|c1|c2|all> <on|off|auto>"));
  Serial.println(F("flujo <c1|c2> <autos/min> | flujo off | ruido <on|off>"));
  Serial.println(F("-- ambiente --"));
  Serial.println(F("ldr <1|2|all> <0-4095|auto> | co2 <valor|alto|bajo|auto>"));
  Serial.println(F("noche <on|off|auto>"));
  Serial.println(F("-- peatones --"));
  Serial.println(F("p1 | p2 | peaton <c1|c2|limpiar> | botones <on|off>"));
  Serial.println(F("-- pantalla --"));
  Serial.println(F("combo | pantalla <1-4> | lcd <on|off|limpiar>"));
  Serial.println(F("lcd texto <mensaje>"));
  Serial.println(F("-- tiempos --"));
  Serial.println(F("set <verdemin|verdemax|extension|amarillo|todorojo> <ms>"));
  Serial.println(F("set <umbralnoche|umbralco2|brillodia|brillonoche|cnybajo> <v>"));
  Serial.println(F("-- escenarios --"));
  Serial.println(F("escenario <trafico1|trafico2|noche|dia|contaminacion|"));
  Serial.println(F("           vacio|intermitente|horapico>"));
  Serial.println(F("-- programacion --"));
  Serial.println(F("en <ms> <comando> | secuencia a ; espera 500 ; b | cancelar"));
  Serial.println(F("-- diagnostico --"));
  Serial.println(F("test <leds|sensores>"));
  Serial.println(F("========================================"));
}
