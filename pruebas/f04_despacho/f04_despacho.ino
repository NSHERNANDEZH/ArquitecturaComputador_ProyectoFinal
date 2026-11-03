/*
 Prueba Feature 04 - Despacho de comida y agua

 Teclas:
   1 = dar comida (servo abierto + LED amarillo)
   2 = dar agua   (LED azul)
   4 = bajar dosis    5 = subir dosis   (1 a 9)

 Duracion: t = k * d, con k = 1 s por dosis.

 Al terminar cada despacho imprime la duracion medida y OK si
 coincide con la esperada (+-150 ms). Las teclas presionadas
 durante un despacho se reportan como ignoradas: eso prueba que
 loop() sigue corriendo y no hay bloqueo.

 Monitor Serial a 9600.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

const byte PIN_BUZZER = 2;
const byte PIN_SERVO  = 4;

const byte DATA  = 7;
const byte CLOCK = 8;
const byte LATCH = 9;

const byte NUM_FILAS    = 4;
const byte NUM_COLUMNAS = 2;
const byte columnas[NUM_COLUMNAS] = {A0, A1};

char teclas[NUM_FILAS][NUM_COLUMNAS] = {
  {'1', '2'},
  {'4', '5'},
  {'7', '8'},
  {'*', '0'}
};

const byte MASCARA_LEDS = B11110000;
const byte LED_AMARILLO = 2;
const byte LED_AZUL     = 3;

const byte SERVO_CERRADO = 0;
const byte SERVO_ABIERTO = 90;

const unsigned long SEGUNDOS_POR_DOSIS = 1;

enum TipoDespacho { COMIDA, AGUA };

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo puerta;

byte registro = B00001111;


// ---------------- 74HC595, LEDs y teclado ----------------

void actualizarRegistro()
{
  digitalWrite(LATCH, LOW);
  shiftOut(DATA, CLOCK, MSBFIRST, registro);
  digitalWrite(LATCH, HIGH);
}

void encenderLED(byte numero)
{
  registro &= ~MASCARA_LEDS;
  bitSet(registro, numero + 4);
  actualizarRegistro();
}

void apagarLEDs()
{
  registro &= ~MASCARA_LEDS;
  actualizarRegistro();
}

const unsigned long TIEMPO_ANTIRREBOTE = 40;

char teclaLeida   = 0;
char teclaEstable = 0;
unsigned long ultimoCambioTecla = 0;

void seleccionarFila(byte fila)
{
  registro |= B00001111;
  bitClear(registro, fila);
  actualizarRegistro();
}

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


// ---------------- LCD ----------------

void escribirLinea(byte fila, const char *texto)
{
  char linea[17];
  snprintf(linea, sizeof(linea), "%-16s", texto);
  lcd.setCursor(0, fila);
  lcd.print(linea);
}


// ---------------- Despacho (igual que el firmware) ----------------

TipoDespacho tipoDespacho = COMIDA;

unsigned long inicioDespacho   = 0;
unsigned long duracionDespacho = 0;
unsigned long ultimaPantalla   = 0;

bool despachoActivo = false;

void despachar(TipoDespacho tipo, byte dosis)
{
  tipoDespacho = tipo;
  duracionDespacho = dosis * SEGUNDOS_POR_DOSIS * 1000UL;
  inicioDespacho = millis();
  despachoActivo = true;

  if (tipo == COMIDA)
  {
    puerta.write(SERVO_ABIERTO);
    encenderLED(LED_AMARILLO);
  }
  else
  {
    encenderLED(LED_AZUL);
  }

  char linea[17];
  snprintf(linea, sizeof(linea), tipo == COMIDA ? "DANDO COMIDA x%u" : "DANDO AGUA x%u", dosis);
  escribirLinea(0, linea);

  tone(PIN_BUZZER, 1047, 150);

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
    puerta.write(SERVO_CERRADO);
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


// ---------------- Prueba ----------------

byte dosis = 3;
byte despachosOk = 0;
byte despachosMal = 0;

void mostrarMenu()
{
  escribirLinea(0, "F04 1=COM 2=AGUA");
  char linea[17];
  snprintf(linea, sizeof(linea), "DOSIS:%u   4- 5+", dosis);
  escribirLinea(1, linea);
}

void verificarDuracion()
{
  unsigned long real = millis() - inicioDespacho;
  long diferencia = (long)real - (long)duracionDespacho;
  bool ok = diferencia >= -150 && diferencia <= 150;

  if (ok) despachosOk++; else despachosMal++;

  Serial.print(F("   Duracion real: "));
  Serial.print(real);
  Serial.print(F(" ms  (esperada "));
  Serial.print(duracionDespacho);
  Serial.print(F(" ms)  "));
  Serial.println(ok ? F("OK") : F("ERROR"));

  Serial.print(F("   Despachos OK: "));
  Serial.print(despachosOk);
  Serial.print(F("   con error: "));
  Serial.println(despachosMal);
}

void setup()
{
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);
  actualizarRegistro();

  for (byte i = 0; i < NUM_COLUMNAS; i++)
  {
    pinMode(columnas[i], INPUT_PULLUP);
  }

  pinMode(PIN_BUZZER, OUTPUT);

  puerta.attach(PIN_SERVO);
  puerta.write(SERVO_CERRADO);

  Serial.println(F("=== Prueba F04: despacho ==="));
  Serial.println(F("1 = comida, 2 = agua, 4/5 = dosis"));

  mostrarMenu();
}

void loop()
{
  char tecla = leerTeclado();

  if (despachoActivo)
  {
    if (tecla)
    {
      Serial.print(F("   Tecla "));
      Serial.print(tecla);
      Serial.println(F(" ignorada: despacho en curso (loop sigue activo)"));
    }

    if (!actualizarDespacho())
    {
      verificarDuracion();
      mostrarMenu();
    }
    return;
  }

  switch (tecla)
  {
    case '1':
      despachar(COMIDA, dosis);
      break;

    case '2':
      despachar(AGUA, dosis);
      break;

    case '4':
      if (dosis > 1) dosis--;
      mostrarMenu();
      break;

    case '5':
      if (dosis < 9) dosis++;
      mostrarMenu();
      break;
  }
}