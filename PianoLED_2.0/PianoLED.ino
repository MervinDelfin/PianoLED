// PIANO LED 2.0
// Nicolás de Ory 2017-2018
// Programa que funciona con la tira de LEDs WS2812B y el teclado Yamaha CLP320
// Works with the WS2812B LED strip and the Yamaha CLP320 keyboard

// Modified by Marvin Ruciński in 2021
// Works with Yamaha CLP-745 Digital Piano

// Added custom MIDI protocol in 2024 by Marvin Ruciński

#include <MIDI.h>
#include "FastLED.h"

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include "RemoteDebug.h" //https://github.com/JoaoLopesF/RemoteDebug

#include <EEPROM.h>
#define EEPROM_SIZE 1
#define AUTO_RESTORE_LAST_MODE true

RemoteDebug Debug;

ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

#define clkPin 12 // A
#define dtPin 13  // B
#define btnPin 14

int encoderValues = 0;
int idx = 0;

bool autoModeOn = true;
byte autoMode = 6;
unsigned long lastKeyPress;
#define SLEEP_TIMER 10000 // 20

FASTLED_USING_NAMESPACE

#define button_pin 5
#define POTENTIOMETER_PIN A0

#define DATA_PIN 2
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS 88
#define FIRST_KEY 21

#define PEDAL_STRENGTH 10    // 30
#define NO_PEDAL_STRENGTH 60 // 150//60
#define NOTE_HOLD_FADE 5

byte mode = 1;
CRGB leds[NUM_LEDS];
boolean onLeds[NUM_LEDS];
boolean fadeLeds[NUM_LEDS];
boolean doNotFade[NUM_LEDS];

CRGB rainbowPalette[NUM_LEDS];

int currAnalogRead;

#define MIN_BRIGHTNESS 130
#define BRIGHTNESS 255 // 90
#define PASSIVE_FPS 120

// MODE 5 PALETTES

DEFINE_GRADIENT_PALETTE(GPVelocidad){
    0, 54, 67, 255,
    50, 54, 184, 255,
    100, 54, 255, 161,
    145, 54, 255, 60,
    235, 10, 255, 10,
    255, 9, 255, 9};

DEFINE_GRADIENT_PALETTE(GPHot){
    0, 200, 0, 0,
    55, 255, 0, 0,
    190, 255, 255, 0,
    255, 255, 255, 255};

// MODE 4 PALETTES

DEFINE_GRADIENT_PALETTE(GP_Rainbow){
    0, 255, 0, 0,
    43, 255, 102, 0,
    81, 255, 204, 0,
    122, 123, 255, 0,
    220, 39, 230, 160,
    230, 0, 255, 255,
    240, 0, 153, 255,
    255, 0, 153, 255};

DEFINE_GRADIENT_PALETTE(GP_Tropical){
    40, 0, 176, 155,
    215, 150, 201, 6,
    255, 150, 202, 6};

DEFINE_GRADIENT_PALETTE(GP_PinkChampagne){
    0, 169, 25, 37,
    127, 182, 117, 149,
    255, 19, 117, 147};

DEFINE_GRADIENT_PALETTE(GP_Emerald){
    0, 79, 142, 7,
    73, 88, 196, 7,
    126, 88, 195, 7,
    150, 206, 237, 138,
    255, 213, 233, 158};

DEFINE_GRADIENT_PALETTE(GP_JewelDragon){
    0, 11, 7, 13,
    35, 43, 20, 40,
    58, 91, 63, 95,
    81, 139, 88, 95,
    104, 140, 91, 87,
    127, 184, 104, 127,
    150, 215, 96, 83,
    173, 232, 97, 59,
    196, 208, 78, 27,
    219, 182, 42, 10,
    237, 150, 25, 3,
    255, 36, 5, 1};

DEFINE_GRADIENT_PALETTE(GP_FreshBlue){
    0, 52, 150, 80,
    60, 52, 162, 102,
    132, 11, 111, 138,
    200, 75, 239, 242,
    255, 75, 238, 220};

#define MODE4_PALETTE_COUNT 7

CRGBPalette16 mode4Palettes[] = {
    GP_Rainbow,
    GP_Tropical,
    GP_PinkChampagne,
    GP_Emerald,
    GP_JewelDragon,
    GP_FreshBlue};

#define MODE5_PALETTE_COUNT 2

CRGBPalette16 mode5Palettes[] = {
    GPVelocidad,
    GPHot};

uint8_t gCurrentPatternNumber = 0; // Index number of which animation is current
uint8_t gHue = 0;                  // rotating "base color" used by some animations
uint8_t customHue = 0;
uint8_t customSaturation = 255;

uint8_t mode4PalIndex;
uint8_t mode5PalIndex;

struct MidiSettings : public midi::DefaultSettings // The sketch will probably work fine without these custom settings.
{
  static const bool UseRunningStatus = true;
  static const unsigned SysExMaxSize = 2;
  static const long BaudRate = 31250;
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, MidiSettings);
byte sustain;
bool DONT_FADE_NOTES = false;

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  lastKeyPress = millis();
  debugI("Note on: %s, velocity: %s, channel: %s", String(pitch).c_str(), String(velocity).c_str(), String(channel).c_str());
  byte pitchcheck = constrain(pitch, 21, 108);
  byte velocitycheck = constrain(velocity, 45, 100); // constrain(velocity,0,127);
  // byte ld = map(pitchcheck,21,109,NUM_LEDS-1,-1);
  byte ld = -pitchcheck + NUM_LEDS + 21 - 1;
  // debugI("Mapped: %s -> %s", String(pitchcheck).c_str(), String(ld).c_str());
  // byte ld = map(pitchcheck,21,108,0,NUM_LEDS-1);
  onLeds[ld] = true;
  if (autoModeOn & (mode == 1 || mode == 0))
  {
    mode = autoMode;
  }
  autoModeOn = false;
  switch (mode)
  {
  case 2:                                                                              // FIXED COLOR
    leds[ld].setHSV(customHue, 255, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255)); // 150
    break;
  case 3:                                                                              // FIXED COLOR - less saturation
    leds[ld].setHSV(customHue, 150, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255)); // 60
    break;
  case 4: // PALETTE
    if (mode4PalIndex == 0)
    {
      leds[ld] = rainbowPalette[ld];
      // leds[ld].setHSV(rgb2hsv_approximate(rainbowPalette[ld]).hue, rgb2hsv_approximate(rainbowPalette[ld]).sat, map(velocitycheck,45,100,MIN_BRIGHTNESS,255));
      leds[ld].fadeLightBy(255 - map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255));
    }
    else
    {
      leds[ld] = ColorFromPalette(mode4Palettes[mode4PalIndex - 1], map(pitchcheck, 21, 108, 0, 240));
      // leds[ld].setHSV(rgb2hsv_approximate(leds[ld]).hue, rgb2hsv_approximate(leds[ld]).sat, map(velocitycheck,45,100,MIN_BRIGHTNESS,255));
      leds[ld].fadeLightBy(255 - map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255));
    }
    break;
  case 5: // VELOCITY
    leds[ld] = ColorFromPalette(mode5Palettes[mode5PalIndex], map(velocitycheck, 45, 100, 0, 240));
    break;
  case 6: // ROTATING HUE
    leds[ld].setHSV(gHue, customSaturation, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255));
    break;
  case 7: // FADE AROUND NOTE
    int x = 1;
    leds[ld].setHSV(customHue, 255, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255)); // 115
    for (int i = ld + 1; i < NUM_LEDS; i++)
    {
      if (onLeds[i])
        continue;
      if (map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255) - x * 45 <= 100)
        break;
      leds[i].setHSV(customHue, 250, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255) - x * 45); // 110
      fadeLeds[i] = true;
      x++;
    }
    x = 1;
    for (int i = ld - 1; i >= 0; i--)
    {
      if (onLeds[i])
        continue;
      if (map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255) - x * 45 <= 100)
        break;
      leds[i].setHSV(customHue, 250, map(velocitycheck, 45, 100, MIN_BRIGHTNESS, 255) - x * 45); // 110
      fadeLeds[i] = true;
      x++;
    }
    break;
  }
}

void OnNoteOff(byte channel, byte pitch, byte velocity)
{
  byte pitchcheck2 = constrain(pitch, 21, 108);
  // byte led = map(pitchcheck2,21,108,0,NUM_LEDS-1);
  byte led = -pitchcheck2 + NUM_LEDS + 21 - 1;
  onLeds[led] = false;

  int x = 1;
  for (int i = led + 1; i < NUM_LEDS; i++)
  {
    if (255 - x * 45 <= 100)
      break;
    fadeLeds[i] = false;
    x++;
  }
  x = 1;
  for (int i = led - 1; i >= 0; i--)
  {
    if (255 - x * 45 <= 100)
      break;
    fadeLeds[i] = false;
    x++;
  }
}

void OnControlChange(byte channel, byte number, byte value)
{
  debugV("MIDI Control Change: %s %s", String(number).c_str(), String(value).c_str());
  if (number == 64)
  {
    // sustain
    sustain = value;
    /*if (value > 63) {
      sustain = true;
    } else {
    sustain = false;
    }*/
  }
  else if (number == 67)
  {
    // soft
    if (value > 63)
    {
      DONT_FADE_NOTES = true;
    }
    else
    {
      DONT_FADE_NOTES = false;
    }
  }
  else if (number == 66)
  {
    // middle pedal
  }
}

void OnMidiSysEx(byte *data, unsigned length)
{
  debugV("SYSEX: (%s, %i bytes) ", getSysExStatus(data, length).c_str(), length);
  for (uint16_t i = 0; i < length; i++)
  {
    rdebugD("%i", data[i]);
    rdebugD(" ");
  }
  debugD();
}

String getSysExStatus(const byte *data, uint16_t length)
{
  if (data[0] == 0xF0 && data[length - 1] == 0xF7)
    return "F"; // Full SysEx Command
  else if (data[0] == 0xF0 && data[length - 1] != 0xF7)
    return "S"; // Start of SysEx-Segment
  else if (data[0] != 0xF0 && data[length - 1] != 0xF7)
    return "M"; // Middle of SysEx-Segment
  else
    return "E"; // End of SysEx-Segment
}

void OnNoteOnWIFI(byte channel, byte pitch, byte velocity)
{
  /*
   * Channel: 1 - right hand (led channel 13)
   * Channel: 2 - left hand (led channel 12)
   * Channel: 10 - metronome
   */
  debugI("WiFi note on: %s, velocity: %s, channel: %s", String(pitch).c_str(), String(velocity).c_str(), String(channel).c_str());
  byte pitchcheck = constrain(pitch, 21, 108);
  // byte velocitycheck = constrain(velocity,45,100);//constrain(velocity,0,127);
  // byte ld = map(pitchcheck,21,109,NUM_LEDS-1,-1);
  byte ld = -pitchcheck + NUM_LEDS + 21 - 1;
  // debugI("Mapped: %s -> %s", String(pitchcheck).c_str(), String(ld).c_str());
  // byte ld = map(pitchcheck,21,108,0,NUM_LEDS-1);
  onLeds[ld] = true;
  doNotFade[ld] = true;
  if (autoModeOn & (mode == 1 || mode == 0))
  {
    mode = autoMode;
  }
  autoModeOn = false;
  if (channel == 10)
  {
    if (pitch == 56)
    {
      leds[NUM_LEDS - 1].setHSV(0, 255, 255);
    }
    else
    {
      leds[NUM_LEDS - 1].setHSV(30, 255, 255);
    }
  }
  else if (channel != 1 && channel != 2)
  {

    leds[ld].setHSV(map(channel, 1, 16, 0, 255 * 3), 255, 255);
  }
}

void OnNoteOffWIFI(byte channel, byte pitch, byte velocity)
{
  debugI("WiFi note off: %s, velocity: %s, channel: %s", String(pitch).c_str(), String(velocity).c_str(), String(channel).c_str());
  byte pitchcheck2 = constrain(pitch, 21, 108);
  // byte ld = map(pitchcheck2,21,108,0,NUM_LEDS-1);
  byte ld = -pitchcheck2 + NUM_LEDS + 21 - 1;
  onLeds[ld] = false;
  doNotFade[ld] = false;
  if (channel == 10)
  {
    if (pitch == 56)
    {
      leds[NUM_LEDS - 1] = CRGB::Black;
    }
    else
    {
      leds[NUM_LEDS - 2] = CRGB::Black;
    }
  }
  else
  {

    leds[ld] = CRGB::Black;
  }
}

void OnControlChangeWIFI(byte channel, byte number, byte value)
{
  debugV("WIFI MIDI Control Change: %s %s", String(number).c_str(), String(value).c_str());
}

void setup()
{
  delay(2000); // Safety delay

  connectToWiFi();

  setUpOTA();

  setUpRemoteDebug();

  setUpEEPROM();

  // MIDI SETUP
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(OnNoteOn);
  MIDI.setHandleNoteOff(OnNoteOff);
  MIDI.setHandleControlChange(OnControlChange);
  MIDI.setHandleSystemExclusive(OnMidiSysEx);

  // LED SETUP
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  pinMode(LED_BUILTIN, OUTPUT); // DEBUG LED

  // ROTARY ENCODER SETUP
  pinMode(button_pin, INPUT_PULLUP); // BUTTON
  pinMode(clkPin, INPUT_PULLUP);     // set clkPin as INPUT
  pinMode(dtPin, INPUT_PULLUP);
  pinMode(btnPin, INPUT_PULLUP);

  // fill_solid( leds, NUM_LEDS, CRGB::White);
  // FastLED.show();

  fill_rainbow(rainbowPalette, NUM_LEDS, gHue, 3);
}

void connectToMidiSession()
{
  // Initiate the session
  IPAddress remote(192, 168, 1, 152);
  debugI("Connecting to MIDI: 192.168.1.152 ...");
  // Serial.print(remote);
  // Serial.print(" ...");
  // Serial.println();
}

void setUpEEPROM()
{
  EEPROM.begin(EEPROM_SIZE);
  if (AUTO_RESTORE_LAST_MODE)
    autoMode = EEPROM.read(0);
}

void getEncoderTurn(void)
{
  static int oldA = HIGH;
  static int oldB = HIGH;
  int result = 0;
  int newA = digitalRead(clkPin); // read the value of clkPin to newA
  int newB = digitalRead(dtPin);  // read the value of dtPin to newB
  if (newA != oldA || newB != oldB)
  {
    // something has changed
    if (oldA == HIGH && newA == LOW)
    {
      encoderChange(oldB);
    }
  }
  oldA = newA;
  oldB = newB;
}

void encoderChange(int oldB)
{

  switch (mode)
  {
  case 4:
    idx = constrain(idx - 2 * oldB + 1, 0, MODE4_PALETTE_COUNT - 1);
    debugD("Idx: %i", idx);
    break;
  case 5:
    idx = constrain(idx - 2 * oldB + 1, 0, MODE5_PALETTE_COUNT - 1);
    debugD("Idx: %i", idx);
    break;
  case 6:
    customSaturation = constrain(customSaturation + 4 * (-2 * oldB + 1), 0, 255);
    debugD("CustomSaturation: %i", customSaturation);
    break;
  default:
    customHue = constrain(customHue + 4 * (-2 * oldB + 1), 0, 255);
    debugD("CustomHue: %i", customHue);
    break;
  }
}

void connectToWiFi()
{
  wifiMulti.addAP("Dom", "Internet2014$"); // add Wi-Fi networks you want to connect to
  // Serial.print("Connecting to WiFi...");
  int i = 0;
  while (wifiMulti.run() != WL_CONNECTED)
  { // Wait for the Wi-Fi to connect
    delay(250);
    // Serial.print('.');
  }
  // Serial.println('\n');
  // Serial.print("Connected to ");
  // Serial.println(WiFi.SSID());              // Tell us what network we're connected to
  // Serial.print("IP address:\t");
  // Serial.println(WiFi.localIP());           // Send the IP address of the ESP8266 to the computer
}
void setUpOTA()
{
  ArduinoOTA.setHostname("ESP8266");
  // ArduinoOTA.setPassword("esp8266");

  ArduinoOTA.onStart([]()
                     {
                       FastLED.clear();
                       // Serial.println("Start");
                     });
  ArduinoOTA.onEnd([]()
                   {
    //Serial.println("\nEnd");
    fill_solid( leds, NUM_LEDS, CRGB::Green);
    FastLED.show(); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    CRGBSet leds2(leds, NUM_LEDS);
    CRGBSet ledsToFill(leds2(map(progress, 0, total, NUM_LEDS, -1),NUM_LEDS));
    fill_rainbow( ledsToFill, map(progress, 0, total, 0, NUM_LEDS), gHue, 3);
    FastLED.show(); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    //Serial.printf("Error[%u]: ", error);
    /*if (error == OTA_AUTH_ERROR) //Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) //Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) //Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) //Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) //Serial.println("End Failed");*/
    fill_solid( leds, NUM_LEDS, CRGB::Red);
    FastLED.show(); });
  ArduinoOTA.begin();
  // Serial.println("OTA ready");
}

void processCmdRemoteDebug()
{

  String lastCmd = Debug.getLastCommand();

  if (lastCmd == "connectMIDI" || lastCmd == "cm")
  {

    connectToMidiSession();
  }
}
void setUpRemoteDebug()
{
  Debug.begin("ESP8266");

  String helpCmd = "connectMIDI";
  Debug.setHelpProjectsCmds(helpCmd);
  Debug.setCallBackProjectCmds(&processCmdRemoteDebug);

  Debug.setResetCmdEnabled(true); // Enable the reset command
  // Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true); // Colors
  // Debug.setSerialEnabled(true);  // All messages too send to //Serial too, and can be see in //Serial monitor
}

// List of patterns to cycle through.  Each is defined as a separate function below.
typedef void (*SimplePatternList[])();
// SimplePatternList gPatterns = {rainbow, rainbowWithGlitter, confetti, sinelon};
SimplePatternList gPatterns = {rainbow, confetti, sinelon, bpm, juggle};

void handleInputs()
{
  // PROCESS DIGITAL INPUT
  // Button
  const unsigned int lastMode = mode;
  if (digitalRead(btnPin) == LOW)
  {
    delay(500);
    if (mode == 0)
    {
      mode = autoMode;
    }
    else
    {
      if (mode >= 7)
      {
        mode = 1;
      }
      else
      {
        mode++;
      }
      idx = 0;
      mode4PalIndex = 1;
      mode5PalIndex = 1;
      customHue = 0;
      customSaturation = 255;
    }
    debugI("Mode: %i", mode);
  }
  if (digitalRead(button_pin) == LOW)
  {
    delay(500);
    if (mode != 0)
    {
      autoMode = mode;
      mode = 0;
    }
    else
    {
      mode = autoMode;
    }
    debugI("Mode: %i", mode);
  }
  if (lastMode != mode)
  {
    EEPROM.write(0, mode);
    EEPROM.commit();
    debugI("Mode saved in flash memory (%i)", mode);
  }

  // Potentiometer
  if (millis() % 50 == 0)
  {
    int newAnalogRead = analogRead(POTENTIOMETER_PIN);
    if (newAnalogRead - currAnalogRead > 1 || newAnalogRead - currAnalogRead < -1)
    {
      debugV("Potentiometer: %i", newAnalogRead);
    } // else debugV("Potentiometer: %i", newAnalogRead);
    currAnalogRead = newAnalogRead;
  }

  // Rotary encoder
  getEncoderTurn();
}

void handlePaletteChange()
{
  if (mode == 4)
  {
    // int idx = constrain(floor(map(currAnalogRead,80,1024,0,MODE4_PALETTE_COUNT)),0,MODE4_PALETTE_COUNT-1);
    if (mode4PalIndex != idx)
    {
      digitalWrite(LED_BUILTIN, HIGH);
      mode4PalIndex = idx;
      // FILL PALETTE TO PREVIEW (in reverse) //
      // int incr = floor(255/NUM_LEDS);
      // int colorIndex = 255;
      for (uint16_t i = 0; i < NUM_LEDS; i++)
      {
        // leds[i] = ColorFromPalette(mode4Palettes[mode4PalIndex], colorIndex);
        // colorIndex -= incr;
        if (mode4PalIndex == 0)
          leds[i] = rainbowPalette[i];
        else
          leds[-(i + FIRST_KEY) + NUM_LEDS + FIRST_KEY - 1] = ColorFromPalette(mode4Palettes[mode4PalIndex - 1], map(i + FIRST_KEY, FIRST_KEY, 108, 0, 240));
      }
      /////
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  else if (mode == 5)
  {
    // int idx = constrain(floor(map(currAnalogRead,80,1024,0,MODE5_PALETTE_COUNT)),0,MODE5_PALETTE_COUNT-1);
    if (mode5PalIndex != idx)
    {
      digitalWrite(LED_BUILTIN, HIGH);
      mode5PalIndex = idx;
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  else
  {
    // customHue = map(currAnalogRead,45,1024,0,255);
  }
}

void updatePatternsAndHues()
{
  EVERY_N_MILLISECONDS(20)
  {
    if (mode == 1)
    {
      gHue++;
    }
  }
  EVERY_N_MILLISECONDS(50)
  {
    if (mode == 6)
    {
      gHue++;
    }
  }
  EVERY_N_SECONDS(20)
  {
    if (mode == 1)
    {
      nextPattern();
    }
  }
}

void showLeds()
{
  EVERY_N_MILLISECONDS(40)
  {
    if (mode != 1)
    {
      for (byte i = 0; i < NUM_LEDS; i++)
      {
        if (leds[i].getAverageLight() <= 0)
        { //(((leds[i].r==0 && (leds[i].g==0 || leds[i].b==0)) || (leds[i].g==0 && leds[i].b==0)) && leds[i].getAverageLight() <=5  ) {//(leds[i].getAverageLight() <=0) {
          onLeds[i] = false;
          fadeLeds[i] = false;
          leds[i] = CRGB::Black;
        }
        else if (!onLeds[i] && !fadeLeds[i])
        {
          if (sustain > 0)
          {
            if (!DONT_FADE_NOTES && !doNotFade[i])
              leds[i].fadeToBlackBy(map(constrain(sustain, 0, 80), 0, 80, NO_PEDAL_STRENGTH, PEDAL_STRENGTH));
          }
          else
          {
            leds[i].fadeToBlackBy(NO_PEDAL_STRENGTH);
          }
        }
        else
        {
          if (!DONT_FADE_NOTES && !doNotFade[i])
            leds[i].fadeToBlackBy(NOTE_HOLD_FADE);
        }
      }
      FastLED.show();
    }
  }
}

void sleepMode()
{
  if (!autoModeOn && millis() - lastKeyPress > SLEEP_TIMER * 1000 && mode != 0)
  {
    autoModeOn = true;
    autoMode = mode;
    mode = 1;
  }
  if (millis() - lastKeyPress > 5 * 60 * 1000 && mode != 0)
  {
    autoModeOn = false;
    mode = autoMode;
  }
}

void loop()
{

  ArduinoOTA.handle();

  // Set brightness
  FastLED.setBrightness(map(currAnalogRead, 0, 1024, 1, 255));

  handleInputs();

  handlePaletteChange();

  MIDI.read();

  if (mode == 1)
  {
    gPatterns[gCurrentPatternNumber]();
    FastLED.show();
    FastLED.delay(1000 / PASSIVE_FPS);
  }

  updatePatternsAndHues();

  sleepMode();

  showLeds();

  Debug.handle();
}

// PASSIVE PATTERNS FOR MODE 1 // TAKEN FROM FASTLED EXAMPLES
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

void nextPattern()
{
  // add one to the current pattern number, and wrap around at the end
  gCurrentPatternNumber = (gCurrentPatternNumber + 1) % ARRAY_SIZE(gPatterns);
}

void rainbow()
{
  // FastLED's built-in rainbow generator
  fill_rainbow(leds, NUM_LEDS, gHue, 3);
}

void rainbowWithGlitter()
{
  // built-in FastLED rainbow, plus some random sparkly glitter
  rainbow();
  addGlitter(20);
}

void addGlitter(fract8 chanceOfGlitter)
{
  if (random8() < chanceOfGlitter)
  {
    leds[random16(NUM_LEDS)] += CRGB::White;
  }
}

void confetti()
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, NUM_LEDS, 10);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(gHue + random8(64), 200, 255);
}

void sinelon()
{
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = beatsin16(13, 0, NUM_LEDS - 1);
  leds[pos] += CHSV(gHue, 255, 192);
}
void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
  for (int i = 0; i < NUM_LEDS; i++)
  { // 9948
    leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
  }
}

void juggle()
{
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy(leds, NUM_LEDS, 20);
  byte dothue = 0;
  for (int i = 0; i < 8; i++)
  {
    leds[beatsin16(i + 7, 0, NUM_LEDS - 1)] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}
