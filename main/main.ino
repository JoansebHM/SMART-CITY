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

   NOTAS IMPORTANTES DE HARDWARE (leer antes de conectar):
     - Los pines LDR1(13), LDR2(12) y CO2(14) se leen con analogRead().
       En muchos ESP32 estos pines pertenecen al ADC2, que NO se puede
       usar de forma fiable al mismo tiempo que el WiFi esta activo.
       Este sketch NO usa WiFi, asi que no hay conflicto.
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

const int UMBRAL_NOCHE = 800;
const int BRILLO_DIA   = 255;
const int BRILLO_NOCHE = 60;
const int UMBRAL_CO2_ALTO = 2500;

const unsigned long DEBOUNCE_MS = 200;

// Ventana de tiempo (ms) dentro de la cual, si P1 y P2 bajan los dos,
// se considera una pulsacion "combo" en vez de dos pulsaciones sueltas.
const unsigned long VENTANA_COMBO = 150;

// Cuantas pantallas de informacion existen (ver actualizarLCD)
const int NUM_MODOS_PANTALLA = 4;

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

  tiempoInicioFase = millis();
  duracionVerdeCalculada = VERDE_MINIMO;
  aplicarSemaforos();
}

// ============================================================================
// LOOP PRINCIPAL
// ============================================================================
void loop() {
  leerSensoresDetalle();       // lee LDRs, CNYs y cuenta autos (para logica y pantalla)
  leerBotones();                // detecta P1, P2 y combos P1+P2
  actualizarModoNoche();
  leerCO2();
  actualizarMaquinaEstados();
  actualizarLCD();

  delay(50);
}

// ============================================================================
// LECTURA DETALLADA DE SENSORES (se guarda en variables globales para que
// tanto la maquina de estados como la pantalla usen los mismos datos)
// ============================================================================
void leerSensoresDetalle() {
  luz1Actual = analogRead(LDR1);
  luz2Actual = analogRead(LDR2);

  cny1Detecta = cnyDetecta(CNY1);
  cny2Detecta = cnyDetecta(CNY2);
  cny3Detecta = cnyDetecta(CNY3);
  cny4Detecta = cnyDetecta(CNY4);
  cny5Detecta = cnyDetecta(CNY5);
  cny6Detecta = cnyDetecta(CNY6);

  autosCalle1Actual = (cny1Detecta ? 1 : 0) + (cny2Detecta ? 1 : 0) + (cny3Detecta ? 1 : 0);
  autosCalle2Actual = (cny4Detecta ? 1 : 0) + (cny5Detecta ? 1 : 0) + (cny6Detecta ? 1 : 0);
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
  co2Actual = analogRead(CO2);
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
  if (co2Actual >= UMBRAL_CO2_ALTO) {
    maximoPermitido = VERDE_MINIMO + (EXTENSION_POR_AUTO * 2);
  }

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
  int brillo = modoNoche ? BRILLO_NOCHE : BRILLO_DIA;
  analogWrite(pin, brillo);
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

// --- MODO 1: Resumen general de todo el cruce ---
void dibujarPantallaResumen() {
  imprimirFila(0, "C1:" + faseCortaCalle1() + " C2:" + faseCortaCalle2() + "   M1");
  imprimirFila(1, "Resta:" + String(tiempoRestanteFase()) + "s CO2:" + (co2Actual >= UMBRAL_CO2_ALTO ? "ALTO" : "OK"));
  imprimirFila(2, "Autos C1:" + String(autosCalle1Actual) + " C2:" + String(autosCalle2Actual));
  imprimirFila(3, "Noche:" + String(modoNoche ? "SI" : "NO") + " Ped:" + String((solicitudPeaton1 || solicitudPeaton2) ? "SI" : "NO"));
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