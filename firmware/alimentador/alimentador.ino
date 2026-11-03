/*
 ==============================================================
             ARQUITECTURA DEL COMPUTADOR
        PROYECTO: SISTEMA DE CONTROL DE ACCESO

                 PROGRAMA BASE
 ==============================================================

 Este programa contiene la arquitectura general del proyecto.

 El código está INCOMPLETO intencionalmente.

 Durante los siguientes laboratorios se implementarán
 progresivamente las funciones marcadas como:

                         TODO

 Al finalizar los laboratorios, este programa se habrá
 convertido en el sistema completo de control de acceso.
 ==============================================================
*/


#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <DHT.h>


// ==============================================================
// 1. MAPA DE PINES
// ==============================================================

/*
 ARDUINO UNO
 --------------------------------------------------------------

 D0  -> HC-05 TX   (Serial RX)
 D1  -> HC-05 RX   (Serial TX, con divisor de voltaje)

 D2  -> Buzzer pasivo
 D3  -> Botón pedir comida / agua
 D4  -> Servo (comida)

 D5  -> DHT11

 D6  -> RTC DS1302 CE (RST)

 D7  -> 74HC595 DATA
 D8  -> 74HC595 CLOCK
 D9  -> 74HC595 LATCH

 D10 -> RFID SDA / SS
 D11 -> RFID MOSI
 D12 -> RFID MISO
 D13 -> RFID SCK

 A0  -> Keypad C1
 A1  -> Keypad C2
 A2  -> RTC DS1302 DAT (IO)
 A3  -> RTC DS1302 CLK

 A4  -> LCD SDA
 A5  -> LCD SCL

 RFID RST -> 3.3V fijo (reset por software)
*/


const byte PIN_BUZZER = 2;
const byte PIN_BOTON  = 3;
const byte PIN_SERVO  = 4;
const byte PIN_DHT    = 5;

const byte RTC_CE  = 6;
const byte RTC_DAT = A2;
const byte RTC_CLK = A3;

const byte DATA  = 7;
const byte CLOCK = 8;
const byte LATCH = 9;

const byte RFID_SS  = 10;
const byte RFID_RST = MFRC522::UNUSED_PIN;

const byte NUM_COLUMNAS = 2;

const byte columnas[NUM_COLUMNAS] = {
  A0, A1
};


// ==============================================================
// 2. DISPOSITIVOS
// ==============================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo puerta;

MFRC522 rfid(RFID_SS, RFID_RST);

ThreeWire hilosRTC(RTC_DAT, RTC_CLK, RTC_CE);

RtcDS1302<ThreeWire> rtc(hilosRTC);

DHT dht(PIN_DHT, DHT11);


// ==============================================================
// 3. REGISTRO DE DESPLAZAMIENTO 74HC595
// ==============================================================

/*

                  74HC595

             Q0 ─────► Keypad R1
             Q1 ─────► Keypad R2
             Q2 ─────► Keypad R3
             Q3 ─────► Keypad R4

             Q4 ─────► LED verde
             Q5 ─────► LED rojo
             Q6 ─────► LED amarillo
             Q7 ─────► LED azul


 Bits:

       7   6   5   4   3   2   1   0
     ┌───┬───┬───┬───┬───┬───┬───┬───┐
     │AZ │AM │RO │VE │R4 │R3 │R2 │R1 │
     └───┴───┴───┴───┴───┴───┴───┴───┘

*/

// Inicialmente las cuatro filas están HIGH.

byte registro = B00001111;


// ==============================================================
// 4. MAPA DEL KEYPAD
// ==============================================================

/*
 Teclado 4x4 usado como 4x2 (solo C1 y C2 conectadas).

   Botón:  S1  S2  /  S5  S6  /  S9  S10  /  S13  S14
   Tecla:   1   2  /   4   5  /   7    8  /    *    0

 '*' borra el último dígito del PIN.
*/

const byte NUM_FILAS = 4;

char teclas[NUM_FILAS][NUM_COLUMNAS] = {

  {'1', '2'},
  {'4', '5'},
  {'7', '8'},
  {'*', '0'}

};


// ==============================================================
// 5. VARIABLES DEL SISTEMA
// ==============================================================

// UID obtenido del RC522

String UIDLeido = "";


// PIN introducido mediante el keypad

