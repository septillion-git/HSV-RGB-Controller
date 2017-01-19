/**
 *  @file
 *  @brief RGB led controller with HSV-control and an OLED display
 *  
 */

/****************************************************************
/ Includes and macros
****************************************************************/
#include <FastLED.h>
#include <SPI.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ClickEncoder.h>
#include <avr/pgmspace.h>

//! Easy access to number of items in an array
#define numberOf(x) (sizeof(x)/sizeof(x[0]))

/****************************************************************
* Pin and address declarations
****************************************************************/
const byte RgbPins[] = {10, 3, 9}; //!< LED pins for {R, G, B}
const byte EncoderPins[] = {A1, A0, A2}; //!< Rotary encoder pins {A, B, switch}
const byte DisplayAddress = 0x3C; //!< Address of the oled diplay
//used by I2C = {A4, A5}; //reminder


/****************************************************************
* Global settings
****************************************************************/
const unsigned int FadeOnTime   = 2000;
const unsigned int FadeOffTime  = 2000;
const byte FadeOnOffInterval = 50; //!< Interval how often the brightness is updated while fading on or off


/****************************************************************
/ Menu definition and setting
****************************************************************/
const byte MaxMenuItems = 4; //!< Number of menu items that fit the display
const unsigned int MenuTimeout = 5000; //!< Time (ms) to reset the menu when no action
const unsigned int MenuRedrawTimeout = 200; //!< minimum time (ms) between display drawings
const byte ScrollBarWidth = 2; //!< Width of the scroll bar

//@{
/**
 *  @brief Menu item names in PROGMEM
 */
const PROGMEM char MenuVol[]   = "Bright.";
const PROGMEM char MenuHue[]   = "Hue";
const PROGMEM char MenuSat[]   = "Satur.";
const PROGMEM char MenuFade[]  = "Fade";
const PROGMEM char MenuSleep[] = "Sleep";
const PROGMEM char MenuWakeUp[]= "WakeUp";
//@}

//! Holds all the pointers to the menu item names as an array
const char* const Menu[] = {MenuVol, MenuHue, MenuSat, MenuFade, MenuSleep, MenuWakeUp};
//! Hold the corresponding function pointer to print the menu item value
void (*const MenuFunctions[numberOf(Menu)])(int, byte) = {
  printMenuPlainValue,  //item 0
  printMenuValueHue,    //item 1 etc
  printMenuPlainValue,
  printMenuTimeSeconds,
  printMenuTimeMinutes,
  printMenuTimeMinutes
};

//! Enum for easy naming Menu items or functions
enum MenuEnum_t{
  BrightnessV,
  HueV,
  SaturationV,
  FadeV,
  SleepV
};


/****************************************************************
/ Display and encoder objects
****************************************************************/
//! Encoder object from ClickEncoder
ClickEncoder *encoder;

//! Display object (u8g2) for a page buffered SSD1306 OLED
U8G2_SSD1306_128X64_NONAME_2_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

/****************************************************************
/ Menu variables
****************************************************************/
//! Value for each Menu item to hold that setting
unsigned int menuValues[numberOf(Menu)] = {50, 0, 100, 0};
byte menuPosition = 0; //!< current menu position
unsigned int menuTime; //!< Last time the menu was altered (done in kickMenu() )
unsigned int menuDrawTime; //!< Last time the menu was drawn
volatile bool redrawMenu = true; //!< Set to flag the menu needs redrawing
bool menuInUse = false; //!< Flags the menu is in use (done in kickMenu() )


/****************************************************************
/ Led control variables
****************************************************************/
CRGB leds; //!< Object to hold the RGB colors of the current led setting
byte brightness; //!< Holds the current set brightness (manual or fade) as byte (0-255)

volatile unsigned long fadeUpdateTime; //!< Time at which the fade was updated last
//! Last fade time set before fade disabled (set to long press default)
unsigned long previousFade = 4 * 60;

//!Enum for easy acces the operationModes
enum OperationEnum_t{
  NormalOperation,
  FadeOff,
  FadeOn,
  FadeSleep
};

byte operationMode = NormalOperation;


/****************************************************************
/ ISR routines
****************************************************************/

/**
 *  @brief ISR for reading the encoder and updating the fading
 *  
 *  @details Timer1 is set to overflow every ~1ms
 *  
 *  @param [in] TIMER1_OVF_vect Indicates Timer1 Overflow
 */
ISR(TIMER1_OVF_vect){
  encoder->service();
  
  unsigned int fadeInterval =(menuValues[FadeV] * 1000UL +  128) / 256;
  if(menuValues[FadeV] && brightness && millis() - fadeUpdateTime >= fadeInterval){
    menuValues[HueV]++;
    if(menuValues[HueV] > 255){
      menuValues[HueV] = 0;
    }
    fadeUpdateTime += fadeInterval;
    redrawMenu = true;
    
    setLeds(menuValues[HueV], menuValues[SaturationV], brightness);
    showLeds();
  }
}


