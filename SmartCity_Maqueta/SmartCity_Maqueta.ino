/* ============================================================================
   CIUDAD AUTOADAPTABLE (SMART CITY) - MAQUETA CON ESP32
   ============================================================================
   Placa: ESP32-S3 (usa GPIO hasta el 42, por eso no es un ESP32 clasico)

   DESCRIPCION GENERAL
   --------------------
   La maqueta representa un cruce de dos calles perpendiculares:

     - CALLE 1 (horizontal, arriba en la foto): controlada por el
       Semaforo 1 (LR1/LY1/LG1). Antes del cruce peatonal hay 3 sensores
       infrarrojos (CNY1, CNY2, CNY3) que detectan vehiculos (objetos
       blancos) aproximandose. Junto al semaforo hay un sensor de luz
       LDR1 y un boton peatonal P1.

     - CALLE 2 (vertical, izquierda en la foto): controlada por el
       Semaforo 2 (LR2/LY2/LG2), con sus propios sensores CNY4, CNY5,
       CNY6, su LDR2 y su boton peatonal P2.

   Como las dos calles comparten la misma interseccion, NUNCA deben estar
   ambas en verde al mismo tiempo. El sistema funciona como una maquina
   de estados que va alternando el verde entre la Calle 1 y la Calle 2,
   con una fase amarilla y una fase "todo en rojo" de seguridad entre
   cada cambio (para dar tiempo a que la interseccion quede despejada).

   COMPORTAMIENTO "AUTOADAPTABLE":
     1. TRAFICO: mientras una calle esta en verde, se cuentan cuantos
        sensores CNY de ESA calle detectan vehiculos. Si hay cola, el
        tiempo de verde se alarga (hasta un maximo).
     2. PEATONES: al pulsar P1 o P2 (uno solo) se marca una "solicitud
        peatonal" que acorta el verde de los autos de esa calle.
     3. LUZ AMBIENTE (LDR1/LDR2): de noche se atenuan los LEDs (PWM).
     4. CALIDAD DE AIRE (CO2): si el CO2 es alto, se reduce el verde
        maximo para favorecer la rotacion del trafico.
     5. PANTALLA LCD I2C 16x4: como una sola pantalla no alcanza para
        mostrar el detalle de los 6 sensores CNY + 2 LDR + CO2 + estado
        de ambos semaforos, la informacion se reparte en 4 "modos" de
        pantalla. Para cambiar de pantalla se pulsan P1 y P2 AL MISMO
        TIEMPO (esto es distinto de pulsarlos por separado, que sigue
        sirviendo para pedir el cruce peatonal normal). Cada combo
        avanza una pantalla y al llegar a la ultima vuelve a la primera.
     6. RELOJ POR INTERNET: al arrancar, la maqueta se conecta al WiFi
        y pide la hora actual en zona UTC-5 (America/Bogota) a una API
        de tiempo. Esa hora queda seteada en el reloj interno, que
        sigue avanzando solo con millis(), y se muestra en el tablero
        digital (LCD). Como el modo "noche profunda" depende de la
        franja 23h-4h, ya no hace falta indicarla a mano por Serial:
        se calcula sola a partir del reloj sincronizado.
     7. TELEMETRIA: cada 5 segundos se envia un POST con el numero de
        vehiculos detectados en cada calle al servidor de pruebas
        (requestcatcher).

   NOTAS IMPORTANTES DE HARDWARE (leer antes de conectar):
     - Los pines LDR1(13), LDR2(12) y CO2(14) se leen con analogRead().
       En el ESP32-S3 estos pines pertenecen al ADC2, que NO se puede
       usar de forma fiable al mismo tiempo que el WiFi esta activo:
       analogRead() puede devolver 0 o basura mientras la radio
       transmite. Este sketch SI usa WiFi, asi que la lectura se hace
       con leerADC(), que descarta las lecturas invalidas y conserva
       el ultimo valor bueno (ver seccion "LECTURA ADC ROBUSTA").
       Si notas que los valores se quedan congelados, la solucion
       definitiva es recablear esos 3 sensores a pines del ADC1
       (GPIO 1 a 10); en esta maqueta quedan libres GPIO3 y GPIO10.
     - El pin P1 = GPIO1 y P2 = GPIO2. En algunas variantes el pin 1
       puede coincidir con TX0 de un Serial de depuracion. Si el boton
       P1 no responde bien, revisa que no este compartido con Serial.
     - CNY4(39), CNY5(38), CNY6(37): en el ESP32 clasico estos pines
       son "solo entrada" y no tienen resistencias pull-up/pull-down
       internas. Si detectas lecturas inestables, agrega una resistencia
       externa de 10k a 3.3V.
     - Los sensores CNY detectan objetos blancos/reflectantes. Por
       defecto este codigo asume que el sensor entrega LOW (0) cuando SI
       detecta un objeto. Si tu modulo funciona al reves, cambia la
       constante CNY_ACTIVO_EN_BAJO a false.
     - Instala la libreria "LiquidCrystal I2C" (autor: Frank de
       Brabander) desde el Gestor de Librerias de Arduino IDE.
     - Verifica la direccion I2C de tu pantalla (normalmente 0x27 o
       0x3F).
   ========================================================================= */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ============================================================================
// 1. DEFINICION DE PINES (tal como estan cableados en la maqueta)
// ============================================================================

// --- Sensores de luz ambiente (analogicos) ---
#define LDR1 13   // Sensor de luz semaforo 1
#define LDR2 12   // Sensor de luz semaforo 2

// --- Sensor de calidad de aire (analogico) ---
#define CO2  14

// --- Botones peatonales (digitales) ---
#define P1   1    // Boton peaton calle 1
#define P2   2    // Boton peaton calle 2

// --- Sensores infrarrojos de vehiculos (digitales) ---
#define CNY1 42   // Calle 1 - sensor lejano
#define CNY2 41   // Calle 1 - sensor intermedio
#define CNY3 40   // Calle 1 - sensor mas cercano al cruce
#define CNY4 39   // Calle 2 - sensor lejano
#define CNY5 38   // Calle 2 - sensor intermedio
#define CNY6 37   // Calle 2 - sensor mas cercano al cruce

