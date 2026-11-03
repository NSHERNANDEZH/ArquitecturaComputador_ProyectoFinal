/*
 Prueba Feature 02 - Teclado 4x2 y captura de PIN

 Usa las mismas funciones del firmware: seleccionarFila(),
 leerTeclado() sin bloqueo y leerPIN() con asteriscos.

 - Cada tecla se imprime una sola vez en el Monitor Serial (9600).
 - '*' borra el último dígito.
 - Al completar 4 dígitos compara con el PIN de prueba 1245.
 - El LED amarillo parpadea todo el tiempo: si se detiene mientras
   se escribe, la lectura está bloqueando el loop().
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

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

const char PIN_PRUEBA[] = "1245";

const byte MASCARA_LEDS = B11110000;
const byte LED_VERDE    = 0;
const byte LED_ROJO     = 1;
const byte BIT_AMARILLO = 6;

LiquidCrystal_I2C lcd(0x27, 16, 2);

byte registro = B00001111;


// ---------------- 74HC595 y LEDs ----------------

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


// ---------------- Teclado (igual que el firmware) ----------------

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

  if (lectura)
  {
    Serial.print(F("Tecla: "));
    Serial.println(lectura);
  }

  return lectura;
}


// ---------------- PIN (igual que el firmware) ----------------

const byte LARGO_PIN   = 4;
const byte COLUMNA_PIN = 12;

char pinIngresado[LARGO_PIN + 1] = "";
byte posicionPIN = 0;

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


// ---------------- Prueba ----------------

enum Estado { PIDIENDO, RESULTADO };

Estado estado = PIDIENDO;
unsigned long inicioResultado = 0;
unsigned long ultimoParpadeo = 0;
byte correctos = 0;
byte incorrectos = 0;

void pedirPIN()
{
  posicionPIN = 0;
  pinIngresado[0] = '\0';

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PRUEBA F02");
  lcd.setCursor(0, 1);
  lcd.print("INGRESE PIN");

  estado = PIDIENDO;
}

void mostrarResultado()
{
  bool ok = strcmp(pinIngresado, PIN_PRUEBA) == 0;

  Serial.print(F("PIN ingresado: "));
  Serial.print(pinIngresado);
  Serial.println(ok ? F("  -> CORRECTO") : F("  -> INCORRECTO"));

  if (ok) correctos++; else incorrectos++;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ok ? "PIN CORRECTO" : "PIN INCORRECTO");

  char linea[17];
  snprintf(linea, sizeof(linea), "OK:%u  MAL:%u", correctos, incorrectos);
  lcd.setCursor(0, 1);
  lcd.print(linea);

  encenderLED(ok ? LED_VERDE : LED_ROJO);

  estado = RESULTADO;
  inicioResultado = millis();
}

void parpadearAmarillo()
{
  if (millis() - ultimoParpadeo < 500) return;

  ultimoParpadeo = millis();

  if (estado == PIDIENDO)
  {
    registro ^= (1 << BIT_AMARILLO);
    actualizarRegistro();
  }
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

  Serial.println(F("=== Prueba F02: teclado y PIN ==="));
  Serial.println(F("PIN de prueba: 1245. '*' borra."));

  pedirPIN();
}

void loop()
{
  parpadearAmarillo();

  switch (estado)
  {
    case PIDIENDO:
      if (leerPIN())
      {
        mostrarResultado();
      }
      break;

    case RESULTADO:
      if (millis() - inicioResultado >= 2000)
      {
        apagarLEDs();
        pedirPIN();
      }
      break;
  }
}