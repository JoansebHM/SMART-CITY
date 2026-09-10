/// Maqueta con ESP32
#include <Wire.h>              //Library required for I2C comms (LCD)
#include <LiquidCrystal_I2C.h> 

#define LDR1 13 // LDR Light sensor from traffic light 1 connected in pin A0
#define LDR2 12 // LDR Light sensor from traffic light 2 connected in pin A1
#define CO2 14  // CO2 sensor connected in pin A3
#define P1 1    // Traffic light 1 button connected in pin 1
#define P2 2    // Traffic light 2 button connected in pin 2
#define CNY1 42 // Infrared sensor 1 in traffic light 1 connected in pin 42
#define CNY2 41 // Infrared sensor 2 in traffic light 1 connected in pin 41
#define CNY3 40 // Infrared sensor 3 in traffic light 1 connected in pin 40
#define CNY4 39 // Infrared sensor 4 in traffic light 2 connected in pin 39
#define CNY5 38 // Infrared sensor 5 in traffic light 2 connected in pin 38
#define CNY6 37 // Infrared sensor 6 in traffic light 2 connected in pin 37
#define LR1 5   // Red traffic light 1 connected in pin 5
#define LY1 4   // Yellow traffic light 1 connected in pin 4
#define LG1 6   // Green traffic light 1 connected in pin 6
#define LR2 7   // Red traffic light 2 connected in pin 7
#define LY2 15  // Yellow traffic light 2 connected in pin 15
#define LG2 16  // Green traffic light 2 connected in pin 16

// Library definitions
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Tiempos fijos del ciclo semafórico (UC1.12)
const unsigned long T_VERDE    = 5000; // 10s verde
const unsigned long T_AMARILLO = 3000;  // 3s amarillo
const unsigned long T_ROJO_SEG = 1000;  // margen de seguridad (todo en rojo) entre fases
const unsigned long T_EXTENSION_SEGURIDAD = 2000; 

// Estados posibles del ciclo (vía 1 y vía 2 son complementarias)
enum EstadoCiclo {
  V1_VERDE,
  V1_AMARILLO,
  AMBAS_ROJO_1, 
  V2_VERDE,
  V2_AMARILLO,
  AMBAS_ROJO_2 
};

EstadoCiclo estadoActual = V1_VERDE;
unsigned long tEstado = 0; // marca de tiempo en que empezó el estado actual

// Variables para el parpadeo de aviso (UC2)
bool avisoV1Activo = false;
bool estadoLedAvisoV1 = false;
unsigned long tUltimoParpadeoV1 = 0;
const unsigned long INTERVALO_PARPADEO = 300; // ms entre toggles

// Variable para el forzado de LR1 (UC3)
bool forzadoLR1Activo = false;

// Varaibles (UC4)
unsigned long extraTiempoRojo1 = 0;
bool extensionAplicadaRojo1 = false;

// Variables para el parpadeo de aviso (UC5)
bool avisoV2Activo = false;
bool estadoLedAvisoV2 = false;
unsigned long tUltimoParpadeoV2 = 0;

// Variable para el forzado de LR2 (UC6) 
bool forzadoLR2Activo = false;

// Varaibles (UC7)
unsigned long extraTiempoRojo2 = 0;
bool extensionAplicadaRojo2 = false;

// Variables para modo nocturno (UC8)
bool modoNocturnoV1Activo = false;
bool estadoLedNocturnoV1 = false;
unsigned long tUltimoParpadeoNocturnoV1 = 0;

// Variables para modo nocturno (UC9)
bool modoNocturnoV2Activo = false;
bool estadoLedNocturnoV2 = false;
unsigned long tUltimoParpadeoNocturnoV2 = 0;
const int UMBRAL_NOCHE = 100;

// Variables CO2 (UC10)
const int UMBRAL_CO2_ALTO = 500; // mismo umbral usado en el sketch de nivel medio
bool alertaCO2Activa = false;
unsigned long tUltimaImpresionCO2 = 0;
const unsigned long INTERVALO_IMPRESION_CO2 = 1000;

// Configuración de la fase peatonal fija (UC11/UC12)
const unsigned long T_PEATON = 5000; // tiempo fijo de rojo forzado al presionar

//Variables de estado para P1 (UC11)
bool p1Anterior = HIGH;         // INPUT_PULLUP: reposo = HIGH
bool peatonV1Activo = false;
unsigned long tInicioPeatonV1 = 0;

// Variables de estado para P2 (UC12)
bool p2Anterior = HIGH;
bool peatonV2Activo = false;
unsigned long tInicioPeatonV2 = 0;