// --- Semaforo 1 (calle horizontal) ---
#define LR1  5    // Rojo
#define LY1  4    // Amarillo
#define LG1  6    // Verde

// --- Semaforo 2 (calle vertical) ---
#define LR2  7    // Rojo
#define LY2  15   // Amarillo
#define LG2  16   // Verde

// ============================================================================
// 2. CONFIGURACION DE LA PANTALLA LCD I2C (16 columnas x 4 filas)
// ============================================================================
LiquidCrystal_I2C lcd(0x27, 16, 4);

// ============================================================================
// 3. PARAMETROS AJUSTABLES DEL SISTEMA
// ============================================================================

const bool CNY_ACTIVO_EN_BAJO = true;

const unsigned long VERDE_MINIMO       = 5000;
const unsigned long VERDE_MAXIMO       = 15000;
const unsigned long EXTENSION_POR_AUTO = 2000;
const unsigned long TIEMPO_AMARILLO    = 3000;
const unsigned long TIEMPO_TODO_ROJO   = 1000;

const int UMBRAL_NOCHE = 1000;
const int BRILLO_DIA   = 255;
const int UMBRAL_CO2_ALTO = 2500;

const unsigned long DEBOUNCE_MS = 200;

// Ventana de tiempo (ms) dentro de la cual, si P1 y P2 bajan los dos,
// se considera una pulsacion "combo" en vez de dos pulsaciones sueltas.
const unsigned long VENTANA_COMBO = 150;

// Cuantas pantallas de informacion existen (ver actualizarLCD)
const int NUM_MODOS_PANTALLA = 5;

// ============================================================================
// 3.b CONFIGURACION DE RED (WiFi + hora por internet + telemetria)
// ============================================================================

// --- Credenciales del WiFi al que se conecta la maqueta ---
const char* WIFI_SSID = "hotspot";
const char* WIFI_PASS = "isa20261";

// Cuanto esperamos como maximo a que conecte el WiFi durante el arranque.
// Si se vence, la maqueta arranca igual en modo offline: los semaforos
// NUNCA se quedan bloqueados esperando la red.
const unsigned long TIMEOUT_CONEXION_WIFI = 15000;

// Cada cuanto se reintenta la conexion si el WiFi se cae en caliente.
const unsigned long INTERVALO_REINTENTO_WIFI = 20000;

// --- API de hora (zona UTC-5, America/Bogota) ---
const char* URL_API_HORA = "http://worldtimeapi.org/api/timezone/America/Bogota";

// Servidores NTP de respaldo, por si la API HTTP no responde
const char* NTP_SERVIDOR_1 = "pool.ntp.org";
const char* NTP_SERVIDOR_2 = "time.nist.gov";
const long  DESFASE_UTC_SEGUNDOS = -5 * 3600; // UTC-5, Colombia (sin horario de verano)

// Si la sincronizacion falla, se reintenta cada cierto tiempo en segundo plano
const unsigned long INTERVALO_REINTENTO_HORA = 60000;

// --- Servidor de telemetria (mismo esquema del ejemplo de clase) ---
const char* TELEMETRIA_HOST = "http://isa.requestcatcher.com";
const char* TELEMETRIA_PATH = "/post";

// Cada cuanto se envian los datos de trafico
const unsigned long INTERVALO_ENVIO = 5000;

// Timeouts cortos: una peticion lenta no puede congelar el cruce.
// El de la hora se puede permitir mas margen porque solo corre en el arranque;
// el del POST se repite cada 5 s en pleno ciclo de semaforos, asi que va mas
// apretado (en el peor caso el loop se detiene ~1,2 s por envio fallido).
const uint16_t TIMEOUT_HTTP_HORA = 2000;
const uint16_t TIMEOUT_HTTP_POST = 1200;

// ============================================================================
// 4. MAQUINA DE ESTADOS DEL CRUCE
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
unsigned long duracionVerdeCalculada = VERDE_MINIMO;

// ============================================================================
// 5. VARIABLES DE ESTADO DE ENTRADAS
// ============================================================================
bool solicitudPeaton1 = false;
bool solicitudPeaton2 = false;

// --- Variables para distinguir pulsacion individual vs combo ---
bool p1PendienteConfirmar = false;   // P1 bajo y estamos esperando ver si P2 tambien baja
bool p2PendienteConfirmar = false;   // P2 bajo y estamos esperando ver si P1 tambien baja
unsigned long tiempoP1Bajo = 0;      // instante (millis) en que P1 bajo
unsigned long tiempoP2Bajo = 0;      // instante (millis) en que P2 bajo
int estadoAnteriorP1 = HIGH;
int estadoAnteriorP2 = HIGH;
unsigned long ultimoDebounceP1 = 0;
unsigned long ultimoDebounceP2 = 0;
bool comboEnCurso = false;           // evita repetir el cambio de modo mientras se mantienen pulsados

bool modoNoche = false;
int co2Actual = 0;

// --- Snapshot de sensores, se actualiza cada vuelta del loop y se usa
//     tanto para la logica como para mostrarlo en pantalla ---
int luz1Actual = 0;
int luz2Actual = 0;
bool cny1Detecta = false, cny2Detecta = false, cny3Detecta = false;
bool cny4Detecta = false, cny5Detecta = false, cny6Detecta = false;
int autosCalle1Actual = 0;
int autosCalle2Actual = 0;

// --- Que pantalla de informacion se esta mostrando ahora mismo (0 a 3) ---
int modoPantalla = 0;


// ---------- Reloj interno de la maqueta (zona UTC-5) ----------
// El reloj se "siembra" una sola vez (con la hora de internet, o a mano por
// Serial) y desde ahi avanza solo contando millis(). Se guarda como segundos
// transcurridos desde la medianoche.
bool relojSincronizado = false;         // true cuando ya tiene una hora valida
unsigned long segundosBaseDia = 0;      // segundos desde medianoche en el instante de la siembra
unsigned long millisBaseReloj = 0;      // valor de millis() en ese mismo instante
String origenHora = "---";              // "API", "NTP" o "MANUAL": de donde salio la hora

bool horaNocheProfundaIndicada = false; // true si la hora actual está en 23h-4h
int horaActualIndicada = -1;            // -1 = el reloj aún no tiene hora válida

