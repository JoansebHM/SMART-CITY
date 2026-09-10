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
     3. LUZ (LDR) : los LDR solo sirven de indicador (dia/noche) y como
                    condicion de la noche profunda. NO atenuan los LEDs: el
                    brillo es siempre el mismo, se tapen o no los sensores.
     4. CO2       : si el aire esta cargado, se reduce el verde maximo; el LED
                    RGB de la placa lo indica (verde = OK, rojo = critico) y
                    si pasa el umbral se entra en EMERGENCIA: el ciclo se
                    congela con LG2+LR1 para evacuar por la Calle 2.
     5. LCD I2C   : 5 pantallas de informacion; se rotan con P1+P2 juntos.
     6. HORA      : al arrancar, la maqueta pide por WiFi la hora real en
                    zona UTC-5 (primero por NTP, y si esa red bloquea el
                    puerto 123, por una API HTTP) y la guarda en un reloj
                    interno que avanza solo con millis(). Tambien se puede
                    forzar a mano con "hora <0-23>". Si la hora cae en 23h-4h
                    Y los dos LDR ven poca luz, se entra en NOCHE PROFUNDA:
                    LY1 y LR2 parpadean.
     7. TELEMETRIA: cada 5 s se envia por WiFi un POST con el numero de
                    vehiculos de cada calle al servidor de pruebas
                    (requestcatcher). Es independiente del dashboard, que
                    sigue hablando por el puerto Serial.

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
     - LDR1(13), LDR2(12) y CO2(14) son analogicos y estan en el ADC2, que
       el ESP32-S3 comparte con la radio WiFi: mientras la radio trabaja,
       analogRead() puede devolver 0 aunque el sensor tenga un valor real.
       Como ahora SI se usa WiFi, esas lecturas pasan por leerADC(), que
       descarta el 0 y conserva el ultimo valor bueno. Es un apaño; si ves
       lecturas congeladas, la solucion de verdad es recablear esos tres
       sensores al ADC1 (GPIO 1-10). Tambien puedes esquivarlo del todo
       simulando los valores por consola ("ldr 1 800", "co2 2600").
     - Los CNY detectan objetos blancos. Por defecto se asume que el sensor
       entrega LOW cuando SI detecta; cambia cnyActivoEnBajo si es al reves.
     - Libreria necesaria: "LiquidCrystal I2C" (Frank de Brabander).
       Direccion I2C tipica: 0x27 o 0x3F.
   ========================================================================= */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

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
const uint8_t LCD_COLUMNAS = 16;
const uint8_t LCD_FILAS    = 4;

LiquidCrystal_I2C lcd(DIRECCION_LCD, LCD_COLUMNAS, LCD_FILAS);
bool lcdEncendido = true;
bool lcdPresente  = false;      // se detecta en setup(); si es false, se ignora el LCD

// --- Buffers para redibujar solo lo que cambia (ver volcarPantalla) ---
// lcdEnPantalla = lo que creemos que el LCD esta mostrando ahora
// lcdDeseado    = lo que queremos que muestre en el proximo refresco
char lcdEnPantalla[LCD_FILAS][LCD_COLUMNAS];
char lcdDeseado[LCD_FILAS][LCD_COLUMNAS];

const unsigned long INTERVALO_REFRESCO_LCD = 400;

// Cada cuanto se comprueba que el LCD sigue respondiendo en el bus I2C
const unsigned long INTERVALO_CHEQUEO_LCD = 2000;

// Reinicio preventivo del LCD (0 = desactivado). Hace falta porque el chequeo
// I2C solo detecta que el expansor deje de contestar; si lo que se corrompe es
// un comando que llega al HD44780 (por ejemplo un "display off"), el expansor
// sigue respondiendo tan normal y la pantalla se queda en blanco para siempre.
const unsigned long INTERVALO_REINIT_LCD = 60000;

const uint8_t FALLOS_I2C_PARA_REINICIAR = 2;
uint8_t fallosI2CSeguidos = 0;
unsigned long recuperacionesLCD = 0;

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
int umbralCo2Alto = 20500;

const unsigned long DEBOUNCE_MS   = 200;
const unsigned long VENTANA_COMBO = 150;
const int NUM_MODOS_PANTALLA      = 5;

// ============================================================================
// 3.b RED: WiFi, hora por internet y telemetria HTTP
// ============================================================================
// Todo lo de red corre en una tarea aparte (ver tareaRed) para que ninguna
// espera de internet pueda congelar el cruce, la consola ni el LCD.

const char* WIFI_SSID = "Familia HM";
const char* WIFI_PASS = "PepisySebas123*";

