/**
 * LIMITADOR DE FRECUENCIA (local)
 * ============================================================================
 * El plan gratuito de Gemini es MUY estrecho por minuto (5 RPM en los modelos
 * Flash, 15 en los Lite). Cuando te pasas, la API responde 429 y todo se vuelve
 * lento y confuso.
 *
 * Este contador lleva la cuenta de las peticiones que hemos hecho nosotros, y
 * si vamos a pasarnos avisa ANTES de llamar, diciendo cuantos segundos faltan.
 * Es mejor un "espera 12 s" inmediato que una espera ciega de 30 s que termina
 * en un error que no explica nada.
 *
 * Ojo: es una estimacion nuestra, no el contador real de Google. Si usas la
 * misma clave desde otro lado, el de Google ira mas adelantado que este.
 */

// Limites del plan gratuito, por modelo. Fuente: ai.dev/rate-limit
// (RPM = peticiones por minuto, RPD = peticiones por dia)
const LIMITES = {
  'gemini-3-flash':        { rpm: 5,  rpd: 20 },
  'gemini-3.5-flash':      { rpm: 5,  rpd: 20 },
  'gemini-3.6-flash':      { rpm: 5,  rpd: 20 },
  'gemini-3.7-flash':      { rpm: 5,  rpd: 20 },
  'gemini-3.8-flash':      { rpm: 5,  rpd: 20 },
  'gemini-2.5-flash':      { rpm: 5,  rpd: 20 },
  'gemini-3.1-flash-lite': { rpm: 15, rpd: 500 },
  'gemini-3.5-flash-lite': { rpm: 15, rpd: 500 },
  'gemini-2.5-flash-lite': { rpm: 10, rpd: 20 }
};

const POR_DEFECTO = { rpm: 5, rpd: 20 };

export function limitesDe(modelo) {
  const limpio = String(modelo).replace(/^models\//, '');
  return LIMITES[limpio] ?? POR_DEFECTO;
}

export class Limitador {
  constructor(modelo) {
    this.modelo = modelo;
    this.limites = limitesDe(modelo);
    this.sellos = [];        // instantes (ms) de cada peticion hecha
  }

  #limpiar() {
    const hace24h = Date.now() - 86400000;
    this.sellos = this.sellos.filter((t) => t > hace24h);
  }

  /** Peticiones en el ultimo minuto y en las ultimas 24 h. */
  uso() {
    this.#limpiar();
    const haceUnMinuto = Date.now() - 60000;
    return {
      minuto: this.sellos.filter((t) => t > haceUnMinuto).length,
      dia: this.sellos.length,
      limites: this.limites
    };
  }

  /**
   * ¿Podemos llamar ahora?
   * @returns {{permitido: boolean, motivo?: string, esperaSegundos?: number}}
   */
  revisar() {
    const { minuto, dia } = this.uso();

    if (dia >= this.limites.rpd) {
      return {
        permitido: false,
        motivo: `Llegaste al limite diario del plan gratuito para ${this.modelo} ` +
                `(${this.limites.rpd} peticiones al dia). Se renueva solo. ` +
                `Mientras tanto puedes cambiar GEMINI_MODELO en .env, o usar ` +
                `los botones y la consola, que no dependen de la IA.`
      };
    }

    if (minuto >= this.limites.rpm) {
      const haceUnMinuto = Date.now() - 60000;
      const masViejo = this.sellos.filter((t) => t > haceUnMinuto)[0];
      const espera = Math.max(1, Math.ceil((masViejo + 60000 - Date.now()) / 1000));
      return {
        permitido: false,
        esperaSegundos: espera,
        motivo: `Vas muy rapido: el plan gratuito permite ${this.limites.rpm} ` +
                `peticiones por minuto en ${this.modelo}. Espera ${espera} s y vuelve a intentar.`
      };
    }

    return { permitido: true };
  }

  /** Se llama justo antes de cada peticion real. */
  registrar() {
    this.sellos.push(Date.now());
  }

  /**
   * Si Google nos dice que nos pasamos, damos por buenos SUS numeros: se
   * rellena el contador local hasta el limite para no seguir insistiendo.
   */
  marcarRechazo(porDia) {
    const ahora = Date.now();
    const faltan = porDia
      ? this.limites.rpd - this.uso().dia
      : this.limites.rpm - this.uso().minuto;
    for (let i = 0; i < Math.max(0, faltan); i++) this.sellos.push(ahora);
  }
}