// ---------- Estado de la red ----------
bool wifiConectado = false;
unsigned long ultimoIntentoWiFi = 0;
unsigned long ultimoIntentoHora = 0;

// ---------- Estado de la telemetria ----------
unsigned long ultimoEnvio = 0;
int ultimoCodigoHttp = 0;      // codigo de respuesta del ultimo POST (>0 = ok)
unsigned long enviosOk = 0;
unsigned long enviosFallidos = 0;

// ---------- Variables para el modo de parpadeo especial ----------
bool modoNocheProfundaActivo = false;
bool estadoParpadeoNocheProfunda = false;
unsigned long tUltimoParpadeoNocheProfunda = 0;
const unsigned long INTERVALO_PARPADEO_NOCHE = 400;

bool advertenciaHoraMostrada = false; // evita spamear el mensaje de error cada 50ms

// ---------- Variables de estado de la emergencia ----------
bool emergenciaCO2Activa = false;

// ============================================================================
// LED RGB integrado: refleja visualmente el nivel de CO2
//   Verde    -> CO2 normal (por debajo de UMBRAL_CO2_ALTO)
//   Amarillo -> CO2 medio (entre UMBRAL_CO2_ALTO y UMBRAL_CO2_EMERGENCIA)
//   Rojo     -> CO2 en peligro (por encima de UMBRAL_CO2_EMERGENCIA)
// ============================================================================
const uint8_t NIVEL_LED_RGB = 40; // brillo maximo por canal (0-255), evita encandilar

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("Iniciando Ciudad Autoadaptable..."));

  pinMode(LDR1, INPUT);
  pinMode(LDR2, INPUT);
  pinMode(CO2, INPUT);

  pinMode(P1, INPUT_PULLUP);
  pinMode(P2, INPUT_PULLUP);

  pinMode(CNY1, INPUT);
  pinMode(CNY2, INPUT);
  pinMode(CNY3, INPUT);
  pinMode(CNY4, INPUT);
  pinMode(CNY5, INPUT);
  pinMode(CNY6, INPUT);

  pinMode(LR1, OUTPUT);
  pinMode(LY1, OUTPUT);
  pinMode(LG1, OUTPUT);
  pinMode(LR2, OUTPUT);
  pinMode(LY2, OUTPUT);
  pinMode(LG2, OUTPUT);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Ciudad Autoadapt."));
  lcd.setCursor(0, 1);
  lcd.print(F("Inicializando..."));
  delay(1500);
  lcd.clear();

  rgbLedWrite(RGB_BUILTIN, 0, 0, 0); // apagado inicial

  // --- Arranque de la parte de red: conectar y pedir la hora UTC-5 ---
  // Si algo de esto falla, la maqueta sigue funcionando en modo offline.
  conectarWiFi();
  if (wifiConectado) {
    sincronizarHora(true); // arranque: puede usar el LCD y esperar
  } else {
    mostrarMensajeArranque("WiFi no conecto", "Modo offline");
    delay(1500);
  }
  lcd.clear();

  tiempoInicioFase = millis();
  duracionVerdeCalculada = VERDE_MINIMO;
  aplicarSemaforos();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  leerComandosSerial();
  leerSensoresDetalle();
  leerBotones();
  actualizarModoNoche();
  leerCO2();
  actualizarLedRGB();
  actualizarEmergenciaCO2();

  // --- Parte de red: nunca bloquea el cruce ---
  mantenerWiFi();          // reconecta en segundo plano si se cayo el WiFi
  actualizarReloj();       // avanza la hora y recalcula la franja 23h-4h
  enviarDatosTrafico();    // POST cada 5 s con los vehiculos de cada calle

  if (!emergenciaCO2Activa) {
    actualizarMaquinaEstados();
    actualizarNocheProfunda();
    actualizarLCD();
  } else {
    static unsigned long ultimaActualizacionEmergencia = 0;
    if (millis() - ultimaActualizacionEmergencia >= 500) {
      dibujarPantallaEmergenciaCO2();
      ultimaActualizacionEmergencia = millis();
    }
  }

  delay(50);
}

// ============================================================================
// LECTURA DETALLADA DE SENSORES (se guarda en variables globales para que
// tanto la maquina de estados como la pantalla usen los mismos datos)
// ============================================================================
void leerSensoresDetalle() {
  leerADC(LDR1, luz1Actual);
  leerADC(LDR2, luz2Actual);

  cny1Detecta = cnyDetecta(CNY1);
  cny2Detecta = cnyDetecta(CNY2);
  cny3Detecta = cnyDetecta(CNY3);
  cny4Detecta = cnyDetecta(CNY4);
  cny5Detecta = cnyDetecta(CNY5);
  cny6Detecta = cnyDetecta(CNY6);

  autosCalle1Actual = (cny1Detecta ? 1 : 0) + (cny2Detecta ? 1 : 0) + (cny3Detecta ? 1 : 0);
  autosCalle2Actual = (cny4Detecta ? 1 : 0) + (cny5Detecta ? 1 : 0) + (cny6Detecta ? 1 : 0);
}

// ============================================================================
// LECTURA ADC ROBUSTA (convivencia con el WiFi)
// ============================================================================
// LDR1(13), LDR2(12) y CO2(14) estan en el ADC2 del ESP32-S3, y el ADC2 se
// comparte con la radio WiFi: mientras la radio trabaja, analogRead() puede
// devolver 0 aunque el sensor tenga un valor real. Como en esta maqueta un 0
// exacto no es una lectura fisica plausible (siempre hay algo de tension en el
// divisor), tratamos el 0 como lectura invalida y conservamos el ultimo valor
// bueno en lugar de dejar que el sistema crea que se hizo de noche de golpe.
//
// Si prefieres precision total en vez de este apaño, recablea los 3 sensores a
// pines del ADC1 (GPIO 1-10); en esta maqueta quedan libres GPIO3 y GPIO10.
void leerADC(int pin, int &destino) {
  int lectura = analogRead(pin);
  if (wifiConectado && lectura == 0 && destino != 0) {
    return; // lectura descartada: casi seguro es interferencia del WiFi
  }
  destino = lectura;
}

