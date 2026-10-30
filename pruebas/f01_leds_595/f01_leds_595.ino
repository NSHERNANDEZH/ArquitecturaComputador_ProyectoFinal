
/*
 Prueba Feature 01 - LEDs en el 74HC595

 Verifica que encender y apagar LEDs (Q4-Q7)
 nunca modifique las filas del teclado (Q0-Q3).

 Monitor Serial a 9600 baudios.
*/

const byte DATA  = 7;
const byte CLOCK = 8;
const byte LATCH = 9;

const byte MASCARA_LEDS  = B11110000;
const byte MASCARA_FILAS = B00001111;

const char *nombres[4] = {"Verde", "Rojo", "Amarillo", "Azul"};

byte registro = B00001111;
byte errores = 0;


void actualizarRegistro()
{
  digitalWrite(LATCH, LOW);
  shiftOut(DATA, CLOCK, MSBFIRST, registro);
  digitalWrite(LATCH, HIGH);
}


void apagarLEDs()
{
  registro &= ~MASCARA_LEDS;
  actualizarRegistro();
}


void encenderLED(byte numero)
{
  if (numero > 3) return;
  registro &= ~MASCARA_LEDS;
  bitSet(registro, numero + 4);
  actualizarRegistro();
}


void imprimirRegistro()
{
  for (int bit = 7; bit >= 0; bit--)
  {
    Serial.print(bitRead(registro, bit));
    if (bit == 4) Serial.print(' ');
  }
}


void verificar(const char *paso, byte ledsEsperados)
{
  bool filasOk = (registro & MASCARA_FILAS) == MASCARA_FILAS;
  bool ledsOk  = (registro & MASCARA_LEDS) == ledsEsperados;

  Serial.print(paso);
  Serial.print("  registro = ");
  imprimirRegistro();
  Serial.println(filasOk && ledsOk ? "  OK" : "  ERROR");

  if (!(filasOk && ledsOk)) errores++;
}


void setup()
{
  Serial.begin(9600);

  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  actualizarRegistro();

  Serial.println("=== Prueba LEDs 74HC595 ===");
  Serial.println("Formato: Q7..Q4 Q3..Q0");
  verificar("Inicio     ", B00000000);

  for (byte led = 0; led < 4; led++)
  {
    encenderLED(led);
    char paso[12];
    snprintf(paso, sizeof(paso), "%-11s", nombres[led]);
    verificar(paso, 1 << (led + 4));
    delay(1000);
  }

  apagarLEDs();
  verificar("Apagar     ", B00000000);

  encenderLED(7);
  verificar("LED inval. ", B00000000);

  Serial.println(errores == 0 ? "=== TODO OK ===" : "=== HAY ERRORES ===");
}


void loop()
{
  for (byte led = 0; led < 4; led++)
  {
    encenderLED(led);
    delay(250);
  }

  for (int led = 2; led > 0; led--)
  {
    encenderLED(led);
    delay(250);
  }
}