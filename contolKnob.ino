/*
    A rotary encoder publishes MQTT values for:
      - brightness (between 0 and 255)
      - RGB values (r,g,b format)

      for more detail check:
      https://github.com/marcoprovolo/Home-Automation-Devices

      Copyright (C) 2017-2018 by Marco Provolo

      This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

      This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with this program. If not, see http://www.gnu.org/licenses/.

      See also the list of [contributors](https://github.com/your/project/contributors) who participated in this project.

*/

#include <FastLED.h>     // http://fastled.io/   https://github.com/FastLED/FastLED
#include <ESP8266WiFi.h>
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient

/************ WIFI and MQTT INFORMATION ******************/
const char* wifi_ssid = "";          //
const char* wifi_password = "";      //    YOUR PARAMETERS HERE
const char* mqtt_server = "";        //
const char* mqtt_usr = "";   // <- can be left empty if not used
const char* mqtt_psw = "";   // <- can be left empty if not used

/************ PIN DEFINITION ******************/
#define encoderPin1 D7
#define encoderPin2 D5
#define encoderButton D8

#define MAXBrightValue 255
#define MINBrightValue 0
#define MAXColorValue 255
#define MINColorValue 0
static unsigned long debouncingTime = 50; //debouncing time in milliseconds for the knob push

/*********** MQTT TOPICS  **********************/
#define BrightnessPub  "knob/brightness"
#define ColorPub "knob/color"


WiFiClient espClient;             //initialise a wifi client
PubSubClient client(espClient);   //creates a partially initialised client instance
char msg[50];

/*************FASTLED******************/

#define DATA_PIN D1
#define NUM_LEDS 2   // feedback led on device
#define COLOR_ORDER GRB
#define LED_TYPE WS2812B
CRGB leds[NUM_LEDS];



/********INTERRUPT VALUES for ROTARY ENCODER********/

volatile int lastEncoded = 0;
volatile long encoderValue = 0;
volatile int Mode = 0;
volatile unsigned long lastMillis = 0; //debouce the button
volatile int BrightValue = 255; //this is full bright
volatile int ColorValue = 30;
unsigned long StartEncoding = 0; //slow down mqtt publish
static unsigned long PubSlowDown = 150; // publish every X milliseconds

long lastencoderValue = 0;

int lastMSB = 0;
int lastLSB = 0;

int BrightOLD = 0;
int ColorOLD = 30;


/************ SETUP WIFI CONNECT and PRINT IP SERIAL ******************/
void setup_wifi() {
  bool toggle = true;
  fill_solid(leds, NUM_LEDS, CRGB::Aqua); FastLED.show();
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (toggle == false) fill_solid(leds, NUM_LEDS, CRGB::Black);
    else fill_solid(leds, NUM_LEDS, CRGB::Aqua);

    FastLED.show();
    toggle = !toggle;
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  fill_solid(leds, NUM_LEDS, CRGB::White);
  FastLED.show();
  FastLED.delay(1000);

}

/* CALLBACK has to exist BTW it's useless because we are not subscribed to any topic */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    msg[i] = payload[i];
    Serial.print((char)payload[i]);
  }
}


void setup() {
  Serial.begin(115200);
  Serial.println("lest's begin");

  pinMode(encoderPin1, INPUT_PULLUP);
  pinMode(encoderPin2, INPUT_PULLUP);
  pinMode(encoderButton, INPUT_PULLUP);

  attachInterrupt(encoderPin1, updateEncoder, CHANGE);
  attachInterrupt(encoderPin2, updateEncoder, CHANGE);
  attachInterrupt(encoderButton, ModeButton, RISING);

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setDither(0);

  setup_wifi();
  client.setServer(mqtt_server, 1883);  //client is now ready for use
  client.setCallback(callback);


}

void loop() {
  char buf[50];

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // HSV (Rainbow) to RGB color conversion
  CHSV hsv(ColorValue, 255, 255);
  CRGB rgb;
  hsv2rgb_rainbow( hsv, rgb);
  // rgb will now be (0, 0, 255)  -- pure blue as RGB

  if (ColorValue != ColorOLD && (StartEncoding < millis() - PubSlowDown)) {        //print if COLOR has CHANGED
    Serial.print("COLOR encoder value [HSV]: ");
    Serial.println(ColorValue);
    Serial.print("COLOR encoder value [RGB]: ");
    Serial.print("R: ");
    Serial.print(rgb[0]);
    Serial.print(" G: ");
    Serial.print(rgb[1]);
    Serial.print(" B: ");
    Serial.println(rgb[2]);
    Serial.println("publishing RGB value");
    String RGBColor = String(rgb[0]) + "," + String(rgb[1]) + "," + String(rgb[2]);

    RGBColor.toCharArray(buf, 14);
    client.publish(ColorPub, buf);
    ColorOLD = ColorValue;
  }
  if (BrightValue != BrightOLD && (StartEncoding < millis() - PubSlowDown)) {      //print if BRIGHTNESS has CHANGED
    Serial.print("BRIGHTNESS value: ");
    Serial.println(BrightValue);
    String(BrightValue).toCharArray(buf, 4);
    Serial.println("publishing brightness");
    client.publish(BrightnessPub, buf);
    BrightOLD = BrightValue;
  }
}


void reconnect() {
  // Loop until we're reconnected
  //bool toggle = 1;
  while (!client.connected()) {
    fill_solid(leds, NUM_LEDS, CRGB::Orange);
    FastLED.show();
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "imaknob";
    clientId += String(random(0xff), HEX);
    // Attempt to connect
    if (client.connect(clientId, mqtt_usr, mqtt_psw)) {
      Serial.println("connected");
      fill_solid(leds, NUM_LEDS, CRGB::Green);  FastLED.show();
      delay(2000);
      fill_solid(leds, NUM_LEDS, CHSV(0, 0, BrightValue)); FastLED.show();
    } else {
      fill_solid(leds, NUM_LEDS, CRGB::Orange);  FastLED.show();
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


void updateEncoder() {
  int MSB = digitalRead(encoderPin1); //MSB = most significant bit
  int LSB = digitalRead(encoderPin2); //LSB = least significant bit
  StartEncoding = millis();

  int encoded = (MSB << 1) | LSB; //converting the 2 pin value to single number
  int sum  = (lastEncoded << 2) | encoded; //adding it to the previous encoded value
  switch (Mode) {
    case 0: //Brightness
      if ((sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) && BrightValue < MAXBrightValue) BrightValue ++;
      if ((sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) && BrightValue > MINBrightValue) BrightValue --;
      break;
    case 1: //Color
      if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) ColorValue ++;
      if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) ColorValue --;
      if (ColorValue > MAXColorValue) ColorValue = MINColorValue;
      if (ColorValue < MINColorValue) ColorValue = MAXColorValue;
      break;

    default:
      // if nothing else matches, do the default
      Serial.println("out of cases... something went wrong");
      // default is optional
      break;
  }

  displayLEDS();
  lastEncoded = encoded; //store this value for next time
}

void ModeButton() {
  while (digitalRead(encoderButton) == HIGH) {
    //do nothing while button is pressed
    Serial.println("please... relese the button!");
  }
  if (lastMillis < millis() - debouncingTime) {
    Mode++;
    lastMillis = millis();
  }

  if (Mode == 2) Mode = 0;
  Serial.print("Setting mode: ");
  Serial.println(Mode);
  displayLEDS();
}

void displayLEDS() {
  switch (Mode) {
    case 0:
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(0, 0, BrightValue);
      break;
    case 1:
      for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(ColorValue, 255, 255);
      break;
  }
  FastLED.show();
}

/***** END OF FILE *****/