bool cnyDetecta(int pin) {
  int lectura = digitalRead(pin);
  return CNY_ACTIVO_EN_BAJO ? (lectura == LOW) : (lectura == HIGH);
}

// ============================================================================
// LECTURA DE BOTONES: distingue pulsacion individual (peticion peatonal)
// de pulsacion combinada P1+P2 (cambio de pantalla)
// ============================================================================
void leerBotones() {
  int lecturaP1 = digitalRead(P1);
  int lecturaP2 = digitalRead(P2);

  // --- Deteccion de flancos de bajada (HIGH -> LOW) de cada boton ---
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

  // --- Si los dos botones estan pendientes y bajaron dentro de la
  //     ventana de combo, es una pulsacion conjunta: cambiar pantalla ---
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

  // --- Si paso la ventana de combo y solo uno de los dos bajo,
  //     se confirma como pulsacion individual (peticion peatonal) ---
  if (p1PendienteConfirmar && (millis() - tiempoP1Bajo) > VENTANA_COMBO) {
    if (!p2PendienteConfirmar) {
      solicitudPeaton1 = true;
      Serial.println(F("Peaton solicito cruce en Calle 1"));
    }
    p1PendienteConfirmar = false;
  }

  if (p2PendienteConfirmar && (millis() - tiempoP2Bajo) > VENTANA_COMBO) {
    if (!p1PendienteConfirmar) {
      solicitudPeaton2 = true;
      Serial.println(F("Peaton solicito cruce en Calle 2"));
    }
    p2PendienteConfirmar = false;
  }

  // --- Evita que, si se mantienen los dos botones apretados, la
  //     pantalla siga avanzando modo tras modo sin soltar ---
  if (lecturaP1 == LOW && lecturaP2 == LOW) {
    comboEnCurso = true;
  } else {
    comboEnCurso = false;
  }
}

// Avanza a la siguiente pantalla de informacion (ciclico: 0,1,2,3,0,1...)
void cambiarModoPantalla() {
  modoPantalla = (modoPantalla + 1) % NUM_MODOS_PANTALLA;
  lcd.clear(); // limpieza total al cambiar de pantalla para no dejar residuos
  Serial.print(F("Cambio a pantalla: "));
  Serial.println(modoPantalla + 1);
}

// ============================================================================
// MODO NOCTURNO
// ============================================================================
void actualizarModoNoche() {
  int promedioLuz = (luz1Actual + luz2Actual) / 2;
  modoNoche = (promedioLuz < UMBRAL_NOCHE);
}

// ============================================================================
// LECTURA DEL SENSOR DE CO2
// ============================================================================
void leerCO2() {
  leerADC(CO2, co2Actual);
  // NOTA: valor crudo de ADC (0-4095). Para ppm reales se necesita la
  // curva de calibracion propia del sensor (ej. MQ-135).
}

// ============================================================================
// MAQUINA DE ESTADOS DEL CRUCE
// ============================================================================
void actualizarMaquinaEstados() {
  unsigned long transcurrido = millis() - tiempoInicioFase;

  switch (estadoActual) {

    case VERDE_CALLE1: {
      unsigned long limiteVerde = duracionVerdeCalculada;
      if (solicitudPeaton1 && limiteVerde > VERDE_MINIMO) {
        limiteVerde = VERDE_MINIMO;
      }
      if (transcurrido >= limiteVerde) {
        cambiarEstado(AMARILLO_CALLE1);
      }
      break;
    }

    case AMARILLO_CALLE1:
      if (transcurrido >= TIEMPO_AMARILLO) {
        cambiarEstado(TODO_ROJO_1a2);
      }
      break;

    case TODO_ROJO_1a2:
      if (transcurrido >= TIEMPO_TODO_ROJO) {
        if (solicitudPeaton1) solicitudPeaton1 = false;
        duracionVerdeCalculada = calcularDuracionVerde(autosCalle2Actual);
        cambiarEstado(VERDE_CALLE2);
      }
      break;

    case VERDE_CALLE2: {
      unsigned long limiteVerde = duracionVerdeCalculada;
      if (solicitudPeaton2 && limiteVerde > VERDE_MINIMO) {
        limiteVerde = VERDE_MINIMO;
      }
      if (transcurrido >= limiteVerde) {
        cambiarEstado(AMARILLO_CALLE2);
      }
      break;
    }

    case AMARILLO_CALLE2:
      if (transcurrido >= TIEMPO_AMARILLO) {
        cambiarEstado(TODO_ROJO_2a1);
      }
      break;

    case TODO_ROJO_2a1:
      if (transcurrido >= TIEMPO_TODO_ROJO) {
        if (solicitudPeaton2) solicitudPeaton2 = false;
        duracionVerdeCalculada = calcularDuracionVerde(autosCalle1Actual);
        cambiarEstado(VERDE_CALLE1);
      }
      break;
  }
}

unsigned long calcularDuracionVerde(int autosDetectados) {
  unsigned long duracion = VERDE_MINIMO + (autosDetectados * EXTENSION_POR_AUTO);

  unsigned long maximoPermitido = VERDE_MAXIMO;

  if (duracion > maximoPermitido) duracion = maximoPermitido;
  if (duracion < VERDE_MINIMO) duracion = VERDE_MINIMO;
  return duracion;
}

void cambiarEstado(EstadoCruce nuevoEstado) {
  estadoActual = nuevoEstado;
  tiempoInicioFase = millis();
  aplicarSemaforos();
}

// ============================================================================
// APLICA EL ESTADO ACTUAL A LOS LEDS FISICOS
// ============================================================================
void aplicarSemaforos() {
  escribirLuz(LR1, false); escribirLuz(LY1, false); escribirLuz(LG1, false);
  escribirLuz(LR2, false); escribirLuz(LY2, false); escribirLuz(LG2, false);

  switch (estadoActual) {
    case VERDE_CALLE1:
      escribirLuz(LG1, true);
      escribirLuz(LR2, true);
      break;
    case AMARILLO_CALLE1:
      escribirLuz(LY1, true);
      escribirLuz(LR2, true);
      break;
    case TODO_ROJO_1a2:
      escribirLuz(LR1, true);
      escribirLuz(LR2, true);
      break;
    case VERDE_CALLE2:
      escribirLuz(LR1, true);
      escribirLuz(LG2, true);
      break;
    case AMARILLO_CALLE2:
      escribirLuz(LR1, true);
      escribirLuz(LY2, true);
      break;
    case TODO_ROJO_2a1:
      escribirLuz(LR1, true);
      escribirLuz(LR2, true);
      break;
  }
}