void setup() {
  Serial.begin(115200);

  // Entradas
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

  // Salidas
  pinMode(LR1, OUTPUT);
  pinMode(LY1, OUTPUT);
  pinMode(LG1, OUTPUT);
  pinMode(LR2, OUTPUT);
  pinMode(LY2, OUTPUT);
  pinMode(LG2, OUTPUT);

  tEstado = millis();
  aplicarEstado(estadoActual);

  lcd.init();
  lcd.backlight();
}

void loop() {
  cicloSemaforoFijo();     // UC1
  avisoAproximacionV1();   // UC2
  mantenerRojoV1();        // UC3
  extensionSeguridadV1();  // UC4
  avisoAproximacionV2();   // UC5
  mantenerRojoV2();        // UC6
  extensionSeguridadV2();  // UC7
  modoNocturnoV1();        // UC8
  modoNocturnoV2();        // UC9
  alertaCalidadAire();     // UC10
  botonPeatonV1();         // UC11
  botonPeatonV2();         // UC12
}

// ============================================================
// UC1 - Ciclo semafórico fijo (base del sistema)
// Alterna vía 1 y vía 2 en tiempos fijos predefinidos.
// ============================================================
void cicloSemaforoFijo() {
  unsigned long ahora = millis();
  unsigned long transcurrido = ahora - tEstado;

  switch (estadoActual) {
    case V1_VERDE:
      if (transcurrido >= T_VERDE) cambiarEstado(V1_AMARILLO);
      break;
    case V1_AMARILLO:
      if (transcurrido >= T_AMARILLO) cambiarEstado(AMBAS_ROJO_1);
      break;
    case AMBAS_ROJO_1:
      if (transcurrido >= T_ROJO_SEG + extraTiempoRojo1) cambiarEstado(V2_VERDE);
      break;
    case V2_VERDE:
      if (transcurrido >= T_VERDE) cambiarEstado(V2_AMARILLO);
      break;
    case V2_AMARILLO:
      if (transcurrido >= T_AMARILLO) cambiarEstado(AMBAS_ROJO_2);
      break;
    case AMBAS_ROJO_2:
      if (transcurrido >= T_ROJO_SEG + extraTiempoRojo2) cambiarEstado(V1_VERDE);
      break;
  }
}

// ============================================================
// UC2 - CNY1 detecta objeto aproximándose -> aviso parpadeante en LY1
// Acción fija: mientras haya detección, LY1 parpadea.
// Solo actúa si el semáforo está en V1_VERDE, para no pisar
// el estado real de amarillo del ciclo (UC1).
// ============================================================
void avisoAproximacionV1() {
  bool objetoDetectado = digitalRead(CNY1) == LOW; // <-- CNY activo en bajo

  if (objetoDetectado && estadoActual == V1_VERDE) {
    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoV1 >= INTERVALO_PARPADEO) {
      estadoLedAvisoV1 = !estadoLedAvisoV1;
      digitalWrite(LY1, estadoLedAvisoV1 ? HIGH : LOW);
      tUltimoParpadeoV1 = ahora;
    }
    avisoV1Activo = true;
  } else if (avisoV1Activo) {
    avisoV1Activo = false;
    if (estadoActual == V1_VERDE) {
      digitalWrite(LY1, LOW);
    }
  }
}

// ============================================================
// UC3 - CNY2 detecta objeto a media distancia en vía 1
// Acción fija: mientras haya detección, fuerza LR1 encendido
// (sobrescribe LY1/LG1 sin importar la fase del ciclo).
// No altera los tiempos del ciclo (UC1) ni el estado interno,
// solo sobrescribe la salida física mientras dura la detección.
// ============================================================
void mantenerRojoV1() {
  bool objetoDetectado = digitalRead(CNY2) == LOW; // activo en bajo, igual que CNY1

  if (objetoDetectado) {
    digitalWrite(LG1, LOW);
    digitalWrite(LY1, LOW);
    digitalWrite(LR1, HIGH);
    forzadoLR1Activo = true;
  } else if (forzadoLR1Activo) {
    forzadoLR1Activo = false;
    aplicarEstado(estadoActual);
  }
}

// ============================================================
// UC4 - CNY3 detecta vehículo en el cruce durante la transición
// de seguridad (AMBAS_ROJO_1) -> extiende ese rojo +2s fijos, una sola vez.
// Acción fija: siempre la misma extensión, sin importar cuántos
// vehículos o cuánto tiempo lleve detectando.
// ============================================================
void extensionSeguridadV1() {
  bool objetoEnCruce = digitalRead(CNY3) == LOW; // activo en bajo

  if (estadoActual == AMBAS_ROJO_1 && objetoEnCruce && !extensionAplicadaRojo1) {
    extraTiempoRojo1 += T_EXTENSION_SEGURIDAD;
    extensionAplicadaRojo1 = true;
    Serial.println(">> CNY3 detectó en el cruce -> extendiendo +2s (t=" + String(millis()) + ")");
  }
}