/****************************************************************
/ Arduino routines
****************************************************************/

/**
 *  @brief Arduino setup() function()
 *  
 *  @details Runs only once
 */
void setup() {
  // setup outputs for the leds
  for(byte i = 0; i < 3; i++){
    pinMode(RgbPins[i], OUTPUT);
  }
  
  //make encoder object and setup
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
  interrupts();
  
  //Start serial for debug
  Serial.begin(115200);
  
  //Setup display
  display.setI2CAddress(DisplayAddress << 1);
  display.begin();
  Wire.setClock(400000L); //enable fast I2C
  display.setFont(u8g2_font_9x15B_tr);
  display.setFontPosTop();
}

/**
 *  @brief Arduino loop() function.
 *  
 *  @details It loops and loops and loops...
 */
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
  else if(operationMode == NormalOperation){
    if(menuValues[BrightnessV]){
      brightness = map(menuValues[BrightnessV], 1, 100, 1, 255);
    }
    else{
      brightness = 0;
    }
  }
  
  //Time to set the led brightness
  //no interrupts to be sure the interrupt will not change this as well
  noInterrupts();
  
  //calculate RGB values from HSV
  setLeds(menuValues[HueV], menuValues[SaturationV], brightness);
  
  //and re-enable them
  interrupts();
  showLeds();
}


/****************************************************************
/ Led functions 
****************************************************************/

/**
 *  @brief Sets the led outputs to the corresponding PWM level.
 *  
 *  @details Uses RgbPins[] for the outputs and the CRGB leds object for the values.
 *  
 */
inline void showLeds(){
  for(byte i = 0; i < sizeof(leds)/sizeof(leds[0]); i++){
    analogWrite(RgbPins[i], leds[i]);
  }
}

/**
 *  @brief Sets the led colors with HSV.
 *  
 *  @details Values are mapped according to there range.
 *  
 *  @param [in] h Hue 0-255
 *  @param [in] s Saturation 0-100
 *  @param [in] v Brightness 0-255
 */
inline void setLeds(byte h, byte s, byte v){
  s = map(s, 0, 100, 0, 255);
  leds.setHSV(h, s, v);
}
  


/****************************************************************
/ Menu functions
****************************************************************/

/**
 *  @brief Kicks the use of the menu.
 *  
 *  @details Lets us know via menuTime and menuInUse that the menu is still is in use. Call when there is user interaction with the menu.
 */
inline void kickMenu(){
  menuTime = millis();
  menuInUse = true;
}

/**
 *  @brief Check if the menu has timed out
 *  
 *  @details After MenuTimeout (ms) of no user interaction the menu is reset to the top menu item and a menu redraw is issued.
 */
inline void checkMenuTimeout(){
  unsigned int currentMillis = millis();
  if(menuInUse && currentMillis - menuTime > MenuTimeout){
    menuPosition = 0;
    menuInUse = false;
    
    redrawMenu = true;
  }
}

/**
 *  @brief Draws the menu onto the display.
 *  
 *  @details Calculates which items to display, scroll bar etc.
 *  
 *  @note drawing the menu takes some time!
 */