void escribirLuz(int pin, bool encendido) {
  if (!encendido) {
    analogWrite(pin, 0);
    return;
  }
  
  analogWrite(pin, BRILLO_DIA);
}

// ============================================================================
// PANTALLA LCD I2C (16x4) - 4 MODOS DE VISUALIZACION
// ============================================================================
// Se cambia de modo pulsando P1+P2 al mismo tiempo (ver leerBotones/
// cambiarModoPantalla). Cada modo llama a su propia funcion de dibujo.
void actualizarLCD() {
  static unsigned long ultimaActualizacion = 0;
  if (millis() - ultimaActualizacion < 500) return;
  ultimaActualizacion = millis();

  switch (modoPantalla) {
    case 0: dibujarPantallaResumen();   break;
    case 1: dibujarPantallaCalle1();    break;
    case 2: dibujarPantallaCalle2();    break;
    case 3: dibujarPantallaSistema();   break;
    case 4: dibujarPantallaRed();       break;
  }
}

// ============================================================================
// MODO NOCTURNO PROFUNDO: LY1 y LR2 parpadean SOLO si ambos LDR detectan
// baja luz Y la hora del reloj interno cae en 23h-4h. Esa hora ahora viene
// sincronizada de internet en UTC-5 (o forzada a mano con HORA:<0-23>).
// ============================================================================
void actualizarNocheProfunda() {
  bool ambosLdrBajos = (luz1Actual < UMBRAL_NOCHE) && (luz2Actual < UMBRAL_NOCHE);

  if (horaNocheProfundaIndicada && ambosLdrBajos) {
    // --- Condición cumplida: activa/mantiene el parpadeo ---
    modoNocheProfundaActivo = true;
    advertenciaHoraMostrada = false;

    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoNocheProfunda >= INTERVALO_PARPADEO_NOCHE) {
      estadoParpadeoNocheProfunda = !estadoParpadeoNocheProfunda;
      tUltimoParpadeoNocheProfunda = ahora;
    }

    int brillo = BRILLO_DIA;

    // Apaga el resto de luces de ambos semaforos
    analogWrite(LR1, 0);
    analogWrite(LG1, 0);
    analogWrite(LG2, 0);
    analogWrite(LY2, 0);

    // Parpadean juntos LY1 y LR2
    analogWrite(LY1, estadoParpadeoNocheProfunda ? brillo : 0);
    analogWrite(LR2, estadoParpadeoNocheProfunda ? brillo : 0);

  } else {
    // --- Condición NO cumplida: libera el forzado si estaba activo ---
    if (modoNocheProfundaActivo) {
      modoNocheProfundaActivo = false;
      aplicarSemaforos(); // vuelve al ciclo normal (o al dimming nocturno normal)
    }

    // --- Caso de inconsistencia: se indicó la hora pero los sensores
    //     no confirman baja luz en ambas vias ---
    if (horaNocheProfundaIndicada && !ambosLdrBajos) {
      if (!advertenciaHoraMostrada) {
        Serial.println(F("ADVERTENCIA: el reloj marca horario 23h-4h pero los sensores no detectan baja luz en ambas vias."));
        advertenciaHoraMostrada = true;
      }
    } else {
      advertenciaHoraMostrada = false;
    }
  }
}

// ============================================================================
// EMERGENCIA POR CO2: si el nivel supera el umbral de emergencia, se congela
// la maquina de estados normal, se fuerza LG2+LR1, y la pantalla muestra
// un aviso de peligro. Se mantiene asi mientras el nivel siga alto.
// Al bajar, el sistema vuelve exactamente al comportamiento normal.
// ============================================================================
void actualizarEmergenciaCO2() {
  bool nivelPeligroso = co2Actual > UMBRAL_CO2_ALTO;

  if (nivelPeligroso && !emergenciaCO2Activa) {
    // --- Entrando a emergencia ---
    emergenciaCO2Activa = true;
    Serial.println(F("EMERGENCIA CO2: nivel critico detectado. Forzando evacuacion Calle 2."));
  } else if (!nivelPeligroso && emergenciaCO2Activa) {
    // --- Saliendo de emergencia: el sistema vuelve a comportarse normal ---
    emergenciaCO2Activa = false;
    tiempoInicioFase = millis(); // evita que la fase recupere tiempo "perdido" durante la emergencia
    aplicarSemaforos();
    Serial.println(F("CO2 normalizado. Reanudando operacion normal."));
  }

  if (emergenciaCO2Activa) {
    escribirLuz(LR1, false); escribirLuz(LY1, false); escribirLuz(LG1, false);
    escribirLuz(LR2, false); escribirLuz(LY2, false); escribirLuz(LG2, false);
    escribirLuz(LG2, true);
    escribirLuz(LR1, true);
  }
}

// Escribe una fila completa, limpiandola primero para que nunca queden
// caracteres residuales de un texto mas largo mostrado antes.
void imprimirFila(int fila, String texto) {
  lcd.setCursor(0, fila);
  lcd.print(F("                ")); // 16 espacios: borra la fila
  lcd.setCursor(0, fila);
  if (texto.length() > 16) texto = texto.substring(0, 16);
  lcd.print(texto);
}

unsigned long duracionActualDeFase() {
  switch (estadoActual) {
    case VERDE_CALLE1:
    case VERDE_CALLE2:
      return duracionVerdeCalculada;
    case AMARILLO_CALLE1:
    case AMARILLO_CALLE2:
      return TIEMPO_AMARILLO;
    case TODO_ROJO_1a2:
    case TODO_ROJO_2a1:
      return TIEMPO_TODO_ROJO;
  }
  return VERDE_MINIMO;
}

long tiempoRestanteFase() {
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

// ############################################################################
// #                        BLOQUE DE RED (WiFi / HORA / POST)                #
// ############################################################################

// ============================================================================
// CONEXION WIFI
// ============================================================================
// Importante: el arranque espera como maximo TIMEOUT_CONEXION_WIFI. Si el
// hotspot no aparece, la maqueta NO se queda colgada: sigue al ciclo normal
// de semaforos en modo offline y reintenta la conexion desde el loop.
void conectarWiFi() {
  Serial.print(F("[WIFI]\tConectando a SSID: "));
  Serial.println(WIFI_SSID);

  mostrarMensajeArranque("Conectando WiFi", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < TIMEOUT_CONEXION_WIFI) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  wifiConectado = (WiFi.status() == WL_CONNECTED);
  ultimoIntentoWiFi = millis();

  if (wifiConectado) {
    Serial.print(F("[WIFI]\tConectado. IP: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("[WIFI]\tIntensidad de senal (RSSI): "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
    mostrarMensajeArranque("WiFi OK", WiFi.localIP().toString().c_str());
    delay(1200);
  } else {
    Serial.println(F("[WIFI]\tNo se pudo conectar. Arrancando en modo offline."));
  }
}

// Vigila el enlace desde el loop y reintenta sin bloquear nada.
void mantenerWiFi() {
  bool conectadoAhora = (WiFi.status() == WL_CONNECTED);

  if (conectadoAhora) {
    if (!wifiConectado) {
      wifiConectado = true;
      Serial.print(F("[WIFI]\tReconectado. IP: "));
      Serial.println(WiFi.localIP());
    }
    // Si el WiFi volvio pero el reloj nunca llego a sincronizarse, se
    // reintenta cada tanto en segundo plano.
    if (!relojSincronizado && (millis() - ultimoIntentoHora) > INTERVALO_REINTENTO_HORA) {
      ultimoIntentoHora = millis();
      sincronizarHora(false); // reintento de fondo: sin LCD ni delays largos
    }
    return;
  }

  if (wifiConectado) {
    wifiConectado = false;
    Serial.println(F("[WIFI]\tEnlace perdido. Reintentando en segundo plano."));
  }

  if ((millis() - ultimoIntentoWiFi) > INTERVALO_REINTENTO_WIFI) {
    ultimoIntentoWiFi = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS); // no esperamos aqui: el loop debe seguir
  }
}

// ============================================================================
// SINCRONIZACION DE LA HORA (zona UTC-5)
// ============================================================================
// Se intenta primero la API HTTP de hora y, si no responde, se cae a NTP.
// El resultado se "siembra" en el reloj interno, que a partir de ahi avanza
// solo con millis() (no hace falta volver a consultar internet).
// El parametro mostrarEnLcd distingue los dos usos:
//   true  -> arranque o RESYNC manual: se puede usar el LCD y hacer delay()
//   false -> reintento automatico desde el loop: NADA de LCD ni de delay(),
//            porque congelar el loop un segundo y medio en pleno ciclo
//            dejaria los semaforos clavados en una fase.
bool sincronizarHora(bool mostrarEnLcd) {
  if (mostrarEnLcd) mostrarMensajeArranque("Sincronizando", "hora UTC-5...");

  if (sincronizarHoraDesdeAPI()) {
    if (mostrarEnLcd) {
      mostrarMensajeArranque("Hora API OK", horaFormateada().c_str());
      delay(1500);
    }
    return true;
  }

  Serial.println(F("[HORA]\tLa API no respondio. Intentando por NTP..."));
  // En el arranque podemos esperar tranquilos; en un reintento de fondo no.
  if (sincronizarHoraDesdeNTP(mostrarEnLcd ? 8000 : 1500)) {
    if (mostrarEnLcd) {
      mostrarMensajeArranque("Hora NTP OK", horaFormateada().c_str());
      delay(1500);
    }
    return true;
  }

  Serial.println(F("[HORA]\tNo se pudo obtener la hora. Usa HORA:<0-23> por Serial."));
  if (mostrarEnLcd) {
    mostrarMensajeArranque("Sin hora", "Usa HORA:<0-23>");
    delay(1500);
  }
  return false;
}

// --- Opcion 1: API HTTP de tiempo ---
// worldtimeapi devuelve un JSON con el campo:
//   "datetime":"2026-09-09T14:23:11.123456-05:00"
// Solo nos interesa el tramo HH:MM:SS que va justo despues de la 'T', y como
// pedimos la zona America/Bogota ya viene convertido a UTC-5.
bool sincronizarHoraDesdeAPI() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_HORA);
  http.setTimeout(TIMEOUT_HTTP_HORA);

  if (!http.begin(URL_API_HORA)) {
    Serial.println(F("[HORA]\tNo se pudo abrir la conexion con la API."));
    return false;
  }

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.print(F("[HORA]\tLa API respondio con codigo: "));
    Serial.println(codigo);
    http.end();
    return false;
  }

  String cuerpo = http.getString();
  http.end();

  int posCampo = cuerpo.indexOf("\"datetime\"");
  if (posCampo < 0) {
    Serial.println(F("[HORA]\tRespuesta sin campo datetime."));
    return false;
  }

  // Nos paramos en la 'T' que separa fecha de hora dentro de ese campo
  int posT = cuerpo.indexOf('T', posCampo);
  if (posT < 0 || cuerpo.length() < (unsigned int)(posT + 9)) {
    Serial.println(F("[HORA]\tFormato de datetime inesperado."));
    return false;
  }

  int h = cuerpo.substring(posT + 1, posT + 3).toInt();
  int m = cuerpo.substring(posT + 4, posT + 6).toInt();
  int s = cuerpo.substring(posT + 7, posT + 9).toInt();

  if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
    Serial.println(F("[HORA]\tLa API devolvio una hora fuera de rango."));
    return false;
  }

  sembrarReloj(h, m, s, "API");
  return true;
}

// --- Opcion 2 (respaldo): NTP ---
// configTime aplica el desfase UTC-5 directamente, asi que la struct tm que
// devuelve getLocalTime ya viene en hora de Colombia.
bool sincronizarHoraDesdeNTP(unsigned long esperaMaxima) {
  if (WiFi.status() != WL_CONNECTED) return false;

  configTime(DESFASE_UTC_SEGUNDOS, 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2);

  struct tm datos;
  if (!getLocalTime(&datos, esperaMaxima)) {
    Serial.println(F("[HORA]\tNTP no respondio a tiempo."));
    return false;
  }

  sembrarReloj(datos.tm_hour, datos.tm_min, datos.tm_sec, "NTP");
  return true;
}

