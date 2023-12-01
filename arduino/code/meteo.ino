/*
 Basic ESP8266 MQTT example
 This sketch demonstrates the capabilities of the pubsub library in combination
 with the ESP8266 board/library.
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off
 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.
 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"
*/

#include <ESP8266WiFi.h>
//
// Pubsublibrary : https://github.com/knolleary/pubsubclient
// A ajouter dans le gestionnaire de bibs de l'ide ino
#include <PubSubClient.h>
#include <arduino_secrets.h>
//
// Connexion wifi
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
//
// Connexion MQTT
const char* mqtt_server = "permarduino";
const int mqtt_port = 1883;
const char* mqtt_topic = "meteo";

//
// ------------------------------------------------------------------------
// FREQUENCES DE GESTION
// ------------------------------------------------------------------------
// Frequence d'emission des messages mqtt
const int MQTT_FREQ = 60000;
// Frequence de prise de mesures
const int MES_FREQ = 10000;
const int NB_MES = 6;
//
// ------------------------------------------------------------------------
// Definition des compteurs.
// Pas d'utilisation de struct dans un premier temps
// Fonctionnement a base de tableaux
// ------------------------------------------------------------------------
const int NB_CAPTEURS = 4;
//
const int I_ANEM = 0;
const int I_PLUV = 1;
const int I_GIRO = 2;
const int I_TEMP = 3;
//
// ---------------------------
const int PIN_ANEM = D1;
const int PIN_PLUV = D3;
const int PIN_GIRO = D2;
const int PIN_TEMP = 3;
//
//
// ------------------------------------------------------------------------
// Variables globales capteurs et mesures
// ------------------------------------------------------------------------
int cptPinNo[NB_CAPTEURS];
char* cptName[NB_CAPTEURS];
int cptDebounceDelay[NB_CAPTEURS];
int sensorState[NB_CAPTEURS];
int sensorStateMillis[NB_CAPTEURS];
int cptCurentValue[NB_CAPTEURS];
//
int measurementMillis[NB_CAPTEURS][NB_MES];
int measurementValue[NB_CAPTEURS][NB_MES];
int currentMeasurementIndex = 0;
int lastMeasurementMillis = 0;
//
int lastMqttPublishMillis = 0;
int mqttPublishIndex = 0
//
// -----------------------------------------------------------------------
// Variable globale objet client mqtt et message
// -----------------------------------------------------------------------
WiFiClient espClient;
PubSubClient client(espClient);
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];

//
// ------------------------------------------------------------------------
// FONCTIONS WIFI ET MQTT
// ------------------------------------------------------------------------
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("meteo", "hello world");
      snprintf (msg, MSG_BUFFER_SIZE, "MODU|%d|HELLO", millis());
      client.publish("meteo", msg);
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//
// ------------------------------------------------------------------------
// FONCTIONS GESTION DES MESURES
// ------------------------------------------------------------------------
void send_mesure( int sensorIndex, int noMes) {
    snprintf (msg, MSG_BUFFER_SIZE, "%s|%d|%d|%d|%d", cptName[sensorIndex], mqttPublishIndex, noMes, measurementMillis[sensorIndex][noMes], measurementValue[sensorIndex][noMes]);
    client.publish("meteo", msg);
    Serial.println(msg);
}

void store_mesure( int sensorIndex, int noMes ) {
    measurementMillis[sensorIndex][noMes]= millis();
    measurementValue[sensorIndex][noMes] = cptCurentValue[sensorIndex];
}

void store_mesures() {
    int cpt = 0;
    for (cpt = 0; cpt < NB_CAPTEURS; cpt = cpt + 1) {
        store_mesure( cpt, currentMeasurementIndex );
    }
}

void send_mesures() {
    int cpt = 0;
    int mes = 0;
    for (cpt = 0; cpt < NB_CAPTEURS; cpt = cpt + 1) {
        for (mes = 0; mes < NB_MES; mes = mes + 1) {
            send_mesure( cpt, mes );
        }
    }
    ++mqttPublishIndex;
}

//
// ------------------------------------------------------------------------
// FONCTIONS PRISE DES MESURES
// ------------------------------------------------------------------------
void lire_compteur( int sensorIndex ) {
    int readingTime = millis();
    int readValue = digitalRead(cptPinNo[sensorIndex]);
    if ( readValue != sensorState[sensorIndex] ) {
        if ( readingTime - sensorStateMillis[sensorIndex] > cptDebounceDelay[sensorIndex] ) {
            // On a bien passe le delai, on change d'etat
            sensorState[sensorIndex] = readValue;
            sensorStateMillis[sensorIndex] = readingTime;
            if (readValue == HIGH) {
                cptCurentValue[sensorIndex] ++;
                Serial.println("tour compteur");
            }
        }
    }
}

void lire_analogique( int sensorIndex ) {
    int readingTime = millis();
    digitalWrite(cptPinNo[sensorIndex], HIGH);
    delay(1);
    sensorStateMillis[sensorIndex] = readingTime;
    cptCurentValue[sensorIndex] = analogRead(A0);
    digitalWrite(cptPinNo[sensorIndex], LOW);
}

void setup_capteurs() {
    pinMode(PIN_ANEM, INPUT);
    pinMode(PIN_PLUV, INPUT);
    pinMode(PIN_GIRO, OUTPUT);
    pinMode(PIN_TEMP, OUTPUT);
    //
    cptPinNo[I_ANEM] = PIN_ANEM;
    cptName[I_ANEM] = "ANEM";
    cptDebounceDelay[I_ANEM] = 20;
    sensorState[I_ANEM]= LOW;
    sensorStateMillis[I_ANEM] = 0;
    //
    cptPinNo[I_PLUV] = PIN_PLUV;
    cptName[I_PLUV] = "PLUV";
    cptDebounceDelay[I_PLUV] = 50;
    sensorState[I_PLUV]= LOW;
    sensorStateMillis[I_PLUV] = 0;
    //
    cptPinNo[I_GIRO] = PIN_GIRO;
    cptName[I_GIRO] = "GIRO";
    cptDebounceDelay[I_GIRO] = 0;
    sensorState[I_GIRO]= LOW;
    sensorStateMillis[I_GIRO] = 0;
    //
    //
    cptPinNo[I_TEMP] = PIN_TEMP;
    cptName[I_TEMP] = "TEMP";
    cptDebounceDelay[I_TEMP] = 0;
    sensorState[I_TEMP]= LOW;
    sensorStateMillis[I_TEMP] = 0;
}


//
// ------------------------------------------------------------------------
// STD ARDUINO
// ------------------------------------------------------------------------
void setup() {
  setup_capteurs();
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  lire_compteur(I_PLUV);
  lire_compteur(I_ANEM);
  unsigned long now = millis();
  if (now - lastMeasurementMillis > MES_FREQ) {
    lastMeasurementMillis = now;
    lire_analogique(I_GIRO);
    lire_analogique(I_TEMP);
    store_mesures();
    ++currentMeasurementIndex;
  }
  if (now - lastMqttPublishMillis > MQTT_FREQ) {
    lastMqttPublishMillis = now;
    send_mesures();
    currentMeasurementIndex = 0;
  }
}

