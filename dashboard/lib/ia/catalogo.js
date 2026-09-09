/**
 * CATALOGO DE COMANDOS
 * ============================================================================
 * Este archivo es el espejo legible de `SmartCity_Maqueta/Comandos.ino`.
 * Es lo UNICO que lee el modelo: nunca ve el codigo fuente de la maqueta.
 *
 * Sirve para tres cosas a la vez:
 *   1. Construir el prompt (solo con las categorias relevantes -> menos
 *      tokens, menos latencia).
 *   2. Validar cada comando propuesto antes de mandarlo por serial.
 *   3. Documentar el vocabulario en un solo lugar.
 *
 * `npm run verificar-catalogo` comprueba que no se desincronice del firmware.
 *
 * -------------------------------------------------------------------------
 * Campos de cada comando:
 *   nombre    : primera palabra del comando
 *   sintaxis  : forma canonica, para el prompt
 *   que       : una linea explicando que hace
 *   args      : validacion posicional (ver validador.js)
 *                 { tipo: 'enum',   valores: [...] }
 *                 { tipo: 'entero', min, max }
 *                 { tipo: 'texto' }             (libre, hasta fin de linea)
 *                 opcional: true                (puede faltar)
 *                 rangoPorArg0: { clave: [min, max] }  (rango que depende del
 *                                                       argumento anterior)
 *   ojo       : (opcional) la trampa del comando. Se renderiza en el prompt.
 *               Va aqui todo lo que el modelo no puede deducir de la sintaxis:
 *               efectos que quedan FIJOS, precedencias entre comandos y cosas
 *               que el comando NO hace aunque su nombre lo sugiera.
 *   ejemplos  : 1-2 ejemplos reales
 * -------------------------------------------------------------------------
 */

// Palabras que disparan cada categoria. El recuperador las usa para decidir
// que parte del catalogo entra al prompt.
export const CATEGORIAS = {
  emergencia: {
    titulo: 'Emergencias y prioridad de paso',
    claves: ['ambulancia', 'emergencia', 'bombero', 'policia', 'urgencia', 'urgente',
             'prioridad', 'paso', 'despejar', 'via libre', 'panico', 'peligro',
             'accidente', 'choque', 'apagar todo', 'corte', 'evacuar', 'patrulla',
             'sirena', 'hospital', 'camilla', 'rescate']
  },
  semaforo: {
    titulo: 'Control del ciclo y de las fases',
    claves: ['semaforo', 'semaforos', 'fase', 'verde', 'rojo', 'amarillo', 'ciclo',
             'automatico', 'manual', 'pausa', 'pausar', 'congelar', 'cambiar',
             'siguiente', 'normalidad', 'normal', 'reanudar']
  },
  luces: {
    titulo: 'Luces, brillo y efectos',
    claves: ['luz', 'luces', 'led', 'leds', 'brillo', 'intensidad', 'atenuar',
             'subir', 'bajar', 'parpadeo', 'parpadear', 'intermitente', 'titilar',
             'fade', 'desvanecer', 'prender', 'prende', 'encender', 'enciende',
             'apagar', 'apaga', 'colores', 'color', 'bombillo', 'oscuro',
             'claro', 'fuerte', 'suave', 'tenue', 'atenua']
  },
  sensores: {
    titulo: 'Sensores de vehiculos y trafico simulado',
    claves: ['carro', 'carros', 'auto', 'autos', 'vehiculo', 'vehiculos', 'trafico',
             'cola', 'fila', 'congestion', 'trancon', 'cny', 'sensor', 'sensores',
             'flujo', 'hora pico', 'horapico', 'atasco', 'embotellamiento', 'ruido']
  },
  ambiente: {
    titulo: 'Luz ambiente, dia/noche y calidad del aire',
    claves: ['noche', 'dia', 'oscuridad', 'oscurecer', 'amanecer', 'anochecer',
             'ldr', 'luz ambiente', 'co2', 'aire', 'contaminacion', 'humo',
             'polucion', 'calidad', 'smog']
  },
  peatones: {
    titulo: 'Botones peatonales',
    claves: ['peaton', 'peatones', 'persona', 'gente', 'cruzar', 'cruce', 'boton',
             'botones', 'pulsador', 'caminar', 'anciano', 'nino']
  },
  pantalla: {
    titulo: 'Pantalla LCD de la maqueta',
    claves: ['pantalla', 'lcd', 'mostrar', 'display', 'mensaje', 'texto',
             'escribir', 'anunciar', 'aviso', 'letrero']
  },
  tiempos: {
    titulo: 'Duraciones y umbrales del ciclo',
    claves: ['tiempo', 'tiempos', 'duracion', 'segundos', 'mas rapido', 'mas lento',
             'rapido', 'lento', 'umbral', 'acortar', 'alargar', 'configurar', 'ajustar']
  },
  escenarios: {
    titulo: 'Escenarios completos preconfigurados',
    claves: ['escenario', 'simula', 'simular', 'demostracion', 'demo', 'ejemplo',
             'situacion', 'caso', 'madrugada', 'hora pico',
             // Un escenario ya resuelve varias peticiones tipicas. Si estas
             // palabras no traen la categoria, el modelo arma la secuencia a
             // mano y se equivoca (ver 'parpadeo': no enciende LEDs).
             'intermitente', 'titilar', 'titilando', 'parpadeante', 'nocturno',
             'trancon', 'congestion', 'contaminacion', 'vacio', 'desierta']
  },
  programacion: {
    titulo: 'Acciones diferidas y secuencias',
    claves: ['despues', 'luego', 'dentro de', 'en unos', 'programar', 'agendar',
             'secuencia', 'temporizador', 'espera', 'esperar', 'cancelar',
             'mas tarde', 'minutos', 'y luego', 'segundos', 'en 10', 'en 20',
             'en 30', 'en 60', 'al rato', 'pasados']
  },
  sistema: {
    titulo: 'Estado, snapshots, watchdog y seguridad',
    claves: ['estado', 'reset', 'reiniciar', 'guardar', 'restaurar', 'volver',
             'deshacer', 'watchdog', 'seguro', 'interlock', 'como esta',
             'normalidad', 'snapshot', 'antes']
  },
  diagnostico: {
    titulo: 'Pruebas de hardware',
    claves: ['probar', 'prueba', 'test', 'diagnostico', 'verificar', 'revisar',
             'funciona', 'chequear']
  }
};

export const COMANDOS = [
  // ======================= EMERGENCIA =======================
  {
    nombre: 'prio', categoria: 'emergencia',
    sintaxis: 'prio <c1|c2> <segundos|inf>  |  prio off',
    que: 'Da paso prioritario a una calle: la sostiene en verde y deja la otra en rojo. Guarda el estado previo automaticamente. Con "inf" dura hasta que se cancele.',
    ojo: 'Aqui los segundos SI son segundos (no ms). Pisa el slot 0 de "guardar". "prio off" da error si no hay ninguna prioridad activa.',
    args: [
      { tipo: 'enum', valores: ['c1', 'c2', 'off', 'fin'] },
      { tipo: 'texto', opcional: true, patron: /^(\d+|inf|indefinido)$/ }
    ],
    ejemplos: ['prio c1 60', 'prio c2 inf', 'prio off']
  },
  {
    nombre: 'panico', categoria: 'emergencia',
    sintaxis: 'panico',
    que: 'Estado seguro: las dos calles en rojo y el ciclo detenido.',
    ojo: 'Deja el semaforo en MANUAL y no se sale solo. Para reanudar hace falta "sem auto" o "reset".',
    args: [], ejemplos: ['panico']
  },
  {
    nombre: 'apagar', categoria: 'emergencia',
    sintaxis: 'apagar',
    que: 'Apaga los seis LEDs (simula un corte de energia en el cruce). Equivale a "leds off" y ademas quita los parpadeos.',
    ojo: 'Los LEDs quedan apagados de forma FIJA aunque el ciclo siga corriendo. Se sale con "leds auto" o "reset".',
    args: [], ejemplos: ['apagar']
  },

  // ======================= SEMAFORO =======================
  {
    nombre: 'sem', categoria: 'semaforo',
    sintaxis: 'sem <auto|manual>',
    que: 'auto = el ciclo corre solo; manual = la fase queda congelada y solo se mueve con "fase". "sem auto" tambien cancela cualquier prioridad.',
    ojo: 'Solo congela el AVANCE de fase. No toca los LEDs: los que esten forzados con "led"/"leds" siguen forzados, y los que no, siguen mostrando la fase congelada.',
    args: [{ tipo: 'enum', valores: ['auto', 'manual', 'pausa'] }],
    ejemplos: ['sem auto', 'sem manual']
  },
  {
    nombre: 'fase', categoria: 'semaforo',
    sintaxis: 'fase <v1|a1|r1|v2|a2|r2|sig>',
    que: 'Salta a una fase concreta. v=verde, a=amarillo, r=todo rojo; el numero es la calle. "sig" avanza a la siguiente.',
    ojo: 'En modo automatico el ciclo lo sobreescribe a los pocos segundos. Si quieres que la fase se quede, manda "sem manual" ANTES.',
    args: [{ tipo: 'enum', valores: ['v1', 'a1', 'r1', 'v2', 'a2', 'r2', 'sig', 'next'] }],
    ejemplos: ['sem manual', 'fase v1', 'fase sig']
  },

  // ======================= LUCES =======================
  {
    nombre: 'brillo', categoria: 'luces',
    sintaxis: 'brillo <0-255|N%>',
    que: 'Atenuacion global de todos los LEDs. 255 o 100% es brillo pleno. Es lo que se usa para "sube/baja la intensidad".',
    ojo: 'Multiplica todo lo demas: con "brillo 0" la maqueta se ve apagada aunque el ciclo siga corriendo. Para atenuar sin apagar, no bajes de 10%.',
    args: [{ tipo: 'texto', patron: /^(\d{1,3}%?)$/ }],
    ejemplos: ['brillo 40%', 'brillo 255']
  },
  {
    nombre: 'parpadeo', categoria: 'luces',
    sintaxis: 'parpadeo <lr1|ly1|lg1|lr2|ly2|lg2|sem1|sem2|all> <ms|off>',
    que: 'Alterna un LED entre el brillo que ya tiene y apagado, con ese periodo en ms.',
    ojo: 'NO enciende nada por si solo: solo apaga a ratos. Sobre un LED apagado no se ve nada. Enciendelo primero con "led <x> on" (o deja que el ciclo lo encienda). Para amarillo intermitente en ambas calles usa "escenario intermitente".',
    args: [
      { tipo: 'enum', valores: ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2', 'sem1', 'sem2', 'all', 'todos'] },
      { tipo: 'texto', patron: /^(\d+|off)$/ }
    ],
    ejemplos: ['led ly1 on', 'parpadeo ly1 500', 'parpadeo sem1 off']
  },
  {
    nombre: 'fade', categoria: 'luces',
    sintaxis: 'fade <lr1..lg2> <0-255> <ms>',
    que: 'Rampa suave del brillo de un LED hasta un valor, en el tiempo dado.',
    ojo: 'Al terminar el LED queda FIJO en ese valor y ya no obedece al semaforo. Devuelvelo con "led <x> auto".',
    args: [
      { tipo: 'enum', valores: ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'] },
      { tipo: 'entero', min: 0, max: 255 },
      { tipo: 'entero', min: 50, max: 60000 }
    ],
    ejemplos: ['fade lg1 0 2000']
  },
  {
    nombre: 'led', categoria: 'luces',
    sintaxis: 'led <lr1|ly1|lg1|lr2|ly2|lg2> <on|off|0-255|auto>',
    que: 'Controla un LED concreto. r=rojo, y=amarillo, g=verde; el numero es el semaforo.',
    ojo: 'on/off/numero desconectan ese LED del ciclo de forma PERMANENTE: seguira igual aunque cambie la fase. "led <x> auto" lo devuelve al semaforo. Los dos verdes (lg1 y lg2) NO pueden estar encendidos a la vez: el interlock apaga uno sin avisar. Si de verdad los quieres juntos, manda "seguro off" antes y "seguro on" al terminar.',
    args: [
      { tipo: 'enum', valores: ['lr1', 'ly1', 'lg1', 'lr2', 'ly2', 'lg2'] },
      { tipo: 'texto', patron: /^(on|off|auto|\d{1,3})$/ }
    ],
    ejemplos: ['led lg1 on', 'led lr2 auto']
  },
  {
    nombre: 'leds', categoria: 'luces',
    sintaxis: 'leds <auto|on|off>',
    que: 'Los seis LEDs a la vez. "on" enciende TODOS los colores al tiempo (util para pruebas visuales), aunque el interlock siga tapando uno de los dos verdes.',
    ojo: '"leds off" y "leds on" congelan los seis LEDs: el semaforo deja de verse aunque el ciclo siga corriendo por dentro. Termina siempre con "leds auto" (o "reset"). Si despues de un "leds off" quieres encender algo, hay que hacerlo LED por LED con "led <x> on".',
    args: [{ tipo: 'enum', valores: ['auto', 'on', 'off'] }],
    ejemplos: ['leds on', 'leds auto']
  },

  // ======================= SENSORES =======================
  {
    nombre: 'cny', categoria: 'sensores',
    sintaxis: 'cny <1-6|c1|c2|all> <on|off|auto>',
    que: 'Simula los sensores de vehiculos. on = hay un carro encima. c1 son los sensores 1-3, c2 los 4-6.',
    ojo: 'on/off dejan el sensor CLAVADO en ese valor: la cola nunca se vacia sola y el verde se extiende indefinidamente. Usa "cny ... auto" para soltarlo. Para trafico que va y viene, mejor "flujo".',
    args: [
      { tipo: 'texto', patron: /^([1-6]|c1|c2|all|todos)$/ },
      { tipo: 'enum', valores: ['on', 'off', 'auto', '0', '1'] }
    ],
    ejemplos: ['cny c1 on', 'cny 4 off', 'cny all auto']
  },
  {
    nombre: 'flujo', categoria: 'sensores',
    sintaxis: 'flujo <c1|c2> <autos por minuto> | flujo off',
    que: 'Trafico dinamico: hace aparecer y desaparecer carros solos a la tasa indicada. Mas realista que dejar sensores fijos.',
    ojo: 'Manda sobre "cny": mientras haya flujo activo en una calle, el va prendiendo y apagando sus sensores y pisa lo que fijes con "cny". No los combines en la misma calle.',
    args: [
      { tipo: 'enum', valores: ['c1', 'c2', 'off'] },
      { tipo: 'entero', min: 0, max: 120, opcional: true }
    ],
    ejemplos: ['flujo c1 30', 'flujo off']
  },
  {
    nombre: 'ruido', categoria: 'sensores',
    sintaxis: 'ruido <on|off>',
    que: 'Anade pequenas fluctuaciones a las lecturas para que se vean mas realistas.',
    args: [{ tipo: 'enum', valores: ['on', 'off'] }],
    ejemplos: ['ruido on']
  },

  // ======================= AMBIENTE =======================
  {
    nombre: 'ldr', categoria: 'ambiente',
    sintaxis: 'ldr <1|2|all> <0-4095|auto>',
    que: 'Fuerza el valor de los sensores de luz. Valores bajos = oscuro.',
    ojo: 'Solo cambia el dia/noche si "noche" esta en auto: un "noche on|off" previo gana siempre. Para "hazte de noche" es mas directo "escenario noche" o "noche on".',
    args: [
      { tipo: 'enum', valores: ['1', '2', 'all', 'todos'] },
      { tipo: 'texto', patron: /^(auto|\d{1,4})$/ }
    ],
    ejemplos: ['ldr all 100', 'ldr 1 auto']
  },
  {
    nombre: 'co2', categoria: 'ambiente',
    sintaxis: 'co2 <0-4095|alto|bajo|auto>',
    que: 'Simula la calidad del aire. Al llegar a umbralco2 el sistema entra en EMERGENCIA: congela el ciclo, fuerza LG2+LR1 para evacuar por la Calle 2, el LCD muestra "CO2 CRITICO" y el LED RGB de la placa se pone rojo. Por debajo del umbral solo recorta el verde maximo.',
    ojo: 'La emergencia manda sobre todo: mientras dure, ni "led", ni "fase", ni "parpadeo", ni la noche profunda tocan los semaforos. Se sale bajando el CO2 ("co2 bajo" o "co2 auto").',
    args: [{ tipo: 'texto', patron: /^(alto|bajo|auto|\d{1,4})$/ }],
    ejemplos: ['co2 alto', 'co2 auto']
  },
  {
    nombre: 'noche', categoria: 'ambiente',
    sintaxis: 'noche <on|off|auto>',
    que: 'Fuerza modo noche (LEDs atenuados) o modo dia, o lo deja segun los LDR.',
    ojo: 'Solo baja el brillo de los LEDs (brillonoche); NO pone los semaforos en amarillo intermitente. Eso es "escenario intermitente".',
    args: [{ tipo: 'enum', valores: ['on', 'off', 'auto'] }],
    ejemplos: ['noche on', 'noche auto']
  },
  {
    nombre: 'hora', categoria: 'ambiente',
    sintaxis: 'hora <0-23|off>',
    que: 'Le dice al sistema que hora es (la placa no tiene reloj). Si la hora cae en la franja 23h-4h Y los dos LDR leen por debajo de umbralnoche, arranca la NOCHE PROFUNDA: LY1 y LR2 parpadean juntos y los demas LEDs se apagan.',
    ojo: 'Por si sola no hace nada: hacen falta las DOS condiciones. Con la hora puesta pero con luz, el firmware solo avisa por consola. Para verlo de una usa "escenario madrugada". "hora off" olvida la hora.',
    args: [{ tipo: 'texto', patron: /^(off|auto|limpiar|\d{1,2})$/ }],
    ejemplos: ['hora 2', 'hora 14', 'hora off']
  },

  // ======================= PEATONES =======================
  {
    nombre: 'peaton', categoria: 'peatones',
    sintaxis: 'peaton <c1|c2|limpiar>',
    que: 'Registra una solicitud de cruce peatonal, que acorta el verde de esa calle al minimo.',
    ojo: 'No cambia nada al instante ni enciende ninguna luz de peaton: solo recorta el verde en curso. Si esa calle ya esta en rojo no se nota hasta su proximo verde.',
    args: [{ tipo: 'enum', valores: ['c1', 'c2', 'limpiar', 'cancelar'] }],
    ejemplos: ['peaton c1', 'peaton limpiar']
  },
  {
    nombre: 'p1', categoria: 'peatones',
    sintaxis: 'p1',
    que: 'Atajo: pulsa el boton peatonal de la Calle 1.',
    args: [], ejemplos: ['p1']
  },
  {
    nombre: 'p2', categoria: 'peatones',
    sintaxis: 'p2',
    que: 'Atajo: pulsa el boton peatonal de la Calle 2.',
    args: [], ejemplos: ['p2']
  },
  {
    nombre: 'botones', categoria: 'peatones',
    sintaxis: 'botones <on|off>',
    que: 'Habilita o ignora los pulsadores fisicos de la maqueta.',
    args: [{ tipo: 'enum', valores: ['on', 'off'] }],
    ejemplos: ['botones off']
  },

  // ======================= PANTALLA =======================
  {
    nombre: 'lcd', categoria: 'pantalla',
    sintaxis: 'lcd <on|off|limpiar> | lcd texto <mensaje>',
    que: 'Enciende/apaga la pantalla, o muestra un mensaje libre que tapa las pantallas normales hasta "lcd limpiar". Usalo para anunciar emergencias.',
    ojo: 'El mensaje se queda fijo tapando la informacion del cruce; acuerdate de "lcd limpiar" cuando pase el evento. Sin acentos ni ";" (el ";" separa pasos en "secuencia"). Caben ~16 caracteres por linea.',
    args: [
      { tipo: 'enum', valores: ['on', 'off', 'limpiar', 'clear', 'texto', 'msg'] },
      { tipo: 'texto', opcional: true, restoDeLinea: true }
    ],
    ejemplos: ['lcd texto AMBULANCIA CALLE 1', 'lcd limpiar']
  },
  {
    nombre: 'pantalla', categoria: 'pantalla',
    sintaxis: 'pantalla <1-4>',
    que: 'Cambia a una de las 4 pantallas de informacion del LCD.',
    args: [{ tipo: 'entero', min: 1, max: 4 }],
    ejemplos: ['pantalla 2']
  },
  {
    nombre: 'combo', categoria: 'pantalla',
    sintaxis: 'combo',
    que: 'Avanza a la siguiente pantalla del LCD.',
    args: [], ejemplos: ['combo']
  },

  // ======================= TIEMPOS =======================
  {
    nombre: 'set', categoria: 'tiempos',
    sintaxis: 'set <parametro> <valor>',
    que: 'Cambia un parametro en caliente. En MILISEGUNDOS: verdemin, verdemax, extension (ms extra por auto), amarillo, todorojo. En cuentas de ADC 0-4095: umbralnoche, umbralco2. En PWM 0-255: brillodia, brillonoche. Booleano 0/1: cnybajo.',
    ojo: 'Cada parametro tiene su propia unidad; no todos son milisegundos. verdemin nunca debe quedar por encima de verdemax.',
    args: [
      { tipo: 'enum', valores: ['verdemin', 'verdemax', 'extension', 'amarillo', 'todorojo',
                                'umbralnoche', 'umbralco2', 'brillodia', 'brillonoche', 'cnybajo'] },
      {
        tipo: 'entero', min: 0, max: 60000,
        // Cada parametro vive en su propia unidad: sin esto el validador dejaba
        // pasar cosas como "set brillodia 60000" (PWM maximo real: 255).
        rangoPorArg0: {
          verdemin: [500, 60000], verdemax: [500, 60000], extension: [0, 10000],
          amarillo: [300, 10000], todorojo: [0, 10000],
          umbralnoche: [0, 4095], umbralco2: [0, 4095],
          brillodia: [0, 255], brillonoche: [0, 255],
          cnybajo: [0, 1]
        }
      }
    ],
    ejemplos: ['set verdemin 3000', 'set amarillo 1500', 'set brillonoche 60']
  },

  // ======================= ESCENARIOS =======================
  {
    nombre: 'escenario', categoria: 'escenarios',
    sintaxis: 'escenario <nombre>',
    que: 'Aplica un escenario completo de una sola vez. trafico1/trafico2 = cola fija en esa calle; noche/dia = luz ambiente; madrugada = hora 2h + oscuridad en las dos vias, que dispara la noche profunda (LY1+LR2 intermitentes); contaminacion = CO2 alto (dispara la emergencia por CO2) y cola en ambas; vacio = sin autos y aire limpio; intermitente = los dos amarillos parpadeando (deja el semaforo en manual); horapico = trafico continuo y variable en ambas.',
    ojo: 'PREFIERELO a armar la misma situacion con varios comandos sueltos: el escenario ya deja el estado consistente. Se deshace con "reset".',
    args: [{ tipo: 'enum', valores: ['trafico1', 'trafico2', 'noche', 'dia', 'madrugada',
                                     'contaminacion', 'vacio', 'intermitente', 'horapico'] }],
    ejemplos: ['escenario horapico', 'escenario intermitente']
  },

  // ======================= PROGRAMACION =======================
  {
    nombre: 'en', categoria: 'programacion',
    sintaxis: 'en <milisegundos> <comando>',
    que: 'Ejecuta otro comando dentro de N MILISEGUNDOS. Sirve para "haz esto y en 30 segundos vuelve a la normalidad".',
    ojo: 'Milisegundos, no segundos: 30 segundos son "en 30000". Maximo 600000 (10 min). No se puede anidar otro "en" ni una "secuencia".',
    args: [
      { tipo: 'entero', min: 0, max: 600000 },
      { tipo: 'texto', restoDeLinea: true }
    ],
    ejemplos: ['en 30000 sem auto', 'en 5000 prio off']
  },
  {
    nombre: 'secuencia', categoria: 'programacion',
    sintaxis: 'secuencia <cmd> ; espera <ms> ; <cmd> ; ...',
    que: 'Encadena comandos separados por ";". "espera <ms>" corre el reloj sin ejecutar nada. El primero se ejecuta de inmediato.',
    ojo: 'El UNICO separador es ";" y cada comando va en su propio paso: "led lg1 on ; led lg2 on". NUNCA separes con comas ("led lg1 on , led lg2 on" se rechaza entero). La placa solo guarda 8 comandos EN ESPERA; los pasos anteriores a la primera "espera" corren de inmediato y no gastan ranura, y los "espera" tampoco. Cada "en" que mandes aparte tambien ocupa una de las 8. No se puede anidar "en" ni otra "secuencia", y no metas un "lcd texto" que lleve ";" dentro.',
    args: [{ tipo: 'texto', restoDeLinea: true }],
    ejemplos: [
      'secuencia leds off ; espera 500 ; leds auto',
      'secuencia leds off ; led lg1 on ; led lg2 on ; espera 1600 ; leds off ; led ly1 on ; led ly2 on ; espera 1600 ; reset'
    ]
  },
  {
    nombre: 'cancelar', categoria: 'programacion',
    sintaxis: 'cancelar',
    que: 'Cancela todos los comandos programados que aun no se han ejecutado.',
    args: [], ejemplos: ['cancelar']
  },

  // ======================= SISTEMA =======================
  {
    nombre: 'estado', categoria: 'sistema',
    sintaxis: 'estado',
    que: 'Imprime el estado completo del sistema en texto.',
    args: [], ejemplos: ['estado']
  },
  {
    nombre: 'json', categoria: 'sistema',
    sintaxis: 'json',
    que: 'Imprime un snapshot del estado en JSON.',
    args: [], ejemplos: ['json']
  },
  {
    nombre: 'reset', categoria: 'sistema',
    sintaxis: 'reset',
    que: 'Devuelve TODO a automatico: quita simulaciones, prioridades, efectos de luz, flujo y mensajes del LCD.',
    ojo: 'Es la salida segura de cualquier estado raro. Usalo cuando pidan "vuelve a la normalidad" en vez de deshacer comando por comando. No borra los slots guardados ni los comandos programados (para eso, "cancelar").',
    args: [], ejemplos: ['reset']
  },
  {
    nombre: 'guardar', categoria: 'sistema',
    sintaxis: 'guardar <0-2>',
    que: 'Guarda el estado completo en un slot para poder volver a el despues.',
    ojo: 'No uses el slot 0: "prio" lo pisa cada vez que arranca una prioridad. Para guardar a mano, usa 1 o 2.',
    args: [{ tipo: 'entero', min: 0, max: 2, opcional: true }],
    ejemplos: ['guardar 1']
  },
  {
    nombre: 'restaurar', categoria: 'sistema',
    sintaxis: 'restaurar <0-2>',
    que: 'Restaura exactamente el estado guardado en ese slot.',
    ojo: 'Da error si el slot esta vacio. Si nadie guardo nada antes, lo correcto para volver a la normalidad es "reset".',
    args: [{ tipo: 'entero', min: 0, max: 2, opcional: true }],
    ejemplos: ['restaurar 1']
  },
  {
    nombre: 'watchdog', categoria: 'sistema',
    sintaxis: 'watchdog <segundos|off>',
    que: 'Red de seguridad: si nadie lo renueva antes de que venza, el sistema se restaura solo. Evita que la maqueta quede trabada.',
    ojo: 'Aqui son SEGUNDOS. "prio" ya arma uno solo, no hace falta anadirlo. Al vencer hace un reset completo: no lo uses para dejar efectos que deban durar.',
    args: [{ tipo: 'texto', patron: /^(off|\d+)$/ }],
    ejemplos: ['watchdog 120', 'watchdog off']
  },
  {
    nombre: 'seguro', categoria: 'sistema',
    sintaxis: 'seguro <on|off>',
    que: 'Interlock. Con "on" (por defecto) el firmware NUNCA deja las dos calles en verde a la vez: si las dos lo piden, apaga una sin avisar.',
    ojo: 'Es lo que hace falta apagar para juegos de luces que enciendan lg1 y lg2 juntos (secuencias de colores, pruebas). Manda "seguro off" antes y devuelvelo con "seguro on" o "reset" al terminar: no lo dejes apagado.',
    args: [{ tipo: 'enum', valores: ['on', 'off'] }],
    ejemplos: ['seguro on']
  },
  {
    nombre: 'mon', categoria: 'sistema',
    sintaxis: 'mon <on|off|json|texto|ms>',
    que: 'Controla la telemetria automatica por serial. El puente ya la configura solo.',
    args: [{ tipo: 'texto', patron: /^(on|off|json|texto|\d+)$/ }],
    ejemplos: ['mon json']
  },
  {
    nombre: 'ayuda', categoria: 'sistema',
    sintaxis: 'ayuda',
    que: 'Lista los comandos en la consola de la placa.',
    args: [], ejemplos: ['ayuda']
  },

  // ======================= DIAGNOSTICO =======================
  {
    nombre: 'test', categoria: 'diagnostico',
    sintaxis: 'test <leds|sensores>',
    que: 'test leds hace un barrido encendiendo los 6 LEDs uno por uno (~2.5 s); test sensores imprime las lecturas crudas de todos los pines.',
    ojo: '"test leds" pisa cualquier efecto de luz que estuviera activo y al terminar deja los LEDs en "auto". No lo mandes junto con otros comandos de luces.',
    args: [{ tipo: 'enum', valores: ['leds', 'sensores'] }],
    ejemplos: ['test leds']
  }
];

/**
 * Alias de calles. La maqueta solo conoce c1 y c2; aqui se traducen los
 * nombres reales para poder hablarle con naturalidad.
 *
 * EDITA ESTO con los nombres de tu maqueta.
 */
export const ALIAS_CALLES = {
  c1: ['calle 1', 'calle uno', 'calle 10', 'horizontal', 'principal', 'avenida'],
  c2: ['calle 2', 'calle dos', 'carrera 43', 'vertical', 'secundaria', 'transversal']
};

/** Indice por nombre, para el validador. */
export const POR_NOMBRE = Object.fromEntries(COMANDOS.map((c) => [c.nombre, c]));

/** Comandos de una categoria. */
export function comandosDe(categoria) {
  return COMANDOS.filter((c) => c.categoria === categoria);
}

/**
 * Render compacto de un subconjunto de categorias, para meter en el prompt.
 * Es deliberadamente escueto: cada token cuenta para la latencia.
 */
export function renderCategorias(categorias) {
  const partes = [];
  for (const cat of categorias) {
    const def = CATEGORIAS[cat];
    const lista = comandosDe(cat);
    if (!def || lista.length === 0) continue;
    partes.push(`## ${def.titulo}`);
    for (const c of lista) {
      partes.push(`- \`${c.sintaxis}\` — ${c.que}`);
      // La trampa va antes que los ejemplos: es lo que evita las secuencias
      // que validan bien pero no hacen nada visible en la maqueta.
      if (c.ojo) partes.push(`  OJO: ${c.ojo}`);
      if (c.ejemplos?.length) partes.push(`  ej: ${c.ejemplos.join(' | ')}`);
    }
  }
  return partes.join('\n');
}