// Si el WiFi se cae, cada cuanto se reintenta (sin bloquear el loop)
const unsigned long INTERVALO_REINTENTO_WIFI = 20000;

// --- De donde se saca la hora, en orden de preferencia ---
//
// 1) NTP. Es el metodo principal: es el protocolo hecho para esto, no usa TLS,
//    no hay que parsear nada y funciona en practicamente cualquier red.
//    configTime() ya aplica el desfase, asi que la hora sale en UTC-5.
const char* NTP_SERVIDOR_1 = "pool.ntp.org";
const char* NTP_SERVIDOR_2 = "time.google.com";
const char* NTP_SERVIDOR_3 = "time.nist.gov";
const long  DESFASE_UTC_SEGUNDOS = -5 * 3600;  // UTC-5 (Colombia, sin horario de verano)

// Cuanto se espera a que NTP conteste. El primer sincronizado incluye resolver
// el DNS del pool, asi que necesita mas margen del que parece.
const unsigned long ESPERA_NTP = 10000;

// 2) API HTTP de respaldo, por si en esa red el puerto UDP 123 (NTP) esta
//    bloqueado. Devuelve la hora en UTC y SIN segundos, asi que hay que
//    restarle 5 horas; por eso es el plan B y no el principal.
//
//    OJO: aqui estaba worldtimeapi.org y era la razon por la que la hora no
//    entraba nunca: ese servicio esta caido (acepta la conexion TCP y luego la
//    corta sin responder). Se cambio por worldclockapi.com, que si responde por
//    HTTP plano. Si algun dia este tambien muere, NTP sigue cubriendo el caso.
const char* URL_API_HORA = "http://worldclockapi.com/api/json/utc/now";

// Reintentos cuando no se pudo obtener la hora: al principio seguido (una red
// recien conectada suele tardar un poco en enrutar), y luego mas espaciado
// para no estar molestando cada minuto si de plano no hay salida a internet.
const unsigned long REINTENTO_HORA_RAPIDO = 10000;
const unsigned long REINTENTO_HORA_LENTO  = 60000;
const uint8_t INTENTOS_HORA_RAPIDOS = 6;   // ~1 min de intentos seguidos

// --- Servidor de telemetria ---
const char* TELEMETRIA_HOST = "http://grupo1.requestcatcher.com";
const char* TELEMETRIA_PATH = "/post";

const unsigned long INTERVALO_ENVIO = 5000;    // un POST cada 5 s

// Timeouts cortos: aunque el POST va en otra tarea, no queremos que un
// servidor caido deje envios colgados acumulandose.
const uint16_t TIMEOUT_HTTP_HORA = 3000;
const uint16_t TIMEOUT_HTTP_POST = 2000;

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
// 9. RELOJ INTERNO (UTC-5) Y NOCHE PROFUNDA
// ============================================================================
// La placa no tiene reloj de verdad, asi que se "siembra" uno: se le da una
// hora de partida (la que pide por WiFi al arrancar, o la que se fuerce con
// "hora <0-23>") y desde ahi avanza sola contando millis(). Se guarda como
// segundos transcurridos desde la medianoche.
//
// Si la hora cae en la franja 23h-4h Y los dos LDR ven poca luz, los
// semaforos pasan a modo intermitente de madrugada: LY1 y LR2 parpadean
// juntos y el resto se apaga.
bool relojSincronizado = false;      // true cuando el reloj ya tiene hora valida
unsigned long segundosBaseDia = 0;   // segundos desde medianoche al sembrarlo
unsigned long millisBaseReloj = 0;   // valor de millis() en ese mismo instante
String origenHora = "---";           // "API", "NTP" o "MANUAL"

int  horaActualIndicada = -1;            // -1 = el reloj aun no tiene hora
bool horaNocheProfundaIndicada = false;  // true si esa hora esta en 23h-4h

// ============================================================================
// 9.b ESTADO DE LA RED Y DE LA TELEMETRIA
// ============================================================================
bool wifiHabilitado = true;          // se puede apagar por consola ("wifi off")
bool wifiConectado = false;
unsigned long ultimoIntentoWiFi = 0;
unsigned long ultimoIntentoHora = 0;
uint8_t intentosHora = 0;            // para espaciar los reintentos poco a poco

unsigned long ultimoEnvio = 0;
int ultimoCodigoHttp = 0;            // codigo del ultimo POST (>0 = respondio)
unsigned long enviosOk = 0;
unsigned long enviosFallidos = 0;

// Foto de los datos que se van a enviar. La rellena el loop y la lee la tarea
// de red; envioEnCurso coordina el turno de cada uno, asi que no hace falta
// un mutex: mientras es true, el loop no toca la struct.
struct MuestraTrafico {
  int autos1;
  int autos2;
  bool cny[6];
  int co2;
  bool noche;
  char hora[9];        // "HH:MM:SS"
  char fase[16];
};