// Deja el reloj interno en la hora indicada y anota desde que instante de
// millis() empieza a contar.
void sembrarReloj(int h, int m, int s, const char* origen) {
  segundosBaseDia = (unsigned long)h * 3600UL + (unsigned long)m * 60UL + (unsigned long)s;
  millisBaseReloj = millis();
  relojSincronizado = true;
  origenHora = String(origen);

  Serial.print(F("[HORA]\tReloj sincronizado ("));
  Serial.print(origenHora);
  Serial.print(F(") -> "));
  Serial.print(horaFormateada());
  Serial.println(F(" (UTC-5)"));

  actualizarReloj();
}

// Segundos transcurridos desde la medianoche, segun el reloj interno.
unsigned long segundosDelDia() {
  if (!relojSincronizado) return 0;
  unsigned long transcurridos = (millis() - millisBaseReloj) / 1000UL;
  return (segundosBaseDia + transcurridos) % 86400UL;
}

// Recalcula la hora actual y, con ella, si estamos en la franja de noche
// profunda (23h-4h). Antes esto dependia de escribir HORA: por Serial; ahora
// sale solo del reloj y se actualiza sin intervencion.
void actualizarReloj() {
  if (!relojSincronizado) {
    horaActualIndicada = -1;
    horaNocheProfundaIndicada = false;
    return;
  }

  int hora = (int)(segundosDelDia() / 3600UL);
  horaActualIndicada = hora;
  horaNocheProfundaIndicada = (hora >= 23 || hora <= 4);
}

// Formatea un numero a 2 digitos ("7" -> "07") para que el reloj no baile
String dosDigitos(int valor) {
  return (valor < 10) ? ("0" + String(valor)) : String(valor);
}

// "HH:MM:SS" para el tablero digital y los logs
String horaFormateada() {
  if (!relojSincronizado) return "--:--:--";
  unsigned long total = segundosDelDia();
  return dosDigitos(total / 3600UL) + ":" +
         dosDigitos((total % 3600UL) / 60UL) + ":" +
         dosDigitos(total % 60UL);
}

// "HH:MM", para cuando no caben los segundos en la fila del LCD
String horaCorta() {
  if (!relojSincronizado) return "--:--";
  unsigned long total = segundosDelDia();
  return dosDigitos(total / 3600UL) + ":" + dosDigitos((total % 3600UL) / 60UL);
}

// ============================================================================
// TELEMETRIA: POST con el numero de vehiculos de cada calle (cada 5 s)
// ============================================================================
// Se manda de las dos formas para que sea comodo de revisar en requestcatcher:
//   - en la query string, como en el ejemplo de clase
//     (/post?vehiculos_calle1=2&vehiculos_calle2=1)
//   - y en el cuerpo, como JSON con el detalle sensor por sensor
void enviarDatosTrafico() {
  if ((millis() - ultimoEnvio) < INTERVALO_ENVIO) return;
  ultimoEnvio = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[SEND]\tSin WiFi: envio omitido."));
    return;
  }

  String url = String(TELEMETRIA_HOST) + TELEMETRIA_PATH +
               "?vehiculos_calle1=" + String(autosCalle1Actual) +
               "&vehiculos_calle2=" + String(autosCalle2Actual);

  String cuerpo = "{";
  cuerpo += "\"hora\":\"" + horaFormateada() + "\",";
  cuerpo += "\"vehiculos_calle1\":" + String(autosCalle1Actual) + ",";
  cuerpo += "\"vehiculos_calle2\":" + String(autosCalle2Actual) + ",";
  cuerpo += "\"sensores_calle1\":[" + String(cny1Detecta ? 1 : 0) + "," +
                                      String(cny2Detecta ? 1 : 0) + "," +
                                      String(cny3Detecta ? 1 : 0) + "],";
  cuerpo += "\"sensores_calle2\":[" + String(cny4Detecta ? 1 : 0) + "," +
                                      String(cny5Detecta ? 1 : 0) + "," +
                                      String(cny6Detecta ? 1 : 0) + "],";
  cuerpo += "\"fase\":\"" + nombreFaseLarga() + "\",";
  cuerpo += "\"co2\":" + String(co2Actual) + ",";
  cuerpo += "\"modo_noche\":" + String(modoNoche ? "true" : "false");
  cuerpo += "}";

  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_POST);
  http.setTimeout(TIMEOUT_HTTP_POST);

  if (!http.begin(url)) {
    Serial.println(F("[SEND]\tNo se pudo abrir la conexion con el servidor."));
    enviosFallidos++;
    ultimoCodigoHttp = -1;
    return;
  }

  http.addHeader("Content-Type", "application/json");
  ultimoCodigoHttp = http.POST(cuerpo);
  http.end();

  Serial.print(F("[SEND]\tCalle1: "));
  Serial.print(autosCalle1Actual);
  Serial.print(F(" | Calle2: "));
  Serial.print(autosCalle2Actual);
  Serial.print(F(" | status-code: "));
  Serial.println(ultimoCodigoHttp);

  if (ultimoCodigoHttp > 0) enviosOk++;
  else enviosFallidos++;
}

// Mensaje de dos lineas para las fases de arranque (WiFi, hora, etc.)
void mostrarMensajeArranque(const char* linea1, const char* linea2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  lcd.setCursor(0, 1);
  lcd.print(linea2);
}

// ============================================================================
// LECTURA DE COMANDOS POR SERIAL
// ============================================================================
//   HORA:<0-23>  fuerza la hora del reloj interno (para la demo)
//   RESYNC       vuelve a pedir la hora a internet
//   RED          imprime IP, hora vigente y contadores de envio
void leerComandosSerial() {
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    // --- HORA:<0-23> : fuerza la hora a mano (util para la demo) ---
    // Ojo: ya no basta con mover la bandera, porque actualizarReloj() la
    // recalcula cada vuelta del loop a partir del reloj interno. Por eso el
    // comando resiembra el reloj completo, y desde ahi sigue avanzando solo.
    if (comando.startsWith("HORA:")) {
      int hora = comando.substring(5).toInt();
      if (hora >= 0 && hora <= 23) {
        sembrarReloj(hora, 0, 0, "MANUAL");
        Serial.print(F("Hora forzada a mano: "));
        Serial.print(hora);
        Serial.println(horaNocheProfundaIndicada ? F("h (dentro de 23h-4h)") : F("h (fuera de ese rango)"));
      } else {
        Serial.println(F("Formato invalido. Usa HORA:0 a HORA:23"));
      }
    }

    // --- RESYNC : vuelve a pedir la hora a internet ---
    else if (comando.equalsIgnoreCase("RESYNC")) {
      if (WiFi.status() == WL_CONNECTED) {
        sincronizarHora(true); // lo pidio una persona: si mostramos avance
        lcd.clear();
      } else {
        Serial.println(F("RESYNC ignorado: no hay WiFi."));
      }
    }

    // --- RED : muestra el estado de la conexion y de los envios ---
    else if (comando.equalsIgnoreCase("RED")) {
      Serial.print(F("[RED]\tWiFi: "));
      Serial.println(wifiConectado ? WiFi.localIP().toString() : String("desconectado"));
      Serial.print(F("[RED]\tHora: "));
      Serial.print(horaFormateada());
      Serial.print(F(" (origen "));
      Serial.print(origenHora);
      Serial.println(F(")"));
      Serial.print(F("[RED]\tEnvios OK/fallidos: "));
      Serial.print(enviosOk);
      Serial.print('/');
      Serial.println(enviosFallidos);
    }
  }
}

void actualizarLedRGB() {
  if (co2Actual > UMBRAL_CO2_ALTO) {
    rgbLedWrite(RGB_BUILTIN, NIVEL_LED_RGB, 0, 0); // rojo
  } else if (co2Actual >= UMBRAL_CO2_ALTO) {
    rgbLedWrite(RGB_BUILTIN, NIVEL_LED_RGB, NIVEL_LED_RGB, 0); // amarillo
  } else {
    rgbLedWrite(RGB_BUILTIN, 0, NIVEL_LED_RGB, 0); // verde
  }
}

// --- MODO 1: Resumen general de todo el cruce ---
// La fila 0 antes se pasaba de los 16 caracteres y el indicador "M1" se
// perdia al truncar; ahora entra justo. La fila 3 lleva ademas el reloj en
// formato corto para tener la hora siempre a la vista en el tablero.
void dibujarPantallaResumen() {
  imprimirFila(0, "C1:" + faseCortaCalle1() + " C2:" + faseCortaCalle2() + " M1");
  imprimirFila(1, "Resta:" + String(tiempoRestanteFase()) + "s CO2:" + (co2Actual >= UMBRAL_CO2_ALTO ? "ALTO" : "OK"));
  imprimirFila(2, "Autos C1:" + String(autosCalle1Actual) + " C2:" + String(autosCalle2Actual));
  imprimirFila(3, "N:" + String(modoNoche ? "SI" : "NO") +
                   " P:" + String((solicitudPeaton1 || solicitudPeaton2) ? "SI" : "NO") +
                   " " + horaCorta());
}

// --- MODO 2: Detalle sensor por sensor de la Calle 1 ---
void dibujarPantallaCalle1() {
  imprimirFila(0, "--CALLE 1--   M2");
  imprimirFila(1, "LDR1:" + String(luz1Actual) + (modoNoche ? " NOC" : " DIA"));
  imprimirFila(2, "S1:" + String(cny1Detecta ? "C" : "_") +
                   " S2:" + String(cny2Detecta ? "C" : "_") +
                   " S3:" + String(cny3Detecta ? "C" : "_"));
  imprimirFila(3, "Autos:" + String(autosCalle1Actual) + " Bot:" + String(solicitudPeaton1 ? "SI" : "NO"));
}

// --- MODO 3: Detalle sensor por sensor de la Calle 2 ---
void dibujarPantallaCalle2() {
  imprimirFila(0, "--CALLE 2--   M3");
  imprimirFila(1, "LDR2:" + String(luz2Actual) + (modoNoche ? " NOC" : " DIA"));
  imprimirFila(2, "S4:" + String(cny4Detecta ? "C" : "_") +
                   " S5:" + String(cny5Detecta ? "C" : "_") +
                   " S6:" + String(cny6Detecta ? "C" : "_"));
  imprimirFila(3, "Autos:" + String(autosCalle2Actual) + " Bot:" + String(solicitudPeaton2 ? "SI" : "NO"));
}

// --- MODO 4: Estado general del sistema / ambiente ---
void dibujarPantallaSistema() {
  imprimirFila(0, "--SISTEMA--   M4");
  imprimirFila(1, "CO2 crudo:" + String(co2Actual));
  imprimirFila(2, "Luz1:" + String(luz1Actual) + " Luz2:" + String(luz2Actual));
  imprimirFila(3, nombreFaseLarga() + " " + String(tiempoRestanteFase()) + "s");
}

// --- MODO 5: Reloj y estado de la conexion ---
// Es el "tablero digital" de la hora: muestra HH:MM:SS del reloj sincronizado
// en UTC-5, de donde salio esa hora, si el WiFi esta arriba y como van los
// envios de telemetria.
void dibujarPantallaRed() {
  imprimirFila(0, "--RED/HORA--  M5");
  imprimirFila(1, horaFormateada() + " " + origenHora);
  imprimirFila(2, wifiConectado ? WiFi.localIP().toString() : String("WiFi: OFFLINE"));
  imprimirFila(3, "POST ok:" + String(enviosOk) + " er:" + String(enviosFallidos));
}

// ============================================================================
// PANTALLA DE EMERGENCIA POR CO2
// ============================================================================
void dibujarPantallaEmergenciaCO2() {
  imprimirFila(0, "!! PELIGRO !!");
  imprimirFila(1, "CO2 CRITICO");
  imprimirFila(2, "Valor:" + String(co2Actual));
  imprimirFila(3, "Evacuando Calle 2");
}