#include <Servo.h>
#include <AccelStepper.h>

const int PIN_VRX   = A0;
const int PIN_VRY   = A1;
const int PIN_SW    = 2;
const int PIN_VIB   = 3;
const int PIN_SERVO = 9;
const int PIN_LED   = 13;

// IN1=D4, IN2=D5, IN3=D6, IN4=D7 -> orden IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, 4, 6, 5, 7);
Servo servo;

// ajustes de calibracion
const bool INVERTIR_X = false;  // true si izquierda no da 0 grados
const bool INVERTIR_Y = false;  // true si arriba/abajo estan al reves
const int  DIR_GIRO   = 1;      // -1 si arriba gira antihorario

// histeresis: entra a moverse con 80, se detiene bajo 50
const int ZONA_ENTRADA = 80;
const int ZONA_SALIDA  = 50;
const int VEL_MIN = 100;
const int VEL_MAX = 800;

int centroX = 512;
int centroY = 512;
int anguloActual = 90;
bool stepperActivo = false;
bool bloqueado = false;
unsigned long ignorarVibHasta = 0;

volatile bool vibracion = false;

void isrVibracion() {
  vibracion = true;
}

int leerEje(int pin, bool invertir) {
  int v = analogRead(pin);
  return invertir ? 1023 - v : v;
}

int promedio(int pin, bool invertir) {
  long suma = 0;
  for (int i = 0; i < 20; i++) {
    suma += leerEje(pin, invertir);
    delay(5);
  }
  return suma / 20;
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_SW, INPUT_PULLUP);
  pinMode(PIN_VIB, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  servo.attach(PIN_SERVO);
  servo.write(anguloActual);

  stepper.setMaxSpeed(VEL_MAX);
  stepper.setSpeed(0);

  centroX = promedio(PIN_VRX, INVERTIR_X);
  centroY = promedio(PIN_VRY, INVERTIR_Y);

  attachInterrupt(digitalPinToInterrupt(PIN_VIB), isrVibracion, CHANGE);
  ignorarVibHasta = millis() + 500;
  vibracion = false;

  Serial.println("Sistema listo");
}

void loop() {
  if (vibracion) {
    vibracion = false;
    if (!bloqueado && millis() > ignorarVibHasta) bloquear();
  }

  if (bloqueado) {
    if (botonPresionado()) desbloquear();
    return;
  }

  controlarServo();
  controlarStepper();
  stepper.runSpeed();
}

void controlarServo() {
  int x = leerEje(PIN_VRX, INVERTIR_X);
  int angulo;

  if (abs(x - centroX) < ZONA_SALIDA) angulo = 90;
  else if (x < centroX) angulo = map(x, 0, centroX, 0, 90);
  else angulo = map(x, centroX, 1023, 90, 180);

  angulo = constrain(angulo, 0, 180);

  if (angulo != anguloActual && (abs(angulo - anguloActual) >= 2 || angulo % 90 == 0)) {
    anguloActual = angulo;
    servo.write(anguloActual);
  }
}

void controlarStepper() {
  int d = leerEje(PIN_VRY, INVERTIR_Y) - centroY;
  int m = abs(d);

  if (stepperActivo && m < ZONA_SALIDA) stepperActivo = false;
  else if (!stepperActivo && m > ZONA_ENTRADA) stepperActivo = true;

  if (!stepperActivo) {
    stepper.setSpeed(0);
    return;
  }

  int limite = (d > 0) ? 1023 - centroY : centroY;
  int vel = map(m, ZONA_SALIDA, limite, VEL_MIN, VEL_MAX);
  vel = constrain(vel, VEL_MIN, VEL_MAX);

  int sentido = (d > 0) ? 1 : -1;
  stepper.setSpeed(vel * sentido * DIR_GIRO);
}

void bloquear() {
  bloqueado = true;
  stepperActivo = false;
  stepper.setSpeed(0);
  stepper.disableOutputs();
  digitalWrite(PIN_LED, HIGH);
  Serial.println("BLOQUEADO: vibracion detectada");
}

void desbloquear() {
  bloqueado = false;
  stepper.enableOutputs();
  digitalWrite(PIN_LED, LOW);
  ignorarVibHasta = millis() + 500;
  vibracion = false;
  Serial.println("Sistema reactivado");
}

bool botonPresionado() {
  if (digitalRead(PIN_SW) == LOW) {
    delay(30);
    if (digitalRead(PIN_SW) == LOW) {
      while (digitalRead(PIN_SW) == LOW);
      return true;
    }
  }
  return false;
}