char pinIngresado[5] = "";

byte posicionPIN = 0;


// Número de intentos incorrectos

byte intentosFallidos = 0;


// Momento en que se entró al estado actual (para timeouts sin delay)

unsigned long inicioEstado = 0;


// ==============================================================
// 6. ESTADOS DEL SISTEMA
// ==============================================================

/*
 La CPU solamente puede encontrarse en uno de estos
 estados en un momento determinado.

 El estado actual determina qué operación realizará
 el programa.
*/

enum EstadoSistema
{
  ESPERANDO_RFID,

  ESPERANDO_PIN,

  VALIDANDO_PIN,

  ACCESO_AUTORIZADO,

  ACCESO_DENEGADO,

  DESPACHANDO,

  ALARMA,

  ELIGIENDO_TIPO,

  MOSTRANDO_MENSAJE
};


EstadoSistema estado = ESPERANDO_RFID;


/*
 Tipo de despacho. Se declara aquí, antes de cualquier función,
 porque el IDE de Arduino genera los prototipos al inicio del
 archivo y necesita conocer el tipo.
*/

enum TipoDespacho
{
  COMIDA,

  AGUA
};


/*
 Quién inició la solicitud: con el botón se elige comida o
 agua después del PIN; con la tarjeta, el tipo lo define
 la tarjeta (Feature 06).
*/

enum OrigenSolicitud
{
  ORIGEN_BOTON,

  ORIGEN_TARJETA
};


OrigenSolicitud origenSolicitud = ORIGEN_BOTON;


// ==============================================================
// 7. ACTUALIZAR 74HC595
// ==============================================================

void actualizarRegistro()
{
  digitalWrite(LATCH, LOW);

  shiftOut(DATA, CLOCK, MSBFIRST, registro);

  digitalWrite(LATCH, HIGH);
}


// ==============================================================
// 8. CONTROL DE LEDs
// ==============================================================

/*
 Q4-Q7 controlan los LEDs y Q0-Q3 las filas del teclado.
 La máscara B11110000 aísla los bits de los LEDs:

   registro & ~MASCARA_LEDS  -> pone Q4-Q7 en 0, conserva Q0-Q3
   bitSet(registro, n + 4)   -> enciende solo el LED n
*/

const byte MASCARA_LEDS = B11110000;

const byte LED_VERDE    = 0;
const byte LED_ROJO     = 1;
const byte LED_AMARILLO = 2;
const byte LED_AZUL     = 3;


void apagarLEDs()
{
  registro &= ~MASCARA_LEDS;

  actualizarRegistro();
}


void encenderLED(byte numero)
{
  if (numero > LED_AZUL)
  {
    return;
  }

  registro &= ~MASCARA_LEDS;

  bitSet(registro, numero + 4);

  actualizarRegistro();
}


void probarLEDs()
{
  for (byte led = LED_VERDE; led <= LED_AZUL; led++)
  {
    encenderLED(led);

    delay(300);
  }

  apagarLEDs();
}


// ==============================================================
// 9. SELECCIÓN DE FILA DEL KEYPAD
// ==============================================================

void seleccionarFila(byte fila)
{
  registro |= B00001111;

  bitClear(registro, fila);

  actualizarRegistro();
}


// ==============================================================
// 10. LECTURA DEL KEYPAD
// ==============================================================

/*
 Lectura sin bloqueo: cada llamada barre la matriz una vez.
 Una tecla se reporta una sola vez, cuando lleva TIEMPO_ANTIRREBOTE
 estable presionada. Mientras sigue presionada o al soltarla
 se retorna 0, así loop() nunca se detiene esperando al usuario.
*/

const unsigned long TIEMPO_ANTIRREBOTE = 40;

char teclaLeida   = 0;
char teclaEstable = 0;
unsigned long ultimoCambioTecla = 0;


char barrerTeclado()
{
  char tecla = 0;

  for (byte fila = 0; fila < NUM_FILAS; fila++)
  {
    seleccionarFila(fila);

    delayMicroseconds(10);

    for (byte columna = 0; columna < NUM_COLUMNAS; columna++)
    {
      if (digitalRead(columnas[columna]) == LOW)
      {
        tecla = teclas[fila][columna];
      }
    }
  }

  registro |= B00001111;

  actualizarRegistro();

  return tecla;
}