MuestraTrafico muestraPendiente;
volatile bool envioEnCurso = false;      // hay un POST pendiente o en marcha
volatile bool solicitudHora = false;     // alguien pidio sincronizar la hora
TaskHandle_t tareaRedHandle = NULL;

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
  // Sin timeout, si el bus se queda con SDA pegado a masa (ruido, un cable
  // suelto) cualquier operacion I2C se bloquearia para siempre y con ella todo
  // el cruce. Con 50 ms peor caso la operacion falla y vigilarLCD() lo detecta.
  Wire.setTimeOut(50);

  Wire.beginTransmission(DIRECCION_LCD);
  lcdPresente = (Wire.endTransmission() == 0);

  if (lcdPresente) {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    invalidarPantalla();

    // Mensaje de bienvenida por la misma via que todo lo demas, para que el
    // buffer y la pantalla arranquen sincronizados.
    imprimirFila(0, "Ciudad Autoadapt");   // 16 chars justos (antes eran 17)
    imprimirFila(1, "Consola Serial");
    imprimirFila(2, "");
    imprimirFila(3, "");
    volcarPantalla();
    delay(1200);
    limpiarLCD();
  }

#ifdef RGB_BUILTIN
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);   // LED RGB de la placa apagado al arrancar
#endif

  tiempoInicioFase = millis();
  duracionVerdeCalculada = verdeMinimo;
  aplicarSemaforos();

  // --- Red ---
  // A proposito NO esperamos aqui a que el WiFi conecte: la consola es lo
  // primero que tiene que estar viva. Se lanza la conexion y la tarea de red
  // se encarga del resto (pedir la hora en cuanto haya enlace, y los POST).
  iniciarRed();

  Serial.println();
  Serial.println(F("=== CIUDAD AUTOADAPTABLE - CONSOLA LISTA ==="));
  Serial.println(F("Escribe 'ayuda' para ver todos los comandos."));
  Serial.println(F("La hora se pide sola por WiFi; mira su estado con 'red'."));
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

  mantenerWiFi();           // 10. vigila el enlace y reconecta, sin bloquear
  actualizarReloj();        // 11. avanza la hora y recalcula la franja 23h-4h
  programarEnvio();         // 12. cada 5 s deja una muestra lista para la tarea de red

  // La emergencia por CO2 manda sobre todo lo demas: mientras dure, ni la
  // maquina de estados ni el intermitente de madrugada tocan los semaforos.
  if (!emergenciaCO2Activa) {
    actualizarMaquinaEstados();
    actualizarNocheProfunda();
  }

  aplicarSemaforos();       // se aplica cada vuelta: el brillo reacciona al instante
  vigilarLCD();             // detecta y repara una pantalla colgada por ruido I2C
  actualizarLCD();
  imprimirMonitorContinuo();

  delay(20);
}

// ============================================================================
// LECTURA (O SIMULACION) DE SENSORES
// ============================================================================
// ----------------------------------------------------------------------------
// LECTURA ADC ROBUSTA (convivencia con el WiFi)
// ----------------------------------------------------------------------------
// LDR1(13), LDR2(12) y CO2(14) estan en el ADC2, que el ESP32-S3 comparte con
// la radio WiFi: mientras la radio trabaja, analogRead() puede devolver 0
// aunque el sensor tenga un valor real. Un 0 exacto no es una lectura fisica
// plausible aqui (el divisor siempre deja algo de tension), asi que lo
// tratamos como invalido y conservamos el ultimo valor bueno. Si no, el
// sistema creeria que se hizo de noche de golpe cada vez que se manda un POST.
void leerADC(int pin, int &destino) {
  int lectura = analogRead(pin);
  if (wifiConectado && lectura == 0 && destino != 0) return; // interferencia
  destino = lectura;
}

void leerSensoresDetalle() {
  // Si el valor esta simulado por consola se usa tal cual; el apaño del ADC2
  // solo hace falta cuando se lee el sensor de verdad.
  if (simLdr[0] >= 0) luz1Actual = simLdr[0]; else leerADC(LDR1, luz1Actual);
  if (simLdr[1] >= 0) luz2Actual = simLdr[1]; else leerADC(LDR2, luz2Actual);

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
  if (simCo2 >= 0) co2Actual = simCo2; else leerADC(CO2, co2Actual);
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
  // Ya no hace falta limpiar aqui: cada fila se compone completa y rellenada
  // con espacios en cada refresco, asi que no puede quedar residuo. Y quitarlo
  // evita el parpadeo en negro que se veia al cambiar de pantalla.
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

  // El brillo ya NO depende de los LDR. Antes era "modoNoche ? brilloNoche :
  // brilloDia", asi que tapar los dos sensores atenuaba todos los semaforos;
  // eso se quito a proposito. modoNoche se sigue calculando, pero solo como
  // indicador (LCD, telemetria) y como condicion de la noche profunda.
  // La variable brilloNoche se conserva para no romper los comandos ni el
  // dashboard, pero ya no se aplica en ninguna parte.
  int brilloBase = brilloDia;
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
    limpiarLCD();
    avisoEvento(F("EMERGENCIA CO2: nivel critico. Evacuacion por la Calle 2."));
  } else if (!nivelPeligroso && emergenciaCO2Activa) {
    emergenciaCO2Activa = false;
    // La fase arranca de cero: no debe "recuperar" el tiempo de la emergencia.
    tiempoInicioFase = millis();
    limpiarLCD();
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

/* ############################################################################
   #                    RED: WIFI, HORA POR INTERNET Y POST                   #
   ############################################################################
   Reparto de responsabilidades:
     - El LOOP nunca espera a la red. Solo mira el estado del enlace, avanza
       el reloj y deja una muestra preparada cada 5 s.
     - La TAREA DE RED (nucleo 0) es la unica que hace HTTP: pide la hora y
       manda los POST. Puede tardar lo que haga falta sin afectar al cruce,
       porque el loop de Arduino corre en el otro nucleo.
   ######################################################################### */

// ============================================================================
// ARRANQUE DE LA RED
// ============================================================================
void iniciarRed() {
  if (!wifiHabilitado) {
    Serial.println(F("[WIFI] Deshabilitado por configuracion."));
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ultimoIntentoWiFi = millis();

  Serial.print(F("[WIFI] Conectando a '"));
  Serial.print(WIFI_SSID);
  Serial.println(F("' en segundo plano..."));

  solicitudHora = true; // en cuanto haya enlace, la tarea pedira la hora

  // 8 KB de pila: HTTPClient necesita bastante para sus buffers.
  BaseType_t creada = xTaskCreatePinnedToCore(
      tareaRed, "red", 8192, NULL, 1, &tareaRedHandle, 0);

  if (creada != pdPASS) {
    tareaRedHandle = NULL;
    Serial.println(F("[WIFI] ERROR: no se pudo crear la tarea de red."));
  }
}

// ============================================================================
// VIGILANCIA DEL ENLACE (corre en el loop, nunca espera)
// ============================================================================
void mantenerWiFi() {
  if (!wifiHabilitado) return;

  bool conectadoAhora = (WiFi.status() == WL_CONNECTED);

  if (conectadoAhora) {
    if (!wifiConectado) {
      wifiConectado = true;
      Serial.print(F("[WIFI] Conectado. IP: "));
      Serial.println(WiFi.localIP());
      solicitudHora = true;   // enlace nuevo: aprovechamos para pedir la hora
    }

    // Si el reloj nunca llego a sincronizarse, se reintenta: seguido durante
    // el primer minuto (una red recien conectada tarda un poco en enrutar) y
    // mas espaciado despues, para no insistir cada minuto si no hay salida.
    if (!relojSincronizado && !solicitudHora) {
      unsigned long espera = (intentosHora < INTENTOS_HORA_RAPIDOS)
                               ? REINTENTO_HORA_RAPIDO
                               : REINTENTO_HORA_LENTO;
      if ((millis() - ultimoIntentoHora) > espera) {
        ultimoIntentoHora = millis();
        if (intentosHora < 255) intentosHora++;
        solicitudHora = true;
      }
    }

    if (solicitudHora) despertarTareaRed();
    return;
  }

  if (wifiConectado) {
    wifiConectado = false;
    Serial.println(F("[WIFI] Enlace perdido. Reintentando en segundo plano."));
  }

  if ((millis() - ultimoIntentoWiFi) > INTERVALO_REINTENTO_WIFI) {
    ultimoIntentoWiFi = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS); // no esperamos: el loop debe seguir
  }
}

void despertarTareaRed() {
  if (tareaRedHandle != NULL) xTaskNotifyGive(tareaRedHandle);
}

// ============================================================================
// RELOJ INTERNO
// ============================================================================

// Deja el reloj en la hora indicada y anota desde que instante de millis()
// empieza a contar. Es el unico sitio donde se pone en hora el sistema.
void sembrarReloj(int h, int m, int s, const char* origen) {
  segundosBaseDia = (unsigned long)h * 3600UL + (unsigned long)m * 60UL + (unsigned long)s;
  millisBaseReloj = millis();
  relojSincronizado = true;
  origenHora = String(origen);
  intentosHora = 0;   // ya hay hora: el contador de reintentos vuelve a cero

  actualizarReloj();

  Serial.print(F("[HORA] Reloj en hora ("));
  Serial.print(origenHora);
  Serial.print(F("): "));
  Serial.print(horaFormateada());
  Serial.println(F(" UTC-5"));
}

// Deja el reloj sin hora (equivale al comando "hora off")
void olvidarReloj() {
  relojSincronizado = false;
  origenHora = "---";
  horaActualIndicada = -1;
  horaNocheProfundaIndicada = false;
  intentosHora = 0;          // los reintentos vuelven a empezar seguidos
  ultimoIntentoHora = millis();
}

unsigned long segundosDelDia() {
  if (!relojSincronizado) return 0;
  unsigned long transcurridos = (millis() - millisBaseReloj) / 1000UL;
  return (segundosBaseDia + transcurridos) % 86400UL;
}

// Recalcula la hora vigente y, con ella, si estamos en la franja 23h-4h.
// Antes esto solo cambiaba cuando alguien escribia "hora"; ahora se actualiza
// sola en cada vuelta, asi que la noche profunda entra y sale por si misma.
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

String dosDigitos(int valor) {
  return (valor < 10) ? ("0" + String(valor)) : String(valor);
}

String horaFormateada() {
  if (!relojSincronizado) return "--:--:--";
  unsigned long t = segundosDelDia();
  return dosDigitos(t / 3600UL) + ":" +
         dosDigitos((t % 3600UL) / 60UL) + ":" +
         dosDigitos(t % 60UL);
}

String horaCorta() {
  if (!relojSincronizado) return "--:--";
  unsigned long t = segundosDelDia();
  return dosDigitos(t / 3600UL) + ":" + dosDigitos((t % 3600UL) / 60UL);
}

// ============================================================================
// TAREA DE RED (nucleo 0): lo unico que hace peticiones HTTP
// ============================================================================
void tareaRed(void *parametro) {
  for (;;) {
    // Duerme sin gastar CPU hasta que la despierten, o hasta 250 ms
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));

    if (WiFi.status() != WL_CONNECTED) continue;

    if (solicitudHora) {
      solicitudHora = false;
      sincronizarHora();
      // El reintento se cuenta desde que ESTE intento termino, no desde que se
      // encolo: una sincronizacion puede tardar mas de 10 s entre el timeout de
      // NTP y el de la API, y si no, el loop pediria otra estando esta a medias.
      ultimoIntentoHora = millis();
    }

    if (envioEnCurso) {
      enviarMuestra();
      envioEnCurso = false;   // libera la struct para que el loop la reescriba
    }
  }
}

// ============================================================================
// SINCRONIZACION DE LA HORA (UTC-5): primero NTP, y si falla la API HTTP
// ============================================================================
// El orden importa. NTP es el protocolo hecho para esto: no usa TLS, no hay
// que parsear nada y funciona en casi cualquier red. La API HTTP queda como
// plan B para redes donde el puerto UDP 123 este bloqueado.
bool sincronizarHora() {
  Serial.println(F("[HORA] Pidiendo la hora por NTP..."));
  if (sincronizarHoraDesdeNTP()) return true;

  Serial.println(F("[HORA] NTP no contesto. Probando la API HTTP..."));
  if (sincronizarHoraDesdeAPI()) return true;

  Serial.println(F("[HORA] Sin hora. Se reintentara; o ponla a mano con 'hora <0-23>'."));
  return false;
}

// configTime aplica el desfase UTC-5, asi que la struct tm que devuelve
// getLocalTime ya viene en hora de Colombia.
bool sincronizarHoraDesdeNTP() {
  configTime(DESFASE_UTC_SEGUNDOS, 0, NTP_SERVIDOR_1, NTP_SERVIDOR_2, NTP_SERVIDOR_3);

  struct tm datos;
  if (!getLocalTime(&datos, ESPERA_NTP)) {
    Serial.println(F("[HORA] NTP no respondio a tiempo."));
    return false;
  }

  sembrarReloj(datos.tm_hour, datos.tm_min, datos.tm_sec, "NTP");
  return true;
}

// worldclockapi devuelve un JSON con el campo:
//   "currentDateTime":"2026-09-10T03:33Z"
// Es hora UTC y SIN segundos, asi que hay que restarle 5 horas a mano para
// pasarla a UTC-5 y asumir segundo 0.
bool sincronizarHoraDesdeAPI() {
  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_HORA);
  http.setTimeout(TIMEOUT_HTTP_HORA);

  if (!http.begin(URL_API_HORA)) {
    Serial.println(F("[HORA] No se pudo abrir la conexion con la API."));
    return false;
  }

  int codigo = http.GET();
  if (codigo != HTTP_CODE_OK) {
    Serial.print(F("[HORA] La API respondio con codigo "));
    Serial.println(codigo);
    http.end();
    return false;
  }

  String cuerpo = http.getString();
  http.end();

  int posCampo = cuerpo.indexOf("\"currentDateTime\"");
  if (posCampo < 0) {
    Serial.println(F("[HORA] Respuesta sin campo currentDateTime."));
    return false;
  }

  // Nos paramos en la 'T' que separa la fecha de la hora dentro de ese campo
  int posT = cuerpo.indexOf('T', posCampo);
  if (posT < 0 || cuerpo.length() < (unsigned int)(posT + 6)) {
    Serial.println(F("[HORA] Formato de fecha inesperado."));
    return false;
  }

  int hUtc = cuerpo.substring(posT + 1, posT + 3).toInt();
  int m    = cuerpo.substring(posT + 4, posT + 6).toInt();

  if (hUtc < 0 || hUtc > 23 || m < 0 || m > 59) {
    Serial.println(F("[HORA] La API devolvio una hora fuera de rango."));
    return false;
  }

  // UTC -> UTC-5, dando la vuelta si se pasa de medianoche hacia atras
  int h = (hUtc + 24 + (int)(DESFASE_UTC_SEGUNDOS / 3600)) % 24;

  sembrarReloj(h, m, 0, "API");
  return true;
}

// ============================================================================
// TELEMETRIA: POST con los vehiculos de cada calle (cada 5 s)
// ============================================================================

// Corre en el LOOP: solo toma la foto y avisa. No espera a la red.
void programarEnvio() {
  if (!wifiHabilitado) return;
  if ((millis() - ultimoEnvio) < INTERVALO_ENVIO) return;
  ultimoEnvio = millis();

  if (!wifiConectado) return;   // sin enlace no hay nada que mandar

  // Si el envio anterior sigue en marcha, saltamos este turno en vez de
  // acumular retraso: mas vale perder una muestra que encolar envios.
  if (envioEnCurso) {
    Serial.println(F("[SEND] El envio anterior sigue en curso: turno omitido."));
    return;
  }

  muestraPendiente.autos1 = autosCalle1Actual;
  muestraPendiente.autos2 = autosCalle2Actual;
  for (int i = 0; i < 6; i++) muestraPendiente.cny[i] = cnyDetectaEstado[i];
  muestraPendiente.co2 = co2Actual;
  muestraPendiente.noche = modoNoche;
  horaFormateada().toCharArray(muestraPendiente.hora, sizeof(muestraPendiente.hora));
  nombreFaseLarga().toCharArray(muestraPendiente.fase, sizeof(muestraPendiente.fase));

  envioEnCurso = true;
  despertarTareaRed();
}

// Corre en la TAREA DE RED: aqui si se puede esperar a que conteste el servidor.
// Se manda de dos formas para que sea comodo de revisar en requestcatcher:
//   - en la query string (/post?vehiculos_calle1=2&vehiculos_calle2=1)
//   - y en el cuerpo, como JSON con el detalle sensor por sensor
void enviarMuestra() {
  MuestraTrafico m = muestraPendiente;   // copia local

  String url = String(TELEMETRIA_HOST) + TELEMETRIA_PATH +
               "?vehiculos_calle1=" + String(m.autos1) +
               "&vehiculos_calle2=" + String(m.autos2);

  String cuerpo = "{";
  cuerpo += "\"hora\":\"" + String(m.hora) + "\",";
  cuerpo += "\"vehiculos_calle1\":" + String(m.autos1) + ",";
  cuerpo += "\"vehiculos_calle2\":" + String(m.autos2) + ",";
  cuerpo += "\"sensores_calle1\":[" + String(m.cny[0] ? 1 : 0) + "," +
                                      String(m.cny[1] ? 1 : 0) + "," +
                                      String(m.cny[2] ? 1 : 0) + "],";
  cuerpo += "\"sensores_calle2\":[" + String(m.cny[3] ? 1 : 0) + "," +
                                      String(m.cny[4] ? 1 : 0) + "," +
                                      String(m.cny[5] ? 1 : 0) + "],";
  cuerpo += "\"fase\":\"" + String(m.fase) + "\",";
  cuerpo += "\"co2\":" + String(m.co2) + ",";
  cuerpo += "\"modo_noche\":" + String(m.noche ? "true" : "false");
  cuerpo += "}";

  HTTPClient http;
  http.setConnectTimeout(TIMEOUT_HTTP_POST);
  http.setTimeout(TIMEOUT_HTTP_POST);

  if (!http.begin(url)) {
    Serial.println(F("[SEND] No se pudo abrir la conexion con el servidor."));
    ultimoCodigoHttp = -1;
    enviosFallidos++;
    return;
  }

  http.addHeader("Content-Type", "application/json");
  ultimoCodigoHttp = http.POST(cuerpo);
  http.end();

  Serial.print(F("[SEND] C1:"));
  Serial.print(m.autos1);
  Serial.print(F(" C2:"));
  Serial.print(m.autos2);
  Serial.print(F(" -> status "));
  Serial.println(ultimoCodigoHttp);

  if (ultimoCodigoHttp > 0) enviosOk++;
  else enviosFallidos++;
}

// ============================================================================
// PANTALLA LCD I2C (16x4) - 5 MODOS
// ============================================================================
// Compone la pantalla que toca y la vuelca al LCD. Es el unico punto del
// programa donde se escribe de verdad en la pantalla.
void pintarPantallaActual() {
  if (!lcdPresente || !lcdEncendido) return;

  // La emergencia por CO2 tapa cualquier otra pantalla mientras dure.
  if (emergenciaCO2Activa) {
    dibujarPantallaEmergenciaCO2();
    volcarPantalla();
    return;
  }

  // Un mensaje libre (por ejemplo "AMBULANCIA") tapa las pantallas normales
  // mientras este vigente. Sirve para que la maqueta anuncie la emergencia.
  if (lcdTextoLibre.length() > 0) {
    if (lcdTextoHasta > 0 && millis() > lcdTextoHasta) {
      lcdTextoLibre = "";   // caduco: seguimos a la pantalla normal de abajo
    } else {
      dibujarTextoLibre();
      volcarPantalla();
      return;
    }
  }

  switch (modoPantalla) {
    case 0: dibujarPantallaResumen(); break;
    case 1: dibujarPantallaCalle1();  break;
    case 2: dibujarPantallaCalle2();  break;
    case 3: dibujarPantallaSistema(); break;
    case 4: dibujarPantallaRed();     break;
  }

  volcarPantalla();
}

void actualizarLCD() {
  static unsigned long ultimaActualizacion = 0;
  if (millis() - ultimaActualizacion < INTERVALO_REFRESCO_LCD) return;
  ultimaActualizacion = millis();

  pintarPantallaActual();
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

// ============================================================================
// RENDERIZADO DEL LCD POR DIFERENCIAS
// ============================================================================
// ANTES: cada refresco reescribia las 4 filas enteras, y cada fila se borraba
// con 16 espacios antes de escribir el texto. Son ~128 caracteres por refresco
// sobre un bus I2C a 100 kHz, varias veces por segundo. Eso trae dos problemas:
// el bus va saturado (mas ocasiones de corromperse por ruido, y ahi el LCD se
// queda pillado sin poder recuperarse solo) y se ve parpadeo, porque entre el
// borrado y la escritura la fila queda un instante en blanco.
//
// AHORA: imprimirFila() no toca el LCD, solo deja el texto en un buffer de
// memoria. Al final del refresco, volcarPantalla() compara ese buffer con lo
// que ya hay en pantalla y manda SOLO los caracteres distintos: en reposo son
// 2 o 3 (los digitos del reloj) en vez de 128.
//
// Efecto secundario util: como cada fila se compone completa y rellenada con
// espacios, es imposible que queden residuos de un texto anterior mas largo,
// que era justo lo que el borrado con espacios trataba de evitar.

// Deja el texto de una fila en el buffer, recortado o rellenado a 16 chars.
void imprimirFila(int fila, String texto) {
  if (fila < 0 || fila >= LCD_FILAS) return;
  for (uint8_t c = 0; c < LCD_COLUMNAS; c++) {
    lcdDeseado[fila][c] = (c < texto.length()) ? texto[c] : ' ';
  }
}

// Marca toda la pantalla como "desconocida" para forzar un redibujado
// completo. Se usa tras un lcd.clear(), tras reinicializar el LCD o al
// escribir en el directamente: en esos casos el buffer de lo que creiamos
// tener en pantalla ya no es de fiar.
void invalidarPantalla() {
  for (uint8_t f = 0; f < LCD_FILAS; f++) {
    for (uint8_t c = 0; c < LCD_COLUMNAS; c++) lcdEnPantalla[f][c] = '\0';
  }
}

// Borra la pantalla de verdad. SIEMPRE hay que limpiar por aqui y nunca con
// lcd.clear() suelto: si se borra el LCD sin invalidar el buffer, el
// renderizador cree que el contenido sigue ahi, no reescribe nada y la
// pantalla se queda en blanco para siempre.
void limpiarLCD() {
  if (!lcdPresente) return;
  lcd.clear();
  invalidarPantalla();
}

// Compara buffer deseado vs pantalla y escribe solo los tramos que difieren.
// Los tramos separados por 1 o 2 caracteres iguales se fusionan: reposicionar
// el cursor cuesta lo mismo que escribir un caracter, asi que saltar huecos
// tan cortos saldria mas caro que reescribirlos.
void volcarPantalla() {
  if (!lcdPresente || !lcdEncendido) return;

  for (uint8_t f = 0; f < LCD_FILAS; f++) {
    uint8_t c = 0;
    while (c < LCD_COLUMNAS) {
      if (lcdDeseado[f][c] == lcdEnPantalla[f][c]) { c++; continue; }

      uint8_t inicio = c;
      uint8_t fin = c;              // ultimo caracter distinto encontrado
      uint8_t igualesSeguidos = 0;

      for (uint8_t j = c; j < LCD_COLUMNAS && igualesSeguidos <= 2; j++) {
        if (lcdDeseado[f][j] != lcdEnPantalla[f][j]) { fin = j; igualesSeguidos = 0; }
        else igualesSeguidos++;
      }

      lcd.setCursor(inicio, f);
      for (uint8_t k = inicio; k <= fin; k++) {
        lcd.write(lcdDeseado[f][k]);
        lcdEnPantalla[f][k] = lcdDeseado[f][k];
      }

      c = fin + 1;
    }
  }
}

// ============================================================================
// VIGILANCIA DEL BUS I2C
// ============================================================================
// El HD44780 no avisa de nada: si un pico de ruido le corrompe un comando, se
// queda mostrando basura (o nada) para siempre. Lo que si podemos hacer es
// preguntarle al expansor I2C si sigue respondiendo, y reinicializar si no.
bool lcdResponde() {
  Wire.beginTransmission(DIRECCION_LCD);
  return (Wire.endTransmission() == 0);   // 0 = contesto ACK
}

// Reinicializa el LCD y lo repinta en el acto. El repintado inmediato importa:
// si esperasemos al siguiente refresco, la pantalla se quedaria en blanco casi
// medio segundo y el parpadeo seria bien visible.
void reiniciarLCD(const __FlashStringHelper* motivo) {
  if (!lcdPresente) return;

  recuperacionesLCD++;
  Serial.print(F("[LCD] Reinicializando pantalla ("));
  Serial.print(motivo);
  Serial.print(F(") #"));
  Serial.println(recuperacionesLCD);

  lcd.init();
  if (lcdEncendido) lcd.backlight(); else lcd.noBacklight();
  invalidarPantalla();
  pintarPantallaActual();
}

void vigilarLCD() {
  if (!lcdPresente) return;

  // --- Reinicio preventivo periodico ---
  static unsigned long ultimoReinit = 0;
  if (INTERVALO_REINIT_LCD > 0 && (millis() - ultimoReinit) >= INTERVALO_REINIT_LCD) {
    ultimoReinit = millis();
    reiniciarLCD(F("preventivo"));
    return;
  }

  // --- Chequeo de que el expansor I2C sigue vivo ---
  static unsigned long ultimoChequeo = 0;
  if (millis() - ultimoChequeo < INTERVALO_CHEQUEO_LCD) return;
  ultimoChequeo = millis();

  if (lcdResponde()) { fallosI2CSeguidos = 0; return; }

  fallosI2CSeguidos++;
  if (fallosI2CSeguidos >= FALLOS_I2C_PARA_REINICIAR) {
    reiniciarLCD(F("sin respuesta I2C"));
    fallosI2CSeguidos = 0;
  }
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
  // La fila 3 lleva el reloj en formato corto para tener la hora siempre a
  // la vista sin cambiar de pantalla.
  imprimirFila(3, "N:" + String(modoNoche ? "SI" : "NO") +
                  " P:" + String((solicitudPeaton1 || solicitudPeaton2) ? "SI" : "NO") +
                  " " + horaCorta());
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

// --- M5: reloj y estado de la red (el "tablero digital" de la hora) ---
void dibujarPantallaRed() {
  imprimirFila(0, "--RED/HORA--  M5");
  imprimirFila(1, horaFormateada() + " " + origenHora);
  if (!wifiHabilitado)      imprimirFila(2, "WiFi: apagado");
  else if (wifiConectado)   imprimirFila(2, WiFi.localIP().toString());
  else                      imprimirFila(2, "WiFi: sin enlace");
  imprimirFila(3, "POST ok:" + String(enviosOk) + " er:" + String(enviosFallidos));
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