// ============================================================
// UC5 - CNY4 detecta objeto aproximándose en vía 2 -> aviso parpadeante en LY2
// Acción fija: mientras haya detección, LY2 parpadea.
// Solo actúa si el semáforo está en V2_VERDE.
// ============================================================
void avisoAproximacionV2() {
  bool objetoDetectado = digitalRead(CNY4) == LOW; // activo en bajo

  if (objetoDetectado && estadoActual == V2_VERDE) {
    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoV2 >= INTERVALO_PARPADEO) {
      estadoLedAvisoV2 = !estadoLedAvisoV2;
      digitalWrite(LY2, estadoLedAvisoV2 ? HIGH : LOW);
      tUltimoParpadeoV2 = ahora;
    }
    avisoV2Activo = true;
  } else if (avisoV2Activo) {
    avisoV2Activo = false;
    if (estadoActual == V2_VERDE) {
      digitalWrite(LY2, LOW);
    }
  }
}

// ============================================================
// UC6 - CNY5 detecta objeto a media distancia en vía 2
// Acción fija: mientras haya detección, fuerza
// LR2 encendido (sobrescribe LY2/LG2 sin importar la fase del ciclo).
// ============================================================
void mantenerRojoV2() {
  bool objetoDetectado = digitalRead(CNY5) == LOW; // activo en bajo

  if (objetoDetectado) {
    digitalWrite(LG2, LOW);
    digitalWrite(LY2, LOW);
    digitalWrite(LR2, HIGH);
    forzadoLR2Activo = true;
  } else if (forzadoLR2Activo) {
    forzadoLR2Activo = false;
    aplicarEstado(estadoActual);
  }
}

// ============================================================
// UC7 - CNY6 detecta vehículo en el cruce durante la transición
// de seguridad (AMBAS_ROJO_2) -> extiende ese rojo +2s fijos, una sola vez.
// ============================================================
void extensionSeguridadV2() {
  bool objetoEnCruce = digitalRead(CNY6) == LOW; // activo en bajo

  if (estadoActual == AMBAS_ROJO_2 && objetoEnCruce && !extensionAplicadaRojo2) {
    extraTiempoRojo2 += T_EXTENSION_SEGURIDAD;
    extensionAplicadaRojo2 = true;
  }
}

void cambiarEstado(EstadoCiclo nuevo) {
  estadoActual = nuevo;
  tEstado = millis();
  aplicarEstado(nuevo);

  if (nuevo == AMBAS_ROJO_1) {
    extraTiempoRojo1 = 0;
    extensionAplicadaRojo1 = false;
  }

  if (nuevo == AMBAS_ROJO_2) {
    extraTiempoRojo2 = 0;
    extensionAplicadaRojo2 = false;
  }
}

// ============================================================
// UC8 - LDR1 detecta poca luz ambiente -> LY1 parpadeo intermitente
// (modo nocturno). Acción fija por umbral: mientras la luz esté baja,
// el semáforo 1 se comporta como amarillo intermitente, sobrescribiendo
// el ciclo normal (UC1) sin importar en qué fase esté.
// ============================================================
void modoNocturnoV1() {
  bool pocaLuz = analogRead(LDR1) < UMBRAL_NOCHE;
  if (pocaLuz) {
    digitalWrite(LR1, LOW);
    digitalWrite(LG1, LOW);

    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoNocturnoV1 >= INTERVALO_PARPADEO) {
      estadoLedNocturnoV1 = !estadoLedNocturnoV1;
      digitalWrite(LY1, estadoLedNocturnoV1 ? HIGH : LOW);
      tUltimoParpadeoNocturnoV1 = ahora;
    }
    modoNocturnoV1Activo = true;
  } else if (modoNocturnoV1Activo) {
    modoNocturnoV1Activo = false;
    aplicarEstado(estadoActual);
  }
}

// ============================================================
// UC9- LDR2 detecta poca luz ambiente -> LY2 parpadeo intermitente
// ============================================================
void modoNocturnoV2() {
  bool pocaLuz = analogRead(LDR2) < UMBRAL_NOCHE;
 
  if (pocaLuz) {
    digitalWrite(LR2, LOW);
    digitalWrite(LG2, LOW);

    unsigned long ahora = millis();
    if (ahora - tUltimoParpadeoNocturnoV2 >= INTERVALO_PARPADEO) {
      estadoLedNocturnoV2 = !estadoLedNocturnoV2;
      digitalWrite(LY2, estadoLedNocturnoV2 ? HIGH : LOW);
      tUltimoParpadeoNocturnoV2 = ahora;
    }
    modoNocturnoV2Activo = true;
  } else if (modoNocturnoV2Activo) {
    modoNocturnoV2Activo = false;
    aplicarEstado(estadoActual);
  }
}

// ============================================================
// UC10 - CO2 por encima de un umbral fijo (en ppm) -> Display
// muestra "Aire contaminado". Comparación instantánea, sin tendencia.
// ============================================================
void alertaCalidadAire() {
  unsigned long ahora = millis();
  if (ahora - tUltimaImpresionCO2 < INTERVALO_IMPRESION_CO2) return;
  tUltimaImpresionCO2 = ahora;
  
  int lecturaCO2 = analogRead(CO2);
  Serial.println(lecturaCO2); // 0-4095, sin calibrar a ppm
  bool aireContaminado = lecturaCO2 > UMBRAL_CO2_ALTO;

  // --- Línea 1: monitoreo permanente, se actualiza siempre ---
  lcd.setCursor(0, 0);
  lcd.print("CO2: ");
  lcd.print(lecturaCO2);
  lcd.print("    "); // espacios para borrar digitos sobrantes de una lectura anterior mas larga

  // --- Línea 2: alerta con el numero, solo cuando supera el umbral ---
  if (aireContaminado) {
    lcd.setCursor(0, 1);
    lcd.print("ALERTA! CO2:");
    lcd.print(lecturaCO2);
    lcd.print("   ");
    alertaCO2Activa = true;
  } else if (alertaCO2Activa) {
    lcd.setCursor(0, 1);
    lcd.print("                "); // 16 espacios: borra la linea de alerta
    alertaCO2Activa = false;
  }
}

// ============================================================
// UC11 - P1 presionado -> fuerza LR1 por un tiempo fijo (paso peatonal)
// Acción fija: siempre la misma duración, sin encolar ni esperar
// un momento "óptimo" del ciclo (eso sería nivel medio).
// Detecta flanco de bajada (botón con INPUT_PULLUP: reposo=HIGH, presionado=LOW).
// ============================================================
void botonPeatonV1() {
  bool p1Actual = digitalRead(P1);

  // Flanco de bajada: se acaba de presionar
  if (p1Anterior == HIGH && p1Actual == LOW && !peatonV1Activo) {
    peatonV1Activo = true;
    tInicioPeatonV1 = millis();
  }
  p1Anterior = p1Actual;

  if (peatonV1Activo) {
    digitalWrite(LG1, LOW);
    digitalWrite(LY1, LOW);
    digitalWrite(LR1, HIGH);

    if (millis() - tInicioPeatonV1 >= T_PEATON) {
      peatonV1Activo = false;
      aplicarEstado(estadoActual); // libera y vuelve al ciclo real
    }
  }
}

// ============================================================
// UC12 - P2 presionado -> fuerza LR2 por un tiempo fijo
// ============================================================
void botonPeatonV2() {
  bool p2Actual = digitalRead(P2);

  if (p2Anterior == HIGH && p2Actual == LOW && !peatonV2Activo) {
    peatonV2Activo = true;
    tInicioPeatonV2 = millis();
  }
  p2Anterior = p2Actual;

  if (peatonV2Activo) {
    digitalWrite(LG2, LOW);
    digitalWrite(LY2, LOW);
    digitalWrite(LR2, HIGH);

    if (millis() - tInicioPeatonV2 >= T_PEATON) {
      peatonV2Activo = false;
      aplicarEstado(estadoActual);
    }
  }
}

void aplicarEstado(EstadoCiclo e) {
  // Apaga todo primero para evitar solapes
  digitalWrite(LR1, LOW); digitalWrite(LY1, LOW); digitalWrite(LG1, LOW);
  digitalWrite(LR2, LOW); digitalWrite(LY2, LOW); digitalWrite(LG2, LOW);

  switch (e) {
    case V1_VERDE:
      digitalWrite(LG1, HIGH);
      digitalWrite(LR2, HIGH);
      break;
    case V1_AMARILLO:
      digitalWrite(LY1, HIGH);
      digitalWrite(LR2, HIGH);
      break;
    case AMBAS_ROJO_1:
      digitalWrite(LR1, HIGH);
      digitalWrite(LR2, HIGH);
      break;
    case V2_VERDE:
      digitalWrite(LG2, HIGH);
      digitalWrite(LR1, HIGH);
      break;
    case V2_AMARILLO:
      digitalWrite(LY2, HIGH);
      digitalWrite(LR1, HIGH);
      break;
    case AMBAS_ROJO_2:
      digitalWrite(LR1, HIGH);
      digitalWrite(LR2, HIGH);
      break;
  }
}