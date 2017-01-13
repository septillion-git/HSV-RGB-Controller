#include <FastLED.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ClickEncoder.h>
#include <avr/pgmspace.h>

#define numberOf(x) (sizeof(x)/sizeof(x[0]))

//pin and address declarations
const byte RgbPins[] = {10, 3, 9}; //!< LED pins for R, G, B
const byte EncoderPins[] = {A1, A0, A2}; //!< Rotary pins A, B, switch
const byte DisplayAddress = 0x3C; //!< Address of the oled diplay
//used by I2C = {A4, A5}; //reminder

//Menu definition
const byte MaxMenuItems = 4; //!< Number of menu items that fit the display
const unsigned int MenuTimeout = 5000; //!< Time (ms) to reset the menu when no action
const unsigned int MenuRedrawTimeout = 200; //!< minimum time (ms) between display drawings
const byte ScrollBarWidth = 2; //!< Width of the scroll bar

const char MenuVol[] PROGMEM   = "Bright.";
const char MenuHue[] PROGMEM   = "Hue";
const char MenuSat[] PROGMEM   = "Sat";
const char MenuFade[] PROGMEM  = "Fade";
const char MenuSleep[] PROGMEM = "Sleep";
const char MenuWakeUp[] PROGMEM= "WakeUp";

const char* const Menu[] = {MenuVol, MenuHue, MenuSat, MenuFade, MenuSleep, MenuWakeUp};
void (*const MenuFunctions[numberOf(Menu)])(int, byte) = {printMenuPlainValue,
                                                          printMenuValueHue, 
                                                          printMenuPlainValue,
                                                          printMenuTimeSeconds};

enum MenuEnum_t{
  BrightnessV,
  HueV,
  SaturationV,
  FadeV,
  SleepV
};

//menu variables
unsigned int menuValues[numberOf(Menu)] = {50, 0, 255, 0};
byte menuPosition = 0; //!< current menu position
unsigned int menuTime; //!< Last time the menu was altered (done in kickMenu()
unsigned int menuDrawTime; //!< Last time the menu was drawn
volatile bool redrawMenu = true; //!< Set to flag the menu needs redrawing
bool menuInUse = false; //!< Flags the menu is in use (done in kickMenu()


//led controll variables
CRGB leds;
byte brightness = 255;

volatile unsigned long fadeUpdateTime;

unsigned long previousFade = 4 * 60; //!< Last fade time set before fade disabled

//encoder object
ClickEncoder *encoder;

U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

//ISR for rotary encoder
ISR(TIMER1_OVF_vect){
  encoder->service();
  
  unsigned int fadeInterval =(menuValues[FadeV] * 1000UL +  256) / 256;
  if(menuValues[FadeV] && millis() - fadeUpdateTime >= fadeInterval){
    menuValues[HueV]++;
    if(menuValues[HueV] > 255){
      menuValues[HueV] = 0;
    }
    fadeUpdateTime += fadeInterval;
    redrawMenu = true;
    
    leds.setHSV(menuValues[HueV], menuValues[SaturationV], brightness);
    showLeds();
  }
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
  
  display.setI2CAddress(DisplayAddress << 1);
  display.begin();
  Wire.setClock(400000L);
  display.setFont(u8g2_font_9x15B_tr);
  display.setFontPosTop();
  
  /*
  display.firstPage();
  do{
    display.setCursor(0, 0);
    display.print("Test");
    MenuFunctions[BrightnessV](menuValues[BrightnessV], 16);
  } while(display.nextPage());
  delay(1000);
  */
}

void loop() {
  updateButton();
  
  updateRotary();
  
  checkMenuTimeout();
  
  unsigned int millisNow = millis();
  if( brightness 
      && redrawMenu 
      && millisNow - menuDrawTime >= MenuRedrawTimeout){
    redrawMenu = false;
    menuDrawTime = millisNow;
    
    unsigned long tic, toc;    
    tic = micros();
    drawMenu();
    toc = micros();
    Serial.print("Menu draw time: ");
    Serial.print(toc-tic);
    Serial.println("us");
    
  }
  
  //check if we need to store a new previousFade
  if(menuValues[FadeV] && menuValues[FadeV] != previousFade){
    previousFade = menuValues[FadeV];
  }
  
  //fix percentage to byte brightness
  //if a sleep timer is set, brightness byte is altered so fix percentage
  if(false){
  
  }
  //otherwise, set byte corresponding to percentage
  else{
    if(menuValues[BrightnessV]){
      brightness = map(menuValues[BrightnessV], 1, 100, 1, 255);
    }
    else{
      brightness = 0;
    }
  }
  
  //fading
  /*
  unsigned int fadeInterval =(menuValues[FadeV] * 1000UL +  256) / 256;
  if(menuValues[FadeV] && millis() - fadeUpdateTime >= fadeInterval){
    menuValues[HueV]++;
    if(menuValues[HueV] > 255){
      menuValues[HueV] = 0;
    }
    fadeUpdateTime += fadeInterval;
    //redrawMenu = true;
  } */ 
  noInterrupts();
  leds.setHSV((byte)menuValues[HueV], (byte)menuValues[SaturationV], brightness);
  interrupts();
  showLeds();    
}

void showLeds(){
  for(byte i = 0; i < 3; i++){
    analogWrite(RgbPins[i], leds[i]);
  }
}

void drawMenu(){
  //display.clearDisplay();
  //display.setTextSize(2);
  //display.setTextColor(INVERSE);
  //display.clearDisplay();
  
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
  
  //display routine
  display.firstPage();
  do{
    //draw the scroll bar
    display.setDrawColor(1);
    byte scrollBarStart = display.getDisplayHeight() * firstItemToDisplay / numberOf(Menu);
    byte scrollBarLength = display.getDisplayHeight() * 4 / numberOf(Menu);
    for(byte i = 0; i < ScrollBarWidth; i++){
      display.drawVLine(127 - i, scrollBarStart, scrollBarLength);
    }
    
    //now draw the real menu
    for(byte i = 0; i < min(MaxMenuItems, numberOf(Menu)); i++){
      byte currentItem = firstItemToDisplay + i;
      byte currentLine = i * 16 + 2;
      
      display.setDrawColor(1);
      
      if(currentItem == menuPosition){
        display.drawBox(0, i * 16, display.getDisplayWidth() - 2 - ScrollBarWidth, 16);
        //display.fillRect(0, i * 16, display.width(), 16, WHITE);
        display.setDrawColor(0);
      }
      
      //print the menu items
      printMenuItem(currentItem, currentLine);
      
      //print menu values
      if(MenuFunctions[currentItem] != NULL){
        MenuFunctions[currentItem](menuValues[currentItem], currentLine);
      }
      
      
    }
  } while(display.nextPage());
  
  //display.display();
}

void printMenuItem(byte itemNumber, byte displayLine){
  display.setCursor(2, displayLine);
  byte len = strlen_P(Menu[itemNumber]);
  for(byte k = 0; k < len; k++){
    char myChar = pgm_read_byte_near(Menu[itemNumber] + k);
    display.print(myChar);
  }
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
  if(b == ClickEncoder::Held){
    kickMenu();
    switch(menuPosition){
      case 0:{
        menuValues[BrightnessV] = 0;
        //now clear the display
        display.firstPage();
        while(display.nextPage());
        break;
      }
      case 3:{
        menuValues[FadeV] = previousFade;
        fadeUpdateTime = millis();
        redrawMenu = true;
        break;
      }
    }
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
      //Brightness
      case 0:{
        if(value < 0 && (menuValues[BrightnessV] - 1) < -value){
          menuValues[BrightnessV] = 1;
        }
        else if(value > 0 && (100 - menuValues[BrightnessV]) < value){
          menuValues[BrightnessV] = 100;
        }
        else{
          menuValues[BrightnessV] += value;
        }
        Serial.println(menuValues[BrightnessV]);
        break;
      }
      //Hue
      case 1:{
        if(!menuValues[FadeV]){
          byte temp = menuValues[HueV];
          temp += value;
          menuValues[HueV] = temp;
          Serial.println(menuValues[HueV]);
        }
        
        menuValues[FadeV] = 0; //turn off fading
        break;
      }
      //Saturation
      case 2:{
        if(value < 0 && (menuValues[menuPosition]) < -value){
          menuValues[menuPosition] = 0;
        }
        else if(value > 0 && (255 - menuValues[menuPosition]) < value){
          menuValues[menuPosition] = 255;
        }
        else{
          menuValues[menuPosition] += value;
        }
        Serial.println(menuValues[menuPosition]);
        break;
      }
      //Fade time
      case 3:{
        signed char sign = (value < 0 ? -1: 1);
        value = abs(value);
        
        for(byte i = 0; i < value; i++){
          if(menuValues[menuPosition] >= 5 || sign == 1){
            if(menuValues[menuPosition] < 60 - sign){
              menuValues[menuPosition] += sign * 5;
            }
            else if(menuValues[menuPosition] < (5 * 60 - sign)){
              menuValues[menuPosition] += sign * 15;
            }
            else if(menuValues[menuPosition] < (10 * 60 - sign)){
              menuValues[menuPosition] += sign * 30;
            }
            else if(menuValues[menuPosition] < (60 * 60 - sign)){
              menuValues[menuPosition] += sign * 60;
            }
          }
        }
        
        //reset fading time to now on fade time change
        fadeUpdateTime = millis();
        
        Serial.println(menuValues[menuPosition]);
        break;
      }
    } 
  }
}

void printMenuPlainValue(int value, byte line){
  byte width = 0;
  int temp = value;
  for(; temp || !width; width++){
    temp /= 10;
  }
  
  display.setCursor(display.getDisplayWidth() - (width * 9) - 3 - ScrollBarWidth, line);
  display.print(value);
}

void printMenuValueHue(int value, byte line){
  if(menuValues[FadeV]){
    display.setCursor(display.getDisplayWidth() - (4 * 9) - 3 - ScrollBarWidth, line);
    display.print('F');
  }
  
  printMenuPlainValue(value, line);
}

void printMenuTimeMinutes(int value, byte line){
  
}

void printMenuTimeSeconds(int value, byte line){
  byte rightMargin = display.getDisplayWidth() - 3 - ScrollBarWidth;

  if(value == 0){
    display.setCursor(rightMargin - 26, line); //26 = width "Off"
    display.print(F("Off"));
  }
  else{
    byte s;
    byte m;
    byte h;
    
    h = value / (60 * 60);
    value -= h * (60 * 60);
    m = value / 60;
    s = value - (m * 60);
    
    if(h){
      //hxx take 3 char of 9 pix + number of pix for h
      display.setCursor(rightMargin - ((3 + getNumDigits(h)) * 9), line);
      display.print(h);
      display.print('h');
    }
    if(m || h){
      byte numDigits = getNumDigits(m);
      
      //xx take 2 char of 9 pix
      if(h){
        display.setCursor(rightMargin - (2 * 9), line);
        for(; 2 - numDigits; numDigits++){
          display.print('0');
        }
        display.print(m);
      }
      //mxx take 3 char of 9 pix + pix for m
      else{
        display.setCursor(rightMargin - ((3 + numDigits) * 9), line);
        display.print(m);
        display.print('m');
        
        numDigits = getNumDigits(s);
        for(; 2 - numDigits; numDigits++){
          display.print('0');
        }
        display.print(s);
      }
    }
    else{
      byte numDigits = getNumDigits(s);
      display.setCursor(rightMargin - ((1 + numDigits) * 9), line);
      display.print(s);
      display.print('s');
    } 
  }
  
}

byte getNumDigits(unsigned int val){
  byte num = 0;
  
  for(; val || !num; num++){
    val /= 10;
  }
  return num;
}