char leerTeclado()
{
  char lectura = barrerTeclado();

  if (lectura != teclaLeida)
  {
    teclaLeida = lectura;

    ultimoCambioTecla = millis();

    return 0;
  }

  if (millis() - ultimoCambioTecla < TIEMPO_ANTIRREBOTE || lectura == teclaEstable)
  {
    return 0;
  }

  teclaEstable = lectura;

  return lectura;
}


// ==============================================================
// 11. LECTURA DEL RFID
// ==============================================================

String leerTarjeta()
{
  /*
   TODO - LABORATORIO RFID

   Utilizar el módulo RC522.

   El algoritmo deberá:

   1. Detectar si existe una nueva tarjeta.
   2. Leer el identificador UID.
   3. Recorrer los bytes del UID.
   4. Convertirlos a una representación que pueda
      utilizar el programa.
   5. Retornar el UID.

   Si no existe una tarjeta:

       return "";
  */

  return "";
}


// ==============================================================
// 12. VALIDACIÓN DE TARJETA
// ==============================================================

bool tarjetaAutorizada(String uid)
{
  /*
   TODO - LABORATORIO RFID

   Determinar si el UID recibido pertenece
   a una tarjeta autorizada.

   Retornar:

       true  -> tarjeta autorizada
       false -> tarjeta no autorizada
  */

  return false;
}


// ==============================================================
// 13. CAPTURA DEL PIN
// ==============================================================

/*
 Los asteriscos se muestran en la fila 1 del LCD, a la derecha
 de "INGRESE PIN" (columnas 12 a 15).
*/

const byte LARGO_PIN   = 4;
const byte COLUMNA_PIN = 12;


bool leerPIN()
{
  char tecla = leerTeclado();

  if (tecla == 0)
  {
    return false;
  }

  if (tecla == '*')
  {
    if (posicionPIN > 0)
    {
      posicionPIN--;

      pinIngresado[posicionPIN] = '\0';

      lcd.setCursor(COLUMNA_PIN + posicionPIN, 1);
      lcd.print(' ');
    }

    return false;
  }

  if (tecla < '0' || tecla > '9' || posicionPIN >= LARGO_PIN)
  {
    return false;
  }

  pinIngresado[posicionPIN] = tecla;

  lcd.setCursor(COLUMNA_PIN + posicionPIN, 1);
  lcd.print('*');

  posicionPIN++;

  pinIngresado[posicionPIN] = '\0';

  return posicionPIN == LARGO_PIN;
}


// ==============================================================
// 14. VALIDACIÓN DEL PIN
// ==============================================================

/*
 MAPA DE LA EEPROM (1 KB en el ATmega328P)

   Dirección   Contenido
   0           Firma 0xA5: la EEPROM ya tiene configuración
   1 - 4       PIN (4 caracteres ASCII)
   5 - 8       UID tarjeta de comida      (Feature 06)
   9 - 12      UID tarjeta de agua        (Feature 06)
   13 - 14     Dosis de comida y de agua  (Feature 08)
   15          Modo automático / manual   (Feature 07)
   16 - 21     Horarios 1 y 2             (Feature 07)

 La EEPROM conserva los datos sin energía. Si la firma no
 está, es la primera vez que corre el programa y se graban
 los valores de fábrica.
*/

const int  EE_FIRMA     = 0;
const int  EE_PIN       = 1;
const byte FIRMA_EEPROM = 0xA5;

const char PIN_FABRICA[] = "1245";


void iniciarEEPROM()
{
  if (EEPROM.read(EE_FIRMA) == FIRMA_EEPROM)
  {
    return;
  }

  for (byte i = 0; i < LARGO_PIN; i++)
  {
    EEPROM.update(EE_PIN + i, PIN_FABRICA[i]);
  }

  EEPROM.update(EE_FIRMA, FIRMA_EEPROM);
}


bool validarPIN()
{
  for (byte i = 0; i < LARGO_PIN; i++)
  {
    if (EEPROM.read(EE_PIN + i) != pinIngresado[i])
    {
      return false;
    }
  }

  return true;
}


// ==============================================================
// 15. ABRIR PUERTA
// ==============================================================

/*
 En el alimentador, la "puerta" es la compuerta de comida:
 el servo la abre a 90° mientras cae la porción.
*/