void drawMenu(){
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

/**
 *  @brief Print the menu item name.
 *  
 *  @details Will fetch the item name from PROGMEM and display it in the right X position. Y position is set with displayY.
 *  
 *  @param [in] itemNumber  The index number of the item to display.
 *  @param [in] displayY    The Y position for the item name.
 */
void printMenuItem(byte itemNumber, byte displayY){
  display.setCursor(2, displayY);
  byte len = strlen_P(Menu[itemNumber]);
  for(byte k = 0; k < len; k++){
    char myChar = pgm_read_byte_near(Menu[itemNumber] + k);
    display.print(myChar);
  }
}

/**
 *  @brief Just prints the value into the menu.
 *  
 *  @details Right alignes the value. Y position is given by displayY
 *  
 *  @param [in] value     The value to be printed
 *  @param [in] displayY  The Y position for the item name.
 */
void printMenuPlainValue(int value, byte displayY){
  byte width = 0;
  int temp = abs(value);
  
  //Check length of value
  for(; temp || !width; width++){
    temp /= 10;
  }
  
  //correct for negative values
  if(value < 0){
    width++;
  }
  
  display.setCursor(display.getDisplayWidth() - (width * 9) - 3 - ScrollBarWidth, displayY);
  display.print(value);
}

/**
 *  @brief Function to print the hue.
 *  
 *  @details Only differs from printMenuPlainValue() in that it will print a F in front of the value if fade is enabled.
 *  
 *  @param [in] value    Value to be printed
 *  @param [in] displayY The Y position for the item name.
 */
void printMenuValueHue(int value, byte displayY){
  //Print a F in front is fading
  if(menuValues[FadeV]){
    display.setCursor(display.getDisplayWidth() - (4 * 9) - 3 - ScrollBarWidth, displayY);
    display.print('F');
  }
  
  printMenuPlainValue(value, displayY);
}

/**
 *  @brief Print value as minutes with time makeup to the display
 *  
 *  @details Short for calling printMenuTime() with unitMinutes = **true** to be able to use it as menuValue function.
 *  
 *  @param [in] value    Value to be printed
 *  @param [in] displayY The Y position for the item name.
 */
void printMenuTimeMinutes(int value, byte displayY){
  printMenuTime(value, displayY, true);
}

/**
 *  @brief Print value as seconds with time makeup to the display
 *  
 *  @details Short for calling printMenuTime() with unitMinutes = **false** to be able to use it as menuValue function.
 *  
 *  @param [in] value    Value to be printed
 *  @param [in] displayY The Y position for the item name.
 */
void printMenuTimeSeconds(int value, byte displayY){
  printMenuTime(value, displayY, false);
}

/**
 *  @brief Print the value as a time to the display
 *  
 *  @details It's printed as (x)xhxx, (x)xmxx or (x)xs for seconds, or (x)xhxx or (x)xm for minutes. With x being the value for hours, minutes or seconds.
 *  
 *  If the value is 0, Off is displayed.
 *  
 *  unitMinutes = false, value is in seconds
 *  unitMinutes = true, value is in minutes
 *  
 *  @param [in] value       Value to be printed
 *  @param [in] displayY    The Y position for the item name.
 *  @param [in] unitMinutes Is the value in minutes?
 */
void printMenuTime(int value, byte displayY, bool unitMinutes){
  byte rightMargin = display.getDisplayWidth() - 3 - ScrollBarWidth;

  //If the value is 0 just print it as Off
  if(value == 0){
    display.setCursor(rightMargin - 26, displayY); //26 = width "Off"
    display.print(F("Off"));
  }
  else{
    byte s;
    byte m;
    byte h;
    
    //Split in hours, minutes and seconds
    if(unitMinutes){
      h = value / 60;
      m = value - (h * 60);
      s = 0;
    }
    else{
      h = value / (60 * 60);
      value -= h * (60 * 60);
      m = value / 60;
      s = value - (m * 60);
    }
    
    if(h){
      //hxx take 3 char of 9 pix + number of pix for h
      display.setCursor(rightMargin - ((3 + getNumDigits(h)) * 9), displayY);
      display.print(h);
      display.print('h');
    }
    
    if(m || h){
      byte numDigits = getNumDigits(m);
      
      //xx take 2 char of 9 pix
      if(h){
        display.setCursor(rightMargin - (2 * 9), displayY);
        for(; 2 - numDigits; numDigits++){
          display.print('0');
        }
        display.print(m);
      }
      //(x)xm takes numDigits + 1 char of 9 pix
      else if(unitMinutes){
        display.setCursor(rightMargin - ( (numDigits + 1) * 9), displayY);
        display.print(m);
        display.print('m');
      }
      //mxx take 3 char of 9 pix + pix for m
      else{
        display.setCursor(rightMargin - ((3 + numDigits) * 9), displayY);
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
      display.setCursor(rightMargin - ((1 + numDigits) * 9), displayY);
      display.print(s);
      display.print('s');
    } 
  }
  
}


/****************************************************************
/ Rotary encoder functions
****************************************************************/

/**
 *  @brief Checks the rotary encoder buttons and acts on it.
 *  
 *  @details Difference between press and (if individually enabled) long press and double press. Last is disabled to give the menu faster response.
 *  
 *  The action of the long press is determined by the menu position.
 */
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

/**
 *  @brief Checks the rotary encoder rotation and acts on it.
 *  
 *  @details Acts, depending on the menu position, on the rotation of the rotary encoder. For most items that's increasing/decreasing the menuValue withing boundaries.
 */
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
        else if(value > 0 && (100 - menuValues[menuPosition]) < value){
          menuValues[menuPosition] = 100;
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
          if(menuValues[menuPosition] > 0 || sign == 1){
            if(menuValues[menuPosition] < 30 - sign){
              menuValues[menuPosition] += sign * 5;
            }
            else if(menuValues[menuPosition] < (2 * 60 - sign)){
              menuValues[menuPosition] += sign * 15;
            }
            else if(menuValues[menuPosition] < (5 * 60 - sign)){
              menuValues[menuPosition] += sign * 30;
            }
            else if(menuValues[menuPosition] < (60 * 60 - sign)){
              menuValues[menuPosition] += sign * 60 * 5;
            }
          }
          //decreasing if off will go to the max fade time
          else{
            menuValues[menuPosition] = 60 * 60;
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


/****************************************************************
/ Miscellaneous small functions
****************************************************************/

/**
 *  @brief Calculates how many digits the value is.
 *  
 *  @details Done in 10-base.
 *  
 *  @param [in] val Value to check number of digits of.
 *  @return The number of digits of val
 */
byte getNumDigits(unsigned int val){
  byte num = 0;
  
  for(; val || !num; num++){
    val /= 10;
  }
  return num;
}

