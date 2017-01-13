#include <FastLED.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ClickEncoder.h>
#include <avr/pgmspace.h>

#define numberOf(x) (sizeof(x)/sizeof(x[0]))

//pin declarations
const byte RgbPins[] = {10, 3, 9};
const byte EncoderPins[] = {A1, A0, A2};

//Menu definition
const byte MaxMenuItems = 4;
const unsigned int MenuTimeout = 5000;
const unsigned int MenuRedrawTimeout = 100;

const char MenuVol[] PROGMEM   = "Vol";
const char MenuHue[] PROGMEM   = "Hue";
const char MenuSat[] PROGMEM   = "Sat";
const char MenuFade[] PROGMEM  = "Fade";
const char MenuSleep[] PROGMEM = "Sleep";

const char* const Menu[] = {MenuVol, MenuHue, MenuSat, MenuFade, MenuSleep};
void (*const MenuFunctions[numberOf(Menu)])(int, byte) = {printMenuPlainValue,
                                                          printMenuPlainValue, 
                                                          printMenuPlainValue};

enum MenuEnum_t{
  brightnessV,
  hueV,
  saturationV,
  fadeV,
  SleepV
};

//menu variables
unsigned int menuValues[numberOf(Menu)] = {255, 0};
byte menuPosition = 0; //!< current menu position
unsigned int menuTime; //!< Last time the menu was altered
unsigned int menuDrawTime;
bool redrawMenu = true;
bool menuInUse = false;


//led controll variables
CRGB leds;
byte hue;
byte saturation = 255;
byte brightness = 255;

//encoder object
ClickEncoder *encoder;

Adafruit_SSD1306 display(255);

//ISR for rotary encoder
ISR(TIMER1_OVF_vect){
  encoder->service();
}

void setup() {
  // setup outputs for the leds
  for(byte i = 0; i < 3; i++){
    pinMode(RgbPins[i], OUTPUT);
  }
  
  //make encoder object
  encoder = new ClickEncoder(EncoderPins[0], EncoderPins[1], EncoderPins[2], 4);
  encoder->setDoubleClickEnabled(false);
  encoder->setAccelerationEnabled(true);
        
  //Wave Generation Mode Fast 8-bit PWM (WGM1 = 0b0101)
  //Clock preScaler 64 (CS1 = 0b011) => 976,5625Hz = 1,024ms
  TCCR1A = (TCCR1A & 0b11111100) | 0b01; //WGM11:WGM10
  TCCR1B = (TCCR1B & 0b11100000) | (0b01 << WGM12) | 0b011; //WGM13:WGM12 CS12:CS10
  
  //Wave Generation Mode Fast PWM
  //Clock preScaler 64 => 976,5625Hz = 1,024ms
  TCCR2A = (TCCR2A & 0b11111100) | 0b11; //WGM21:WGM20
  TCCR2B = (TCCR2B & 0b11110000) | (0b0 << WGM22) | 0b100; //WGM22 CS22:CS20
  
  //enable ISR for Timer1 Overflow
  TIMSK1 = 0x01;
  
  //And enable global interrupts
  sei();
  
  Serial.begin(115200);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(INVERSE);
  display.print(0);
  display.print("test");

  display.display();
  delay(1000);
  
  display.clearDisplay();
  
  
  drawMenu();
}

void loop() {
  /*
  for(byte i = 0; i < 3; i++){
    digitalWrite(RgbPins[i], HIGH);
    delay(1000);
    digitalWrite(RgbPins[i], LOW);
  }
  delay(2000);
  */
  
  /*
  leds.setHSV(hue, saturation, brightness);
  showLeds();
  hue++;
  delay(200);
  */
  
  updateButton();
  
  updateRotary();
  
  checkMenuTimeout();
  
  unsigned int millisNow = millis();
  if(redrawMenu && millisNow - menuDrawTime >= MenuRedrawTimeout){
    drawMenu();
  }
}

void showLeds(){
  for(byte i = 0; i < 3; i++){
    analogWrite(RgbPins[i], leds[i]);
  }
}

void drawMenu(){
  //display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(INVERSE);
  display.clearDisplay();
  
  //Calculate first item to display based on menu position
  byte firstItemToDisplay;
  if(menuPosition == 0){
    firstItemToDisplay = 0;
  }
  else if((numberOf(Menu) - menuPosition) <= (MaxMenuItems - 2)){
    firstItemToDisplay = numberOf(Menu) - MaxMenuItems;
  }
  else{
    firstItemToDisplay = menuPosition - 1;
  }
  
  for(byte i = 0; i < min(MaxMenuItems, numberOf(Menu)); i++){
    if((firstItemToDisplay + i) == menuPosition){
      display.fillRect(0, i * 16, display.width(), 16, WHITE);
    }
    
    //print the menu items
    printMenuItem(firstItemToDisplay + i, i * 16 + 1);
    
    //print menu values
    printMenuValue(firstItemToDisplay + i, i * 16 + 1);
  }
  
  display.display();
}

void printMenuItem(byte itemNumber, byte displayLine){
  display.setCursor(2, displayLine);
  byte len = strlen_P(Menu[itemNumber]);
  for(byte k = 0; k < len; k++){
    char myChar = pgm_read_byte_near(Menu[itemNumber] + k);
    display.print(myChar);
  }
}

void printMenuValue(byte itemNumber, byte displayLine){
  if( itemNumber == 0) printMenuPlainValue(brightness, displayLine);
  if( itemNumber == 1) printMenuPlainValue(hue, displayLine);
        
}

inline void kickMenu(){
  menuTime = millis();
  menuInUse = true;
}

void updateButton(){
  //read button
  ClickEncoder::Button b = encoder->getButton();
  if(b == ClickEncoder::Clicked){
    kickMenu();
    menuPosition++;
    
    //check when to wrap
    if(menuPosition == numberOf(Menu)){
      menuPosition = 0;
    }
    
    redrawMenu = true;
  }
}

inline void checkMenuTimeout(){
  unsigned int currentMillis = millis();
  if(menuInUse && currentMillis - menuTime > MenuTimeout){
    menuPosition = 0;
    menuInUse = false;
    
    redrawMenu = true;
  }
}

void updateRotary(){
  int value = encoder->getValue();
  
  if(value){
    kickMenu();
    redrawMenu = true;    
    
    switch(menuPosition){
      case 0:
        if(value < 0 && (brightness - 1) < -value){
          brightness = 1;
        }
        else if(value > 0 && (255 - brightness) < value){
          brightness = 255;
        }
        else{
          brightness += value;
        }
        break;
      case 1:
        hue += value;
        break;
    } 
  }
}

void printMenuPlainValue(int value, byte line){
  byte width = 0;
  int temp = value;
  for(; temp || !width; width++){
    temp /= 10;
  }
  
  display.setCursor(display.width() - (width * 12), line);
  display.print(value);
}
