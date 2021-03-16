/*******************************************************************************************
 * Software per ESP-01 montato su Roomba 606
 * Utilizzo di due feed Adafruit, RoombaCtrl e RoombaData, per il controllo ed i dati
 * forniti dall'ESP.
 * 
 * Aggiornato al 14/03/2021  
 * 
 * Part list:
 *  1 x Roomba 606
 *  1 x MP1584EN Buck converter set to 3.3V
 *  1 x 3906 PNP transistor
 *  1 x ESP-01
 * 
 * Cablaggio:
 *  DEVICE  DIR N NAME  ->  DEVICE  DIR N NAME
 * 
 *  Roomba  OUT 1 VPWR  ->  Buck    IN  1 VPWR
 *  Roomba  OUT 6 VGND  ->  Buck    IN  3 VGND
 *  Buck    OUT 4 BPWR  ->  ESP-01  IN  8 VCC
 *  Buck    OUT 4 BPWR  ->  ESP-01  IN  4 Ch_EN
 *  Buck    OUT 6 BGND  ->  ESP-01  IN  1 GND
 *  Buck    OUT 6 BGND  ->  PNP     IN  2 BASE
 *  Roomba  OUT 4 TXD   ->  PNP     IN  2 TXD
 *  PNP     OUT 1 EMIT  ->  ESP-01  IN  7 RXD
 *  ESP-01  OUT 3 COLL  ->  Roomba  IN  3 RXD
 *  ESP-01  OUT 5 GPIO0 ->  Roomba  IN  5 BRC
 *  
 * Written by Luca Mainardi
 ******************************************************************************************/
/* Librerie *******************************************************************************/
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <ESP8266WiFi.h>
#include <SimpleTimer.h>

/* Parametri di connessione ***************************************************************/
#define WLAN_SSID       "***"             // Nome WiFi
#define WLAN_PASS       "***"             // Password WiFi
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883              // 1883 per SSL
#define AIO_USERNAME    "***"             // Username Adafruit
#define AIO_KEY         "***"             // IO key Adafruit

/* Comandi Roomba *************************************************************************/
#define RESET           7
#define START           128
#define BAUD            129 // [129]      [Baud Code]
#define SAFE            131
#define POWER           133
#define SPOT            134
#define CLEAN           135
#define MAX             136
#define SEEK_DOCK       143
#define QUERY_LIST      149 // [149]      [N° Pak]     [Pak ID 1] [Pak ID 2] ...
/*#define SCHEDULE        167 // [167]      [Days]       [Sun Hour] [Sun Minute]
                            // [Mon Hour] [Mon Minute] [Tue Hour] [Tue Minute]
                            // [Wed Hour] [Wed Minute] [Thu Hour] [Thu Minute]
                            // [Fri Hour] [Fri Minute] [Sat Hour] [Sat Minute]
#define SET_TIME        168 // [168]      [Day]        [Hour]     [Minute]*/

/* Stati Roomba ***************************************************************************/
#define STATE_UNKNOWN   0
#define STATE_CLEANING  1
#define STATE_RETURNING 2
#define STATE_DOCKED    3
#define STATE_IDLE      4

/* Stati carica ***************************************************************************/
#define CHARGE_NONE             0 // Not charging
#define CHARGE_RECONDITIONING   1 // Reconditioning
#define CHARGE_BULK             2 // Full Charging
#define CHARGE_TRICKLE          3 // Trickle Charging
#define CHARGE_WAITING          4 // Waiting
#define CHARGE_FAULT            5 // Charging Fault Condition

/* Miscellanee Roomba *********************************************************************/
#define STAY_AWAKE_PIN          0     // Pin al BRC
#define STAY_AWAKE_TIMEOUT      30000
#define GET_SENSORS_TIMEOUT     200000
#define RETURN_BATTERY_PERCENT  10    // % sotto la quale il Roomba torna alla dock

/* Variabile globale myRoomba come struct con tutto riguardo il Roomba dentro *************/
struct RoombaState {
  uint8_t   state;
  uint8_t   chargeState;
  uint16_t  batteryVoltage;
  int16_t   batteryCurrent;
  uint8_t   batteryTemp;
  uint16_t  batteryCharge;
  uint16_t  batteryCapacity;
  uint8_t   batteryPercent;
};
struct RoombaState myRoomba;

/* Dichiarazione WiFi, MQTT, timer, server NTP ********************************************/
WiFiClient client;

Adafruit_MQTT_Client    mqtt (&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe roombaSub = Adafruit_MQTT_Subscribe (&mqtt, AIO_USERNAME "***");
Adafruit_MQTT_Publish   roombaPub = Adafruit_MQTT_Publish   (&mqtt, AIO_USERNAME "***");

SimpleTimer stayAwakeTimer;
SimpleTimer getSensorsTimer;

/* Prototipi delle funzioni ***************************************************************/
void MQTTConnect();

void startCleaning();
void startMax();
void startSpot();
void stopCleaning();
void seekDock();
void roombaReset();

void stayAwake();
void getSensors();
void sendState();

void blinkSlow(int n);
void blinkFast(int n);

String translateState();
String translateCharge();

/* Setup **********************************************************************************/
void setup() {
  pinMode(STAY_AWAKE_PIN, OUTPUT);
  digitalWrite(STAY_AWAKE_PIN, HIGH);
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  roombaReset();
  
  WiFi.begin(WLAN_SSID, WLAN_PASS);   
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  mqtt.subscribe(&roombaSub);
  
  stayAwakeTimer.setInterval(STAY_AWAKE_TIMEOUT);
  getSensorsTimer.setInterval(GET_SENSORS_TIMEOUT);

  myRoomba.state = STATE_UNKNOWN;
}

/* Loop ***********************************************************************************/
void loop() {
  MQTTConnect();
  Adafruit_MQTT_Subscribe *subscription;
  
  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &roombaSub) {
      digitalWrite(0, (int)roombaSub.lastread);
      
      if(!strcmp((char *)roombaSub.lastread, "clean"))  startCleaning();
      if(!strcmp((char *)roombaSub.lastread, "max"))    startMax();
      if(!strcmp((char *)roombaSub.lastread, "spot"))   startSpot();
      if(!strcmp((char *)roombaSub.lastread, "stop"))   stopCleaning();
      if(!strcmp((char *)roombaSub.lastread, "dock"))   seekDock();
      if(!strcmp((char *)roombaSub.lastread, "reset"))  roombaReset();
    }
  }

  if (getSensorsTimer.isReady()) {
    getSensors();
    getSensorsTimer.reset();
  }
  
  if (stayAwakeTimer.isReady()) {
    stayAwake();
    stayAwakeTimer.reset();
  }
  
  if(! mqtt.ping()) {
    mqtt.disconnect();
  }
}

void MQTTConnect() {
  int8_t ret;

  if (mqtt.connected()) {
    return;
  }

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) {
    mqtt.disconnect();
    delay(5000);
    retries--;
    if (retries == 0) {
      while (1);
    }
  }
}

/* Nei comandi di pulizia sono presenti le condizioni:
 *  Ignora se il Roomba è alla dock e non ha finito la carica
 *  Ignora se il Roomba sta rientrando alla dock ed è sotto il valore di carica minima
 *  Ignora se il Roomba sta già pulendo
 */
void startCleaning() {
  if (myRoomba.state == STATE_DOCKED    && myRoomba.chargeState != CHARGE_WAITING)            return;
  if (myRoomba.state == STATE_RETURNING && myRoomba.batteryPercent <= RETURN_BATTERY_PERCENT) return;
  if (myRoomba.state == STATE_CLEANING  && myRoomba.batteryCurrent < -300)                    return;
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  Serial.write(CLEAN);
  delay(25);
  myRoomba.state = STATE_CLEANING;
  blinkSlow(1);
  getSensors();
}

void startMax() {
  if (myRoomba.state == STATE_DOCKED    && myRoomba.chargeState != CHARGE_WAITING)            return;
  if (myRoomba.state == STATE_RETURNING && myRoomba.batteryPercent <= RETURN_BATTERY_PERCENT) return;
  if (myRoomba.state == STATE_CLEANING  && myRoomba.batteryCurrent < -300)                    return;
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  Serial.write(MAX);
  delay(25);
  myRoomba.state = STATE_CLEANING;
  blinkSlow(1);
  getSensors();
}

void startSpot() {
  if (myRoomba.state == STATE_DOCKED    && myRoomba.chargeState != CHARGE_WAITING)            return;
  if (myRoomba.state == STATE_RETURNING && myRoomba.batteryPercent <= RETURN_BATTERY_PERCENT) return;
  if (myRoomba.state == STATE_CLEANING  && myRoomba.batteryCurrent < -300)                    return;
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  Serial.write(SPOT);
  delay(25);
  myRoomba.state = STATE_CLEANING;
  blinkSlow(1);
  getSensors();
}

void stopCleaning() {
  // Ignora se il Roomba è alla dock
  if (myRoomba.state == STATE_DOCKED) return;
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  Serial.write(POWER);
  delay(25);
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  myRoomba.state = STATE_IDLE;
  blinkSlow(1);
  getSensors();
}

void seekDock() {
  if (myRoomba.state == STATE_CLEANING) {
    Serial.write(START);
    delay(25);
    Serial.write(SAFE);
    delay(25);
    Serial.write(SEEK_DOCK);
    delay(500);
  }
  Serial.write(START);
  delay(25);
  Serial.write(SAFE);
  delay(25);
  Serial.write(SEEK_DOCK);
  delay(25);
  myRoomba.state = STATE_RETURNING;
  blinkSlow(1);
  getSensors();
}

void roombaReset() {
  Serial.begin(19200);
  delay(25);
  Serial.write(START);
  delay(25);
  Serial.write(BAUD);
  delay(25);
  Serial.write(11);
  delay(25);
  Serial.write(RESET);
  delay(100);

  Serial.begin(115200);
  delay(25);
  Serial.write(START);
  delay(25);
  Serial.write(BAUD);
  delay(25);
  Serial.write(11);
  delay(25);
  Serial.write(RESET);
  delay(100);
}

void stayAwake() {
  digitalWrite(STAY_AWAKE_PIN, LOW);
  delay(100);
  digitalWrite(STAY_AWAKE_PIN, HIGH);
}

void getSensors() {
  char buffer[10];

  // Pulizia buffer in lettura
  int i = 0;
  while ( Serial.available() > 0 ) {
    Serial.read();
    i++;
    delay(1);
  }

  // Richiesta del gruppo sensori 3:
  // 21 (1 byte) - chargeState
  // 22 (2 byte) - batteryVoltage
  // 23 (2 byte) - batteryCurrent
  // 24 (1 byte) - batteryTemp
  // 25 (2 byte) - batteryCharge
  // 26 (2 byte) - batteryCapacity
  Serial.write(START);
  delay(50);
  Serial.write(QUERY_LIST);
  delay(50);
  Serial.write(1);
  delay(50);
  Serial.write(3);
  delay(50);

  // Lettura risposta
  i = 0;
  while ( Serial.available() > 0) {
    buffer[i] = Serial.read();
    i++;
    delay(1);
  }
  
  // Controllo errore lunghezza
  if ( i != 10 ) {
    myRoomba.state = STATE_UNKNOWN;
    return;
  }

  myRoomba.chargeState      = buffer[0];
  myRoomba.batteryVoltage   = (uint16_t)word(buffer[1], buffer[2]);
  myRoomba.batteryCurrent   = (int16_t)word(buffer[3], buffer[4]);
  myRoomba.batteryTemp      = buffer[5];
  myRoomba.batteryCharge    = (uint16_t)word(buffer[6], buffer[7]);
  myRoomba.batteryCapacity  = (uint16_t)word(buffer[8], buffer[9]);

  /*myRoomba.chargeState      = 3;
  myRoomba.batteryVoltage   = 15000;
  myRoomba.batteryCurrent   = 200;
  myRoomba.batteryTemp      = 20;
  myRoomba.batteryCharge    = 3500;
  myRoomba.batteryCapacity  = 5000;*/
  
  // Controllo errori valori
  if (myRoomba.chargeState > 5)        return; // Valori tra 0 e 5
  if (myRoomba.batteryCapacity == 0)   return; // Non dovrebbe capitare mai, ma non si rischia di dividere per 0
  if (myRoomba.batteryCapacity > 6000) return; // Normalmente attorno a 2050
  if (myRoomba.batteryCharge > 6000)   return; // Non può essere maggiore di batteryCapacity
  if (myRoomba.batteryVoltage > 18000) return; // Dovrebbe essere circa 17V in carica, ~13.1V quando scarico
  
  // Calcolo percentuale batteria
  uint8_t newBatteryPercent = 100 * myRoomba.batteryCharge / myRoomba.batteryCapacity;
  if (newBatteryPercent > 100) return;
  myRoomba.batteryPercent = newBatteryPercent;

  // Se il Roomba è in carica, allora è alla dock
  if (myRoomba.chargeState >= CHARGE_RECONDITIONING && myRoomba.chargeState <= CHARGE_WAITING) {
    if (myRoomba.state != STATE_CLEANING) {
      myRoomba.state = STATE_DOCKED;
    }
  }
  
  // Se la batteria è troppo bassa ed il Roomba sta ancora pulendo, lo si fa tornare alla dock
  if (myRoomba.state = STATE_CLEANING && myRoomba.batteryPercent <= RETURN_BATTERY_PERCENT) {
    seekDock();
  }
  
  sendState();
}

void sendState() {
  String message  = translateState()          + ", "      +
                    translateCharge()         + ", "      +
                    myRoomba.batteryVoltage   + " mV, "   +
                    myRoomba.batteryCurrent   + " mA, "   +
                    myRoomba.batteryTemp      + " °C, "   +
                    myRoomba.batteryPercent   + " % ("    +
                    myRoomba.batteryCharge    + " mAh / " +
                    myRoomba.batteryCapacity  + " mAh)";
  
  if (!roombaPub.publish(message.c_str())) {
    myRoomba.state = STATE_UNKNOWN;
  }

  blinkFast(3);
}

void blinkSlow(int n) {
  for(int i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
  }
}

void blinkFast(int n) {
  for(int i=0; i<n; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

String translateState(){
  String stateStr;
  switch (myRoomba.state) {
    case STATE_UNKNOWN:   stateStr = "Errore di stato";   break;
    case STATE_CLEANING:  stateStr = "Pulizia";           break;
    case STATE_RETURNING: stateStr = "Ritorno alla base"; break;
    case STATE_DOCKED:    stateStr = "Alla base";         break;
    case STATE_IDLE:      stateStr = "Fermo";             break;
  }
  return stateStr;
}

String translateCharge() {
  String chargeStateStr;
  switch (myRoomba.chargeState) {
    case CHARGE_NONE:           chargeStateStr = "Non in carica";             break;
    case CHARGE_RECONDITIONING: chargeStateStr = "Ricondizionamento";         break;
    case CHARGE_BULK:           chargeStateStr = "In carica";                 break;
    case CHARGE_TRICKLE:        chargeStateStr = "Mantenimento della carica"; break;
    case CHARGE_WAITING:        chargeStateStr = "Carico";                    break;
    case CHARGE_FAULT:          chargeStateStr = "Errore di carica";          break;
  }
  return chargeStateStr;
}