const byte SERVO_CERRADO = 0;
const byte SERVO_ABIERTO = 90;


void abrirPuerta()
{
  puerta.write(SERVO_ABIERTO);
}


// ==============================================================
// 16. CERRAR PUERTA
// ==============================================================

void cerrarPuerta()
{
  puerta.write(SERVO_CERRADO);
}


// ==============================================================
// 17. SONIDO DE ACCESO CORRECTO
// ==============================================================

/*
 tone() con duración no bloquea: el Timer2 genera la onda
 y la apaga solo, mientras loop() sigue corriendo.
*/

void sonidoCorrecto()
{
  tone(PIN_BUZZER, 1047, 150);
}


// ==============================================================
// 18. SONIDO DE ERROR
// ==============================================================

void sonidoError()
{
  tone(PIN_BUZZER, 220, 400);
}


// ==============================================================
// 19. ALARMA
// ==============================================================

/*
 Después de 3 PIN incorrectos el sistema se bloquea 30 s:
 el LED rojo parpadea, el buzzer pita y el LCD muestra la
 cuenta regresiva. Se llama en cada vuelta de loop() y
 retorna false cuando el bloqueo terminó.
*/

const unsigned long TIEMPO_BLOQUEO = 30000;

unsigned long ultimoPulsoAlarma = 0;


bool activarAlarma()
{
  unsigned long transcurrido = millis() - inicioEstado;

  if (transcurrido >= TIEMPO_BLOQUEO)
  {
    apagarLEDs();

    noTone(PIN_BUZZER);

    return false;
  }

  if (millis() - ultimoPulsoAlarma >= 500)
  {
    ultimoPulsoAlarma = millis();

    registro ^= (1 << (LED_ROJO + 4));

    actualizarRegistro();

    tone(PIN_BUZZER, 880, 120);

    char linea[17];

    snprintf(linea, sizeof(linea), "ESPERE: %lu s", (TIEMPO_BLOQUEO - transcurrido + 999) / 1000);

    escribirLinea(1, linea);
  }

  return true;
}


// ==============================================================
// 19.1 RELOJ DE TIEMPO REAL (DS1302)
// ==============================================================

/*
 El DS1302 conserva la hora con su batería aunque el Arduino
 se apague. Solo se ajusta si perdió la hora o si quedó atrasado
 respecto al momento en que se compiló el programa.
*/

void iniciarRTC()
{
  rtc.Begin();

  RtcDateTime compilado(__DATE__, __TIME__);

  if (rtc.GetIsWriteProtected())
  {
    rtc.SetIsWriteProtected(false);
  }

  if (!rtc.GetIsRunning())
  {
    rtc.SetIsRunning(true);
  }

  if (!rtc.IsDateTimeValid() || rtc.GetDateTime().TotalSeconds() < compilado.TotalSeconds())
  {
    rtc.SetDateTime(compilado);
  }
}


// ==============================================================
// 19.2 SENSOR DE TEMPERATURA Y HUMEDAD (DHT11)
// ==============================================================

/*
 El DHT11 tarda en medir y no admite lecturas más seguidas
 que una por segundo; se lee cada 2 s y se guarda el último valor.
*/

const unsigned long INTERVALO_DHT = 2000;

float temperatura = NAN;
float humedad     = NAN;

unsigned long ultimaLecturaDHT = 0;


void leerSensor()
{
  if (millis() - ultimaLecturaDHT < INTERVALO_DHT)
  {
    return;
  }

  ultimaLecturaDHT = millis();

  temperatura = dht.readTemperature();

  humedad = dht.readHumidity();
}


// ==============================================================
// 19.3 PANTALLA DE REPOSO
// ==============================================================

/*
   Fila 0:  DD/MM HH:MM:SS
   Fila 1:  T:24C  H:60%

 Se reescribe una vez por segundo sin lcd.clear() para que
 la pantalla no parpadee.
*/

const unsigned long INTERVALO_PANTALLA = 1000;

unsigned long ultimaPantalla = 0;

bool refrescarReposo = true;


void escribirLinea(byte fila, const char *texto)
{
  char linea[17];

  snprintf(linea, sizeof(linea), "%-16s", texto);

  lcd.setCursor(0, fila);
  lcd.print(linea);
}


void mostrarReposo()
{
  if (!refrescarReposo && millis() - ultimaPantalla < INTERVALO_PANTALLA)
  {
    return;
  }

  refrescarReposo = false;

  ultimaPantalla = millis();

  char linea[17];

  if (rtc.IsDateTimeValid())
  {
    RtcDateTime ahora = rtc.GetDateTime();

    snprintf(linea, sizeof(linea), "%02u/%02u %02u:%02u:%02u",
             ahora.Day(), ahora.Month(), ahora.Hour(), ahora.Minute(), ahora.Second());
  }
  else
  {
    snprintf(linea, sizeof(linea), "RELOJ: ERROR");
  }

  escribirLinea(0, linea);

  if (isnan(temperatura) || isnan(humedad))
  {
    snprintf(linea, sizeof(linea), "SENSOR: ERROR");
  }
  else
  {
    snprintf(linea, sizeof(linea), "T:%dC  H:%d%%", (int)round(temperatura), (int)round(humedad));
  }

  escribirLinea(1, linea);
}


// ==============================================================
// 19.4 DESPACHO DE COMIDA Y AGUA
// ==============================================================

/*
 Comida: servo abierto + LED amarillo.
 Agua:   LED azul (bomba simulada).

 Duración proporcional a la dosis:  t = k * d
   k = SEGUNDOS_POR_DOSIS,  d = dosis

 despachar() inicia el despacho y actualizarDespacho() lo
 termina cuando se cumple el tiempo, sin detener loop().

 El enum TipoDespacho está en la sección 6.
*/

const unsigned long SEGUNDOS_POR_DOSIS = 1;

byte dosisComida = 3;
byte dosisAgua   = 2;

TipoDespacho tipoSolicitado = COMIDA;

TipoDespacho tipoDespacho = COMIDA;

unsigned long inicioDespacho   = 0;
unsigned long duracionDespacho = 0;

bool despachoActivo = false;


byte dosisDe(TipoDespacho tipo)
{
  return tipo == COMIDA ? dosisComida : dosisAgua;
}


void despachar(TipoDespacho tipo, byte dosis)
{
  tipoDespacho = tipo;

  duracionDespacho = dosis * SEGUNDOS_POR_DOSIS * 1000UL;

  inicioDespacho = millis();

  despachoActivo = true;

  if (tipo == COMIDA)
  {
    abrirPuerta();

    encenderLED(LED_AMARILLO);
  }
  else
  {
    encenderLED(LED_AZUL);
  }

  char linea[17];

  snprintf(linea, sizeof(linea), tipo == COMIDA ? "DANDO COMIDA x%u" : "DANDO AGUA x%u", dosis);

  escribirLinea(0, linea);

  sonidoCorrecto();

  Serial.print(F("EV,"));
  Serial.print(tipo == COMIDA ? 'C' : 'A');
  Serial.print(',');
  Serial.println(dosis);

  ultimaPantalla = 0;
}


bool actualizarDespacho()
{
  if (!despachoActivo)
  {
    return false;
  }

  unsigned long transcurrido = millis() - inicioDespacho;

  if (transcurrido >= duracionDespacho)
  {
    cerrarPuerta();

    apagarLEDs();

    tone(PIN_BUZZER, 784, 120);

    despachoActivo = false;

    return false;
  }

  if (millis() - ultimaPantalla >= 250)
  {
    ultimaPantalla = millis();

    char linea[17];

    snprintf(linea, sizeof(linea), "RESTAN: %lu s", (duracionDespacho - transcurrido + 999) / 1000);

    escribirLinea(1, linea);
  }

  return true;
}


// ==============================================================
// 19.5 SOLICITUD MANUAL, MENSAJES Y BLOQUEO
// ==============================================================

/*
 Flujo del botón:

   REPOSO --botón--> ESPERANDO_PIN --PIN ok--> ELIGIENDO_TIPO
          --1 / 2--> ACCESO_AUTORIZADO --> DESPACHANDO --> REPOSO

   PIN incorrecto --> ACCESO_DENEGADO (1.5 s) --> REPOSO
   3 incorrectos  --> ALARMA (30 s)           --> REPOSO
   Sin respuesta  --> MOSTRANDO_MENSAJE       --> REPOSO
*/

const unsigned long TIMEOUT_PIN     = 20000;
const unsigned long TIMEOUT_TIPO    = 10000;
const unsigned long TIEMPO_MENSAJE  = 1500;
const byte          MAX_INTENTOS    = 3;

unsigned long duracionMensaje = 0;

bool botonAnterior = HIGH;
unsigned long ultimoCambioBoton = 0;


void cambiarEstado(EstadoSistema nuevo)
{
  estado = nuevo;

  inicioEstado = millis();
}


bool botonPresionado()
{
  bool lectura = digitalRead(PIN_BOTON);

  if (lectura != botonAnterior && millis() - ultimoCambioBoton > 50)
  {
    ultimoCambioBoton = millis();

    botonAnterior = lectura;

    return lectura == LOW;
  }

  return false;
}


void pedirPIN(const char *titulo)
{
  posicionPIN = 0;

  pinIngresado[0] = '\0';

  escribirLinea(0, titulo);
  escribirLinea(1, "INGRESE PIN");

  cambiarEstado(ESPERANDO_PIN);
}


void pedirTipo()
{
  encenderLED(LED_VERDE);

  sonidoCorrecto();

  escribirLinea(0, "PIN CORRECTO");
  escribirLinea(1, "1=COMIDA 2=AGUA");

  cambiarEstado(ELIGIENDO_TIPO);
}


void denegarAcceso(const char *motivo)
{
  encenderLED(LED_ROJO);

  sonidoError();

  char linea[17];

  snprintf(linea, sizeof(linea), "INTENTO %u DE %u", intentosFallidos, MAX_INTENTOS);

  escribirLinea(0, motivo);
  escribirLinea(1, linea);

  cambiarEstado(ACCESO_DENEGADO);
}


void mostrarMensaje(const char *linea1, const char *linea2, unsigned long duracion)
{
  apagarLEDs();

  escribirLinea(0, linea1);
  escribirLinea(1, linea2);

  duracionMensaje = duracion;

  cambiarEstado(MOSTRANDO_MENSAJE);
}


void iniciarAlarma()
{
  escribirLinea(0, "BLOQUEADO");
  escribirLinea(1, "");

  ultimoPulsoAlarma = 0;

  cambiarEstado(ALARMA);
}


// ==============================================================
// 20. REINICIAR CICLO DE AUTENTICACIÓN
// ==============================================================

void reiniciarSistema()
{
  UIDLeido = "";

  posicionPIN = 0;

  pinIngresado[0] = '\0';


  apagarLEDs();

  lcd.clear();

  refrescarReposo = true;


  cambiarEstado(ESPERANDO_RFID);
}


// ==============================================================
// 21. SETUP
// ==============================================================

void setup()
{

  // ----------------------------------------------------------
  // SERIAL
  // ----------------------------------------------------------

  Serial.begin(9600);


  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  lcd.init();

  lcd.backlight();


  // ----------------------------------------------------------
  // 74HC595
  // ----------------------------------------------------------

  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  actualizarRegistro();


  // ----------------------------------------------------------
  // KEYPAD
  // ----------------------------------------------------------

  for (byte i = 0; i < NUM_COLUMNAS; i++)
  {
    pinMode(columnas[i], INPUT_PULLUP);
  }


  // ----------------------------------------------------------
  // BUZZER
  // ----------------------------------------------------------

  pinMode(PIN_BUZZER, OUTPUT);


  // ----------------------------------------------------------
  // BOTÓN
  // ----------------------------------------------------------

  pinMode(PIN_BOTON, INPUT_PULLUP);


  // ----------------------------------------------------------
  // SERVO
  // ----------------------------------------------------------

  puerta.attach(PIN_SERVO);

  cerrarPuerta();


  // ----------------------------------------------------------
  // RFID
  // ----------------------------------------------------------

  SPI.begin();

  rfid.PCD_Init();


  // ----------------------------------------------------------
  // RELOJ Y SENSOR
  // ----------------------------------------------------------

  iniciarRTC();

  dht.begin();


  // ----------------------------------------------------------
  // EEPROM
  // ----------------------------------------------------------

  iniciarEEPROM();


  // ----------------------------------------------------------
  // INICIO
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SISTEMA URL");

  lcd.setCursor(0, 1);
  lcd.print("INICIANDO...");

  probarLEDs();

  reiniciarSistema();
}


