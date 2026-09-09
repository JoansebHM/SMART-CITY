/* ============================================================================
   CIUDAD AUTOADAPTABLE (SMART CITY) - MAQUETA CON ESP32-S3
   VERSION CON CONSOLA SERIAL: todos los sensores y actuadores se pueden
   simular / forzar escribiendo comandos en el Monitor Serie.
   ============================================================================

   IDEA GENERAL
   ------------
   La maqueta es un cruce de dos calles perpendiculares:

     - CALLE 1 (horizontal): Semaforo 1 (LR1/LY1/LG1), sensores de vehiculos
       CNY1, CNY2, CNY3, sensor de luz LDR1 y boton peatonal P1.
     - CALLE 2 (vertical):   Semaforo 2 (LR2/LY2/LG2), sensores CNY4, CNY5,
       CNY6, sensor de luz LDR2 y boton peatonal P2.

   Ambas calles comparten la interseccion, asi que nunca estan las dos en
   verde. Una maquina de estados alterna el verde, pasando por amarillo y
   por una fase de "todo rojo" de seguridad.

   COMPORTAMIENTO AUTOADAPTABLE
     1. TRAFICO   : mas sensores CNY activos en una calle => verde mas largo.
     2. PEATONES  : P1/P2 acortan el verde de su calle al minimo.
     3. LUZ (LDR) : de noche los LEDs bajan su brillo (PWM).
     4. CO2       : si el aire esta cargado, se reduce el verde maximo; el LED
                    RGB de la placa lo indica (verde = OK, rojo = critico) y
                    si pasa el umbral se entra en EMERGENCIA: el ciclo se
                    congela con LG2+LR1 para evacuar por la Calle 2.
     5. LCD I2C   : 4 pantallas de informacion; se rotan con P1+P2 juntos.
     6. HORA      : con el comando "hora <0-23>" se le dice al sistema que
                    hora es. Si la hora cae en 23h-4h Y los dos LDR ven poca
                    luz, se entra en NOCHE PROFUNDA: LY1 y LR2 parpadean.

   NOVEDAD: CONSOLA SERIAL
   -----------------------
   Cada entrada (CNY, LDR, CO2, botones) puede estar en modo AUTO (lee el
   hardware real) o en modo SIMULADO (usa el valor que le escribas por
   Serial). Cada salida (los 6 LEDs) puede seguir al semaforo o quedar
   forzada a mano. Tambien se pueden cambiar en caliente los tiempos y
   umbrales, y pausar la maquina de estados para mover las fases a mano.

   Abre el Monitor Serie a 115200 baudios, con final de linea "Nueva linea"
   y escribe:  ayuda

   NOTAS DE HARDWARE
     - LDR1(13), LDR2(12) y CO2(14) son analogicos (ADC2). No se usa WiFi,
       asi que no hay conflicto.
     - Los CNY detectan objetos blancos. Por defecto se asume que el sensor
       entrega LOW cuando SI detecta; cambia cnyActivoEnBajo si es al reves.
     - Libreria necesaria: "LiquidCrystal I2C" (Frank de Brabander).
       Direccion I2C tipica: 0x27 o 0x3F.
   ========================================================================= */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================================
// 1. PINES
// ============================================================================
#define LDR1 13
#define LDR2 12
#define CO2  14

#define P1   1
#define P2   2

#define CNY1 42
#define CNY2 41
#define CNY3 40
#define CNY4 39
#define CNY5 38
#define CNY6 37

#define LR1  5
#define LY1  4
#define LG1  6

#define LR2  7
#define LY2  15
#define LG2  16

// Arreglos para poder recorrer los perifericos por indice desde la consola
const int PIN_CNY[6] = { CNY1, CNY2, CNY3, CNY4, CNY5, CNY6 };

// Orden de los LEDs: 0=LR1 1=LY1 2=LG1 3=LR2 4=LY2 5=LG2
const int PIN_LED[6]  = { LR1, LY1, LG1, LR2, LY2, LG2 };
const char* NOM_LED[6] = { "lr1", "ly1", "lg1", "lr2", "ly2", "lg2" };

// ============================================================================
// 2. LCD I2C 16x4
// ============================================================================
#define DIRECCION_LCD 0x27      // prueba 0x3F si el tuyo no responde
LiquidCrystal_I2C lcd(DIRECCION_LCD, 16, 4);
bool lcdEncendido = true;
bool lcdPresente  = false;      // se detecta en setup(); si es false, se ignora el LCD

// ============================================================================
// 3. PARAMETROS DEL SISTEMA (ahora son variables: se cambian por Serial)
// ============================================================================
bool cnyActivoEnBajo = true;

unsigned long verdeMinimo      = 5000;
unsigned long verdeMaximo      = 15000;
unsigned long extensionPorAuto = 2000;
unsigned long tiempoAmarillo   = 3000;
unsigned long tiempoTodoRojo   = 1000;

int umbralNoche   = 800;
int brilloDia     = 255;
int brilloNoche   = 60;
int umbralCo2Alto = 2500;

const unsigned long DEBOUNCE_MS   = 200;
const unsigned long VENTANA_COMBO = 150;
const int NUM_MODOS_PANTALLA      = 4;

// ============================================================================
// 4. MAQUINA DE ESTADOS
// ============================================================================
enum EstadoCruce {
  VERDE_CALLE1,
  AMARILLO_CALLE1,
  TODO_ROJO_1a2,
  VERDE_CALLE2,
  AMARILLO_CALLE2,
  TODO_ROJO_2a1
};

EstadoCruce estadoActual = VERDE_CALLE1;
unsigned long tiempoInicioFase = 0;
unsigned long duracionVerdeCalculada = 5000;

// Si es false, la maquina de estados queda congelada y las fases solo
// cambian con el comando "fase ..." desde la consola.
bool semaforoAutomatico = true;

// ============================================================================
// 5. MODOS DE SIMULACION
// ============================================================================
// Para los CNY: 0 = AUTO (lee el pin), 1 = forzado DETECTA, 2 = forzado LIBRE
enum ModoCny { CNY_AUTO = 0, CNY_ON = 1, CNY_OFF = 2 };
ModoCny modoCny[6] = { CNY_AUTO, CNY_AUTO, CNY_AUTO, CNY_AUTO, CNY_AUTO, CNY_AUTO };

// Para analogicos: -1 = AUTO (lee el pin), 0..4095 = valor forzado
int simLdr[2] = { -1, -1 };
int simCo2    = -1;

// Modo noche: -1 = AUTO (lo decide el LDR), 0 = forzado dia, 1 = forzado noche
int simNoche = -1;

// LEDs: -1 = sigue al semaforo, 0..255 = PWM forzado a mano
int simLed[6] = { -1, -1, -1, -1, -1, -1 };

// Botones fisicos: true = se leen, false = se ignoran (solo consola)
bool botonesFisicosActivos = true;

// ============================================================================
// 6. ESTADO DE ENTRADAS
// ============================================================================
bool solicitudPeaton1 = false;
bool solicitudPeaton2 = false;

bool p1PendienteConfirmar = false;
bool p2PendienteConfirmar = false;
unsigned long tiempoP1Bajo = 0;
unsigned long tiempoP2Bajo = 0;
int estadoAnteriorP1 = HIGH;
int estadoAnteriorP2 = HIGH;
unsigned long ultimoDebounceP1 = 0;
unsigned long ultimoDebounceP2 = 0;

bool modoNoche = false;
int co2Actual = 0;

int luz1Actual = 0;
int luz2Actual = 0;
bool cnyDetectaEstado[6] = { false, false, false, false, false, false };
int autosCalle1Actual = 0;
int autosCalle2Actual = 0;

int modoPantalla = 0;

// ============================================================================
// 7. CONSOLA SERIAL
// ============================================================================
String bufferSerial = "";
bool monitorContinuo = false;             // imprime telemetria periodica
bool monitorJson = false;                 // true = telemetria en JSON (para el dashboard)
unsigned long periodoMonitor = 1000;      // cada cuanto la imprime (ms)
unsigned long ultimoMonitor = 0;

// Ultimo valor PWM realmente aplicado a cada LED (para reportarlo al dashboard)
int ledActual[6] = { 0, 0, 0, 0, 0, 0 };

// ============================================================================
// 8. EFECTOS DE LUZ, PRIORIDAD, PROGRAMADOR Y SEGURIDAD
//    (los comandos que manejan todo esto viven en Comandos.ino)
// ============================================================================

// --- Brillo maestro: escala 0-255 que multiplica el brillo de TODOS los LEDs.
//     255 = sin atenuar. Es lo que se toca al decir "bajale la intensidad".
int brilloMaestro = 255;

// --- Parpadeo por LED: 0 = fijo, >0 = periodo en ms de encendido/apagado ---
unsigned long parpadeoPeriodo[6] = { 0, 0, 0, 0, 0, 0 };
bool parpadeoEncendido[6] = { true, true, true, true, true, true };
unsigned long parpadeoUltimo[6] = { 0, 0, 0, 0, 0, 0 };

// --- Fade (rampa suave de brillo) por LED ---
bool fadeActivo[6] = { false, false, false, false, false, false };
int fadeDesde[6] = { 0, 0, 0, 0, 0, 0 };
int fadeHasta[6] = { 0, 0, 0, 0, 0, 0 };
unsigned long fadeInicio[6] = { 0, 0, 0, 0, 0, 0 };
unsigned long fadeDuracion[6] = { 0, 0, 0, 0, 0, 0 };

// --- Prioridad / emergencia -------------------------------------------------
// calle 0 = ninguna, 1 = Calle 1, 2 = Calle 2.
// Mientras hay prioridad, esa calle se sostiene en verde y la otra en rojo.
int prioridadCalle = 0;
unsigned long prioridadInicio = 0;
unsigned long prioridadDuracion = 0;   // 0 = indefinida (solo la corta el watchdog)

// --- Watchdog de seguridad --------------------------------------------------
// Si es > 0 y se vence sin que nadie lo renueve, el sistema se auto-restaura.
// Evita que la maqueta quede trabada si se pierde la conexion o se olvida
// mandar el comando de "ya paso la ambulancia".
unsigned long watchdogLimite = 0;      // 0 = desactivado
unsigned long watchdogUltimo = 0;

// --- Interlock de seguridad -------------------------------------------------
// Con seguro activo, el firmware NUNCA deja las dos calles en verde a la vez,
// pase lo que pase por consola. Es la garantia que no depende de quien manda
// los comandos (persona o IA).
bool seguroActivo = true;

// --- Programador: comandos diferidos ("en 5000 reset") ----------------------
#define MAX_PROGRAMADOS 8
String programadoTexto[MAX_PROGRAMADOS];
unsigned long programadoCuando[MAX_PROGRAMADOS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
bool programadoActivo[MAX_PROGRAMADOS] = { false, false, false, false, false, false, false, false };

// --- Flujo de trafico simulado (carros que llegan y se van solos) -----------
int flujoPorMinuto[2] = { 0, 0 };            // 0 = apagado
unsigned long flujoUltimo[2] = { 0, 0 };
bool ruidoSensores = false;                  // pequenas fluctuaciones realistas

// --- Texto libre en el LCD --------------------------------------------------
String lcdTextoLibre = "";
unsigned long lcdTextoHasta = 0;             // 0 = permanente mientras no se borre

// --- Snapshots (guardar / restaurar el estado completo) ---------------------
#define MAX_SLOTS 3
struct Instantanea {
  bool usada;
  ModoCny modoCny[6];
  int simLdr[2];
  int simCo2;
  int simNoche;
  int simLed[6];
  bool semaforoAutomatico;
  bool botonesFisicosActivos;
  int brilloMaestro;
  unsigned long parpadeoPeriodo[6];
  unsigned long verdeMinimo, verdeMaximo, extensionPorAuto;
  unsigned long tiempoAmarillo, tiempoTodoRojo;
  int umbralNoche, brilloDia, brilloNoche, umbralCo2Alto;
  int modoPantalla;
};
Instantanea instantaneas[MAX_SLOTS];

// ============================================================================
// 9. HORA INDICADA POR SERIAL Y NOCHE PROFUNDA
// ============================================================================
// La placa no tiene reloj: la hora se le dice desde afuera con el comando
// "hora <0-23>" (tambien acepta el formato "HORA:23"). Si la hora cae en la
// franja 23h-4h Y los dos LDR ven poca luz, los semaforos pasan a modo
// intermitente de madrugada: LY1 y LR2 parpadean juntos y el resto se apaga.
int  horaActualIndicada = -1;            // -1 = todavia nadie dijo la hora
bool horaNocheProfundaIndicada = false;  // true si esa hora esta en 23h-4h

bool modoNocheProfundaActivo = false;
bool estadoParpadeoNocheProfunda = false;
unsigned long tUltimoParpadeoNocheProfunda = 0;
const unsigned long INTERVALO_PARPADEO_NOCHE = 400;

bool advertenciaHoraMostrada = false;    // no repetir el aviso en cada vuelta

// ============================================================================
// 10. EMERGENCIA POR CO2 Y LED RGB DE LA PLACA
// ============================================================================
// Mientras el CO2 esta por encima de umbralCo2Alto el ciclo normal se congela:
// se fuerza LG2 + LR1 (evacuacion por la Calle 2) y el LCD muestra el aviso.
// Al normalizarse, todo vuelve exactamente a como estaba.
bool emergenciaCO2Activa = false;

// Brillo maximo por canal del LED RGB integrado (0-255): bajo, para no encandilar.
const uint8_t NIVEL_LED_RGB = 40;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);

  // En el ESP32-S3 el puerto USB nativo se "re-enumera" despues del reset:
  // el Monitor Serie tarda ~1 s en reconectarse. Si imprimimos antes de que
  // el host este listo, el mensaje de bienvenida se pierde y parece que la
  // placa no responde. Esperamos hasta 2 s a que el host abra el puerto.
  unsigned long tEspera = millis();
  while (!Serial && (millis() - tEspera) < 2000) delay(10);
  delay(400);

  pinMode(LDR1, INPUT);
  pinMode(LDR2, INPUT);
  pinMode(CO2, INPUT);

  pinMode(P1, INPUT_PULLUP);
  pinMode(P2, INPUT_PULLUP);

  for (int i = 0; i < 6; i++) pinMode(PIN_CNY[i], INPUT);
  for (int i = 0; i < 6; i++) pinMode(PIN_LED[i], OUTPUT);

  // El LCD es OPCIONAL para la consola: si no responde en el bus I2C lo
  // damos por ausente y seguimos. Asi un LCD mal cableado nunca deja la
  // consola serial muda.
  Wire.begin();
  Wire.beginTransmission(DIRECCION_LCD);
  lcdPresente = (Wire.endTransmission() == 0);

  if (lcdPresente) {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Ciudad Autoadapt."));
    lcd.setCursor(0, 1);
    lcd.print(F("Consola Serial"));
    delay(1200);
    lcd.clear();
  }

#ifdef RGB_BUILTIN
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);   // LED RGB de la placa apagado al arrancar
#endif

  tiempoInicioFase = millis();
  duracionVerdeCalculada = verdeMinimo;
  aplicarSemaforos();

  Serial.println();
  Serial.println(F("=== CIUDAD AUTOADAPTABLE - CONSOLA LISTA ==="));
  Serial.println(F("Escribe 'ayuda' para ver todos los comandos."));
  Serial.println();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  atenderSerial();          // 1. procesa comandos escritos por el usuario
  atenderProgramados();     // 2. comandos diferidos ("en 5000 reset")
  atenderFlujoTrafico();    // 3. carros que llegan y se van solos
  leerSensoresDetalle();    // 4. lee (o simula) CNY y LDR
  leerBotones();            // 5. botones fisicos P1 / P2 / combo
  actualizarModoNoche();
  leerCO2();
  actualizarLedRGB();       // 6. LED de la placa: verde = aire OK, rojo = critico
  atenderWatchdog();        // 7. red de seguridad: deshace anulaciones vencidas
  atenderPrioridad();       // 8. emergencia: sostiene el verde de una calle
  actualizarEmergenciaCO2();// 9. CO2 critico: congela el ciclo y evacua por C2

  // La emergencia por CO2 manda sobre todo lo demas: mientras dure, ni la
  // maquina de estados ni el intermitente de madrugada tocan los semaforos.
  if (!emergenciaCO2Activa) {
    actualizarMaquinaEstados();
    actualizarNocheProfunda();
  }

  aplicarSemaforos();       // se aplica cada vuelta: el brillo reacciona al instante
  actualizarLCD();
  imprimirMonitorContinuo();

  delay(20);
}

// ============================================================================
// LECTURA (O SIMULACION) DE SENSORES
// ============================================================================
void leerSensoresDetalle() {
  luz1Actual = (simLdr[0] >= 0) ? simLdr[0] : analogRead(LDR1);
  luz2Actual = (simLdr[1] >= 0) ? simLdr[1] : analogRead(LDR2);

  for (int i = 0; i < 6; i++) {
    switch (modoCny[i]) {
      case CNY_ON:  cnyDetectaEstado[i] = true;  break;
      case CNY_OFF: cnyDetectaEstado[i] = false; break;
      default:      cnyDetectaEstado[i] = cnyLeePin(PIN_CNY[i]); break;
    }
  }

  autosCalle1Actual = contarAutos(0);
  autosCalle2Actual = contarAutos(3);
}

int contarAutos(int desde) {
  int total = 0;
  for (int i = desde; i < desde + 3; i++) if (cnyDetectaEstado[i]) total++;
  return total;
}

bool cnyLeePin(int pin) {
  int lectura = digitalRead(pin);
  return cnyActivoEnBajo ? (lectura == LOW) : (lectura == HIGH);
}

void leerCO2() {
  co2Actual = (simCo2 >= 0) ? simCo2 : analogRead(CO2);
}

void actualizarModoNoche() {
  if (simNoche == 0)      { modoNoche = false; return; }
  if (simNoche == 1)      { modoNoche = true;  return; }
  int promedioLuz = (luz1Actual + luz2Actual) / 2;
  modoNoche = (promedioLuz < umbralNoche);
}

// ============================================================================
// BOTONES FISICOS (individual = peaton, P1+P2 juntos = cambiar pantalla)
// ============================================================================
void leerBotones() {
  if (!botonesFisicosActivos) return;

  int lecturaP1 = digitalRead(P1);
  int lecturaP2 = digitalRead(P2);

  if (lecturaP1 == LOW && estadoAnteriorP1 == HIGH &&
      (millis() - ultimoDebounceP1) > DEBOUNCE_MS) {
    p1PendienteConfirmar = true;
    tiempoP1Bajo = millis();
    ultimoDebounceP1 = millis();
  }
  estadoAnteriorP1 = lecturaP1;

  if (lecturaP2 == LOW && estadoAnteriorP2 == HIGH &&
      (millis() - ultimoDebounceP2) > DEBOUNCE_MS) {
    p2PendienteConfirmar = true;
    tiempoP2Bajo = millis();
    ultimoDebounceP2 = millis();
  }
  estadoAnteriorP2 = lecturaP2;

  // Los dos bajaron casi al tiempo => combo => cambiar de pantalla
  if (p1PendienteConfirmar && p2PendienteConfirmar) {
    unsigned long diferencia = (tiempoP1Bajo > tiempoP2Bajo)
                                 ? (tiempoP1Bajo - tiempoP2Bajo)
                                 : (tiempoP2Bajo - tiempoP1Bajo);
    if (diferencia <= VENTANA_COMBO) {
      cambiarModoPantalla();
      p1PendienteConfirmar = false;
      p2PendienteConfirmar = false;
    }
  }

  // Paso la ventana y solo bajo uno => peticion peatonal individual
  if (p1PendienteConfirmar && (millis() - tiempoP1Bajo) > VENTANA_COMBO) {
    if (!p2PendienteConfirmar) pedirPeaton(1);
    p1PendienteConfirmar = false;
  }
  if (p2PendienteConfirmar && (millis() - tiempoP2Bajo) > VENTANA_COMBO) {
    if (!p1PendienteConfirmar) pedirPeaton(2);
    p2PendienteConfirmar = false;
  }
}

void pedirPeaton(int calle) {
  if (calle == 1) {
    solicitudPeaton1 = true;
    Serial.println(F("[EVENTO] Peaton solicito cruce en Calle 1"));
  } else {
    solicitudPeaton2 = true;
    Serial.println(F("[EVENTO] Peaton solicito cruce en Calle 2"));
  }
}

void cambiarModoPantalla() {
  modoPantalla = (modoPantalla + 1) % NUM_MODOS_PANTALLA;
  if (lcdPresente) lcd.clear();
  Serial.print(F("[EVENTO] Pantalla LCD -> M"));
  Serial.println(modoPantalla + 1);
}

// ============================================================================
// MAQUINA DE ESTADOS DEL CRUCE
// ============================================================================
void actualizarMaquinaEstados() {
  if (!semaforoAutomatico) return;   // modo manual: solo avanza por comando

  unsigned long transcurrido = millis() - tiempoInicioFase;

  switch (estadoActual) {

    case VERDE_CALLE1: {
      unsigned long limiteVerde = duracionVerdeCalculada;
      if (solicitudPeaton1 && limiteVerde > verdeMinimo) limiteVerde = verdeMinimo;
      if (transcurrido >= limiteVerde) cambiarEstado(AMARILLO_CALLE1);
      break;
    }

    case AMARILLO_CALLE1:
      if (transcurrido >= tiempoAmarillo) cambiarEstado(TODO_ROJO_1a2);
      break;

    case TODO_ROJO_1a2:
      if (transcurrido >= tiempoTodoRojo) {
        solicitudPeaton1 = false;
        duracionVerdeCalculada = calcularDuracionVerde(autosCalle2Actual);
        cambiarEstado(VERDE_CALLE2);
      }
      break;

    case VERDE_CALLE2: {
      unsigned long limiteVerde = duracionVerdeCalculada;
      if (solicitudPeaton2 && limiteVerde > verdeMinimo) limiteVerde = verdeMinimo;
      if (transcurrido >= limiteVerde) cambiarEstado(AMARILLO_CALLE2);
      break;
    }

    case AMARILLO_CALLE2:
      if (transcurrido >= tiempoAmarillo) cambiarEstado(TODO_ROJO_2a1);
      break;

    case TODO_ROJO_2a1:
      if (transcurrido >= tiempoTodoRojo) {
        solicitudPeaton2 = false;
        duracionVerdeCalculada = calcularDuracionVerde(autosCalle1Actual);
        cambiarEstado(VERDE_CALLE1);
      }
      break;
  }
}

unsigned long calcularDuracionVerde(int autosDetectados) {
  unsigned long duracion = verdeMinimo + (autosDetectados * extensionPorAuto);

  unsigned long maximoPermitido = verdeMaximo;
  if (co2Actual >= umbralCo2Alto) {
    maximoPermitido = verdeMinimo + (extensionPorAuto * 2);
  }

  if (duracion > maximoPermitido) duracion = maximoPermitido;
  if (duracion < verdeMinimo)     duracion = verdeMinimo;
  return duracion;
}

void cambiarEstado(EstadoCruce nuevoEstado) {
  estadoActual = nuevoEstado;
  tiempoInicioFase = millis();
  aplicarSemaforos();
}

// ============================================================================
// SALIDAS: LEDS DE LOS SEMAFOROS
// ============================================================================
// El valor final de cada LED se arma por capas, en este orden:
//   1. Lo que pide la maquina de estados (o el override manual del LED).
//   2. El fade, si hay una rampa en curso hacia otro brillo.
//   3. El brillo de dia/noche y el brillo maestro.
//   4. El parpadeo, que apaga el LED durante media fase.
//   5. El interlock de seguridad, que corta verdes simultaneos.
// Los dos modos forzados (emergencia por CO2 y noche profunda) se saltan las
// capas 1-2 y 4: mandan ellos, por encima de cualquier override de consola.
void aplicarSemaforos() {
  bool encendido[6] = { false, false, false, false, false, false };
  // indices: 0=LR1 1=LY1 2=LG1 3=LR2 4=LY2 5=LG2

  bool forzado = emergenciaCO2Activa || modoNocheProfundaActivo;

  if (emergenciaCO2Activa) {
    // Evacuacion por la Calle 2: su verde y el rojo de la Calle 1.
    encendido[5] = true;   // LG2
    encendido[0] = true;   // LR1
  } else if (modoNocheProfundaActivo) {
    // Madrugada: solo LY1 y LR2, parpadeando juntos.
    encendido[1] = estadoParpadeoNocheProfunda;   // LY1
    encendido[3] = estadoParpadeoNocheProfunda;   // LR2
  } else {
    switch (estadoActual) {
      case VERDE_CALLE1:    encendido[2] = true; encendido[3] = true; break;
      case AMARILLO_CALLE1: encendido[1] = true; encendido[3] = true; break;
      case TODO_ROJO_1a2:   encendido[0] = true; encendido[3] = true; break;
      case VERDE_CALLE2:    encendido[0] = true; encendido[5] = true; break;
      case AMARILLO_CALLE2: encendido[0] = true; encendido[4] = true; break;
      case TODO_ROJO_2a1:   encendido[0] = true; encendido[3] = true; break;
    }
  }

  int brilloBase = modoNoche ? brilloNoche : brilloDia;
  unsigned long ahora = millis();

  for (int i = 0; i < 6; i++) {
    int valor;

    if (forzado) {
      // Emergencia / madrugada: mandan ellas, sin fade ni override manual.
      valor = encendido[i] ? brilloBase : 0;
    } else if (fadeActivo[i]) {
      // --- Capa 2: rampa suave entre dos brillos ---
      unsigned long transcurrido = ahora - fadeInicio[i];
      if (transcurrido >= fadeDuracion[i]) {
        valor = fadeHasta[i];
        fadeActivo[i] = false;
        simLed[i] = fadeHasta[i];     // al terminar, el LED queda fijo ahi
      } else {
        long recorrido = (long)(fadeHasta[i] - fadeDesde[i]);
        valor = fadeDesde[i] + (int)((recorrido * (long)transcurrido) / (long)fadeDuracion[i]);
      }
    } else if (simLed[i] >= 0) {
      valor = simLed[i];                                  // LED forzado a mano
    } else {
      valor = encendido[i] ? brilloBase : 0;              // lo que pide el semaforo
    }

    // --- Capa 3: brillo maestro (atenuacion global) ---
    valor = (int)(((long)valor * (long)brilloMaestro) / 255L);

    // --- Capa 4: parpadeo (el de consola; los modos forzados traen el suyo) ---
    if (!forzado && parpadeoPeriodo[i] > 0) {
      if (ahora - parpadeoUltimo[i] >= parpadeoPeriodo[i]) {
        parpadeoUltimo[i] = ahora;
        parpadeoEncendido[i] = !parpadeoEncendido[i];
      }
      if (!parpadeoEncendido[i]) valor = 0;
    }

    if (valor < 0) valor = 0;
    if (valor > 255) valor = 255;
    ledActual[i] = valor;
  }

  // --- Capa 5: interlock. Los dos verdes no pueden estar encendidos a la vez.
  //     Ante el conflicto gana la calle con prioridad; si no hay prioridad,
  //     gana la que la maquina de estados tenga en verde.
  if (seguroActivo && ledActual[2] > 0 && ledActual[5] > 0) {
    bool ganaCalle1 = (prioridadCalle == 1) ||
                      (prioridadCalle == 0 && estadoActual == VERDE_CALLE1);
    if (ganaCalle1) ledActual[5] = 0; else ledActual[2] = 0;
  }

  for (int i = 0; i < 6; i++) analogWrite(PIN_LED[i], ledActual[i]);
}

// ============================================================================
// LED RGB INTEGRADO DE LA PLACA: semaforo de calidad del aire
//   Verde -> CO2 por debajo del umbral
//   Rojo  -> CO2 en el umbral o por encima (mismo punto en que dispara la
//            emergencia), asi se ve el problema sin mirar el LCD.
// ============================================================================
void actualizarLedRGB() {
#ifdef RGB_BUILTIN
  if (co2Actual >= umbralCo2Alto) {
    rgbLedWrite(RGB_BUILTIN, NIVEL_LED_RGB, 0, 0);              // rojo
  } else {
    rgbLedWrite(RGB_BUILTIN, 0, NIVEL_LED_RGB, 0);              // verde
  }
#endif
}

// ============================================================================
// EMERGENCIA POR CO2
// ----------------------------------------------------------------------------
// Si el CO2 pasa el umbral se congela el ciclo normal, se fuerza LG2+LR1 para
// evacuar por la Calle 2 y el LCD muestra el aviso de peligro. Cuando el aire
// se normaliza, el sistema vuelve exactamente al comportamiento anterior.
// ============================================================================
void actualizarEmergenciaCO2() {
  bool nivelPeligroso = (co2Actual >= umbralCo2Alto);

  if (nivelPeligroso && !emergenciaCO2Activa) {
    emergenciaCO2Activa = true;
    modoNocheProfundaActivo = false;     // la emergencia manda sobre la madrugada
    if (lcdPresente) lcd.clear();
    avisoEvento(F("EMERGENCIA CO2: nivel critico. Evacuacion por la Calle 2."));
  } else if (!nivelPeligroso && emergenciaCO2Activa) {
    emergenciaCO2Activa = false;
    // La fase arranca de cero: no debe "recuperar" el tiempo de la emergencia.
    tiempoInicioFase = millis();
    if (lcdPresente) lcd.clear();
    avisoEvento(F("CO2 normalizado. Reanudando operacion normal."));
  }

  // Mientras dure, la fase logica es la de evacuacion (LR1 + LG2). Asi el
  // interlock, la telemetria y el LCD ven un estado coherente.
  if (emergenciaCO2Activa && estadoActual != VERDE_CALLE2) {
    estadoActual = VERDE_CALLE2;
    tiempoInicioFase = millis();
  }
}

// ============================================================================
// NOCHE PROFUNDA (INTERMITENTE DE MADRUGADA)
// ----------------------------------------------------------------------------
// Se activa solo si se cumplen las DOS condiciones: la hora indicada por
// consola cae en 23h-4h Y los dos LDR leen por debajo del umbral de noche.
// Mientras esta activo, LY1 y LR2 parpadean juntos y el resto queda apagado.
// Si dijeron la hora pero los sensores no confirman oscuridad, se avisa una
// sola vez por Serial.
// ============================================================================
void actualizarNocheProfunda() {
  bool ambosLdrBajos = (luz1Actual < umbralNoche) && (luz2Actual < umbralNoche);

  if (horaNocheProfundaIndicada && ambosLdrBajos) {
    if (!modoNocheProfundaActivo) {
      modoNocheProfundaActivo = true;
      estadoParpadeoNocheProfunda = true;
      tUltimoParpadeoNocheProfunda = millis();
      avisoEvento(F("NOCHE PROFUNDA: intermitente LY1 + LR2."));
    }
    advertenciaHoraMostrada = false;

    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoNocheProfunda >= INTERVALO_PARPADEO_NOCHE) {
      estadoParpadeoNocheProfunda = !estadoParpadeoNocheProfunda;
      tUltimoParpadeoNocheProfunda = ahora;
    }
    return;
  }

  // --- Condicion no cumplida: se libera el forzado si estaba puesto ---
  if (modoNocheProfundaActivo) {
    modoNocheProfundaActivo = false;
    tiempoInicioFase = millis();
    avisoEvento(F("Fin de la noche profunda. Ciclo normal reanudado."));
  }

  // --- Inconsistencia: dijeron la hora pero no hay oscuridad en las dos vias ---
  if (horaNocheProfundaIndicada && !ambosLdrBajos) {
    if (!advertenciaHoraMostrada) {
      avisoEvento(F("ADVERTENCIA: hora en 23h-4h pero los LDR no ven poca luz en ambas vias."));
      advertenciaHoraMostrada = true;
    }
  } else {
    advertenciaHoraMostrada = false;
  }
}

// ============================================================================
// PANTALLA LCD I2C (16x4) - 4 MODOS
// ============================================================================
void actualizarLCD() {
  if (!lcdPresente || !lcdEncendido) return;

  static unsigned long ultimaActualizacion = 0;
  if (millis() - ultimaActualizacion < 400) return;
  ultimaActualizacion = millis();

  // La emergencia por CO2 tapa cualquier otra pantalla mientras dure.
  if (emergenciaCO2Activa) { dibujarPantallaEmergenciaCO2(); return; }

  // Un mensaje libre (por ejemplo "AMBULANCIA") tapa las pantallas normales
  // mientras este vigente. Sirve para que la maqueta anuncie la emergencia.
  if (lcdTextoLibre.length() > 0) {
    if (lcdTextoHasta > 0 && millis() > lcdTextoHasta) {
      lcdTextoLibre = "";
      lcd.clear();
    } else {
      dibujarTextoLibre();
      return;
    }
  }

  switch (modoPantalla) {
    case 0: dibujarPantallaResumen(); break;
    case 1: dibujarPantallaCalle1();  break;
    case 2: dibujarPantallaCalle2();  break;
    case 3: dibujarPantallaSistema(); break;
  }
}

// Parte el mensaje en trozos de 16 caracteres y lo reparte en las 4 filas,
// cortando por espacios para no partir palabras a la mitad.
void dibujarTextoLibre() {
  String resto = lcdTextoLibre;
  for (int fila = 0; fila < 4; fila++) {
    if (resto.length() == 0) { imprimirFila(fila, ""); continue; }
    String trozo;
    if (resto.length() <= 16) {
      trozo = resto;
      resto = "";
    } else {
      int corte = resto.lastIndexOf(' ', 16);
      if (corte <= 0) corte = 16;
      trozo = resto.substring(0, corte);
      resto = resto.substring(corte);
      resto.trim();
    }
    imprimirFila(fila, trozo);
  }
}

void imprimirFila(int fila, String texto) {
  lcd.setCursor(0, fila);
  lcd.print(F("                "));
  lcd.setCursor(0, fila);
  if (texto.length() > 16) texto = texto.substring(0, 16);
  lcd.print(texto);
}

unsigned long duracionActualDeFase() {
  switch (estadoActual) {
    case VERDE_CALLE1:
    case VERDE_CALLE2:      return duracionVerdeCalculada;
    case AMARILLO_CALLE1:
    case AMARILLO_CALLE2:   return tiempoAmarillo;
    case TODO_ROJO_1a2:
    case TODO_ROJO_2a1:     return tiempoTodoRojo;
  }
  return verdeMinimo;
}

long tiempoRestanteFase() {
  if (!semaforoAutomatico) return 0;
  unsigned long transcurrido = millis() - tiempoInicioFase;
  long restante = (long)duracionActualDeFase() - (long)transcurrido;
  if (restante < 0) restante = 0;
  return restante / 1000;
}

String faseCortaCalle1() {
  switch (estadoActual) {
    case VERDE_CALLE1:    return "VER";
    case AMARILLO_CALLE1: return "AMA";
    default:              return "ROJ";
  }
}

String faseCortaCalle2() {
  switch (estadoActual) {
    case VERDE_CALLE2:    return "VER";
    case AMARILLO_CALLE2: return "AMA";
    default:              return "ROJ";
  }
}

String nombreFaseLarga() {
  switch (estadoActual) {
    case VERDE_CALLE1:    return "Verde C1";
    case AMARILLO_CALLE1: return "Amar. C1";
    case TODO_ROJO_1a2:   return "Rojo 1>2";
    case VERDE_CALLE2:    return "Verde C2";
    case AMARILLO_CALLE2: return "Amar. C2";
    case TODO_ROJO_2a1:   return "Rojo 2>1";
  }
  return "";
}

// Marca "S" si algo de esa zona esta simulado desde la consola
bool haySimulacionCalle(int desde) {
  for (int i = desde; i < desde + 3; i++) if (modoCny[i] != CNY_AUTO) return true;
  return false;
}

void dibujarPantallaResumen() {
  imprimirFila(0, "C1:" + faseCortaCalle1() + " C2:" + faseCortaCalle2() +
                  (semaforoAutomatico ? "   M1" : " MAN"));
  imprimirFila(1, "Resta:" + String(tiempoRestanteFase()) + "s CO2:" +
                  (co2Actual >= umbralCo2Alto ? "ALTO" : "OK"));
  imprimirFila(2, "Autos C1:" + String(autosCalle1Actual) + " C2:" + String(autosCalle2Actual));
  imprimirFila(3, "Noche:" + String(modoNoche ? "SI" : "NO") + " Ped:" +
                  String((solicitudPeaton1 || solicitudPeaton2) ? "SI" : "NO"));
}

void dibujarPantallaCalle1() {
  imprimirFila(0, String("--CALLE 1--") + (haySimulacionCalle(0) ? " S" : "  ") + " M2");
  imprimirFila(1, "LDR1:" + String(luz1Actual) + (modoNoche ? " NOC" : " DIA"));
  imprimirFila(2, "S1:" + String(cnyDetectaEstado[0] ? "C" : "_") +
                  " S2:" + String(cnyDetectaEstado[1] ? "C" : "_") +
                  " S3:" + String(cnyDetectaEstado[2] ? "C" : "_"));
  imprimirFila(3, "Autos:" + String(autosCalle1Actual) + " Bot:" + String(solicitudPeaton1 ? "SI" : "NO"));
}

void dibujarPantallaCalle2() {
  imprimirFila(0, String("--CALLE 2--") + (haySimulacionCalle(3) ? " S" : "  ") + " M3");
  imprimirFila(1, "LDR2:" + String(luz2Actual) + (modoNoche ? " NOC" : " DIA"));
  imprimirFila(2, "S4:" + String(cnyDetectaEstado[3] ? "C" : "_") +
                  " S5:" + String(cnyDetectaEstado[4] ? "C" : "_") +
                  " S6:" + String(cnyDetectaEstado[5] ? "C" : "_"));
  imprimirFila(3, "Autos:" + String(autosCalle2Actual) + " Bot:" + String(solicitudPeaton2 ? "SI" : "NO"));
}

void dibujarPantallaSistema() {
  imprimirFila(0, String("--SISTEMA--") + (haySimulacion() ? " S" : "  ") + " M4");
  imprimirFila(1, "CO2 crudo:" + String(co2Actual));
  imprimirFila(2, "Luz1:" + String(luz1Actual) + " Luz2:" + String(luz2Actual));
  imprimirFila(3, nombreFaseLarga() + " " + String(tiempoRestanteFase()) + "s");
}

void dibujarPantallaEmergenciaCO2() {
  imprimirFila(0, "!! PELIGRO !!");
  imprimirFila(1, "CO2 CRITICO");
  imprimirFila(2, "Valor:" + String(co2Actual));
  imprimirFila(3, "Evacuando Calle2");
}

bool haySimulacion() {
  for (int i = 0; i < 6; i++) if (modoCny[i] != CNY_AUTO) return true;
  for (int i = 0; i < 6; i++) if (simLed[i] >= 0) return true;
  if (simLdr[0] >= 0 || simLdr[1] >= 0) return true;
  if (simCo2 >= 0 || simNoche >= 0) return true;
  if (!semaforoAutomatico || !botonesFisicosActivos) return true;
  return false;
}