// ==============================================================
// 22. LOOP
// ==============================================================

void loop()
{

  /*
   ============================================================
                       UNIDAD DE CONTROL
   ============================================================

   La variable "estado" representa el estado actual
   del sistema.

   Dependiendo de ese estado, la CPU ejecutará una
   operación diferente.
  */


  leerSensor();


  switch (estado)
  {

    // ========================================================
    // ESTADO 0 - ESPERANDO RFID
    // ========================================================

    case ESPERANDO_RFID:

      mostrarReposo();

      if (botonPresionado())
      {
        origenSolicitud = ORIGEN_BOTON;

        pedirPIN("PEDIDO MANUAL");

        break;
      }

      UIDLeido = leerTarjeta();

      if (UIDLeido != "")
      {
        if (tarjetaAutorizada(UIDLeido))
        {
          lcd.clear();

          lcd.setCursor(0, 0);
          lcd.print("TARJETA VALIDA");

          lcd.setCursor(0, 1);
          lcd.print("INGRESE PIN");

          estado = ESPERANDO_PIN;
        }
        else
        {
          estado = ACCESO_DENEGADO;
        }
      }

      break;


    // ========================================================
    // ESTADO 1 - ESPERANDO PIN
    // ========================================================

    case ESPERANDO_PIN:

      if (millis() - inicioEstado >= TIMEOUT_PIN)
      {
        mostrarMensaje("TIEMPO AGOTADO", "", TIEMPO_MENSAJE);
      }
      else if (leerPIN())
      {
        estado = VALIDANDO_PIN;
      }

      break;


    // ========================================================
    // ESTADO 2 - VALIDANDO PIN
    // ========================================================

    case VALIDANDO_PIN:

      if (validarPIN())
      {
        intentosFallidos = 0;

        if (origenSolicitud == ORIGEN_BOTON)
        {
          pedirTipo();
        }
        else
        {
          cambiarEstado(ACCESO_AUTORIZADO);
        }
      }
      else
      {
        intentosFallidos++;

        denegarAcceso("PIN INCORRECTO");
      }

      break;


    // ========================================================
    // ESTADO 3 - ACCESO AUTORIZADO
    // ========================================================

    case ACCESO_AUTORIZADO:

      lcd.clear();

      despachar(tipoSolicitado, dosisDe(tipoSolicitado));

      estado = DESPACHANDO;

      break;


    // ========================================================
    // ESTADO 4 - ACCESO DENEGADO
    // ========================================================

    case ACCESO_DENEGADO:

      if (millis() - inicioEstado >= TIEMPO_MENSAJE)
      {
        if (intentosFallidos >= MAX_INTENTOS)
        {
          iniciarAlarma();
        }
        else
        {
          reiniciarSistema();
        }
      }

      break;


    // ========================================================
    // ESTADO 5 - DESPACHANDO
    // ========================================================

    case DESPACHANDO:

      if (!actualizarDespacho())
      {
        intentosFallidos = 0;

        reiniciarSistema();
      }

      break;


    // ========================================================
    // ESTADO 6 - ALARMA
    // ========================================================

    case ALARMA:

      if (!activarAlarma())
      {
        intentosFallidos = 0;

        reiniciarSistema();
      }

      break;


    // ========================================================
    // ESTADO 7 - ELIGIENDO COMIDA O AGUA (BOTÓN)
    // ========================================================

    case ELIGIENDO_TIPO:
    {
      char tecla = leerTeclado();

      if (tecla == '1' || tecla == '2')
      {
        tipoSolicitado = (tecla == '1') ? COMIDA : AGUA;

        cambiarEstado(ACCESO_AUTORIZADO);
      }
      else if (tecla == '*')
      {
        mostrarMensaje("CANCELADO", "", TIEMPO_MENSAJE);
      }
      else if (millis() - inicioEstado >= TIMEOUT_TIPO)
      {
        mostrarMensaje("TIEMPO AGOTADO", "", TIEMPO_MENSAJE);
      }

      break;
    }


    // ========================================================
    // ESTADO 8 - MENSAJE TEMPORAL
    // ========================================================

    case MOSTRANDO_MENSAJE:

      if (millis() - inicioEstado >= duracionMensaje)
      {
        reiniciarSistema();
      }

      break;
  }
}