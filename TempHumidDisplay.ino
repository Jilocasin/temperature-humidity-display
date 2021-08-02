#include <InputDebounce.h>
#include <DHTesp.h>
#include <SPI.h>
#include <DigitLed72xx.h>
#include <EEPROM.h>

/*
   7 segment bits [1 << n]:

          6
        1   5
          0
        2   4
          3
*/

/* Constants */

#define DISPLAY_NCHIP 1
#define DEBOUNCE_DELAY_MS 20

#define PIN_DISPLAY_CS 9
#define PIN_SENSOR_DATA 4
#define PIN_BUTTON_ACTION 2
#define PIN_BUTTON_NEXT_MODE 3
#define PIN_HUMIDIFIER_RELAY 5

#define HUMIDIFIER_THRESHOLD_STEPS 2

/**
 * Lowest value that can be configured.
 * Because the config can only be increasing by a single button,
 * using a high minimum value helps to avoid a generally unused interval.
 */
#define HUMIDIFIER_THRESHOLD_MININUM 60

/**
   Magic number read/written from the EEPROM to know its initialization status.
   Change this to provide a new default configuration on next startup.
*/
#define EEPROM_MAGIC_NUMBER 0xBABE

/* Mode selection */

enum DisplayMode {
    live,
    min,
    max,
    humidifierEnabled,
    humidifierLowerOnThreshold,
    humidifierUpperOffThreshold,
    
    END // Last mode entry used for integer modulo.
};

/**
 * Struct containing all data that is persisted to the EEPROM and their default values.
 */
struct PersistentConfiguration {
    int magicNumber = EEPROM_MAGIC_NUMBER;
    
    /**
     Humidity threshold under which the humidifier will turn on.
    */
    byte humidifierLowerOnThreshold = 80;
    
    /**
     Humidity threshold over which the humidifier will turn off.
    */
    byte humidifierUpperOffThreshold = 96;
    
    /**
     Configuration to enable or disable humidifier.
    */
    bool humidifierEnabled = true;
};

PersistentConfiguration persistentConfiguration;

/**
   Direct mapping of the relay pinout.
*/
static bool humidifierOn = false;

/**
 * The currently active mode, responsible for the display content.
 */
static DisplayMode currentMode = live;

/**
   Timestamp of the last mode change.
*/
static long modeSelectedTime;

/* Static data containers */

class DisplayData {
    public:
        int temperature;
        int humidity;
        
        DisplayData() {
          temperature = 0;
          humidity = 0;
        }
        
        DisplayData(DisplayData& data) {
          temperature = data.temperature;
          humidity = data.humidity;
        }
};

static DisplayData liveData;
static DisplayData maxData;
static DisplayData minData;

/* Static Arduino elements */

static DHTesp sensor;
static InputDebounce buttonAction;
static InputDebounce buttonNextModeButton;
static DigitLed72xx display = DigitLed72xx(PIN_DISPLAY_CS, DISPLAY_NCHIP);


/* Button handlers */

void onActionButton(uint8_t pinIn, unsigned long duration) {
    static byte brightness = 0;
    
    switch (currentMode) {
        case live:
            if (brightness == 0) {
                brightness = 8;
            } else {
                brightness = 0;
            }
            
            display.setBright(brightness);
            break;
        
        case max:
            maxData = DisplayData(liveData);
            updateDisplay();
            presentResetAnimation();
            break;
        
        case min:
            minData = DisplayData(liveData);
            updateDisplay();
            presentResetAnimation();
            break;
        
        case humidifierEnabled:
            persistentConfiguration.humidifierEnabled = !persistentConfiguration.humidifierEnabled;
            EEPROM.put(0, persistentConfiguration);
            break;
        
        case humidifierLowerOnThreshold:
            persistentConfiguration.humidifierLowerOnThreshold = persistentConfiguration.humidifierLowerOnThreshold + HUMIDIFIER_THRESHOLD_STEPS;
            
            if (persistentConfiguration.humidifierLowerOnThreshold >= persistentConfiguration.humidifierUpperOffThreshold) {
                persistentConfiguration.humidifierLowerOnThreshold = HUMIDIFIER_THRESHOLD_MININUM;
            }
            
            EEPROM.put(0, persistentConfiguration);
            break;
        
        case humidifierUpperOffThreshold:
            persistentConfiguration.humidifierUpperOffThreshold = persistentConfiguration.humidifierUpperOffThreshold + HUMIDIFIER_THRESHOLD_STEPS;
            
            if (persistentConfiguration.humidifierUpperOffThreshold > 100 - HUMIDIFIER_THRESHOLD_STEPS) {
                persistentConfiguration.humidifierUpperOffThreshold = persistentConfiguration.humidifierLowerOnThreshold + HUMIDIFIER_THRESHOLD_STEPS;
            }
            
            EEPROM.put(0, persistentConfiguration);
            break;
        
        default:
            break;
    }
    
    updateDisplay();
}

void onNextModeButton(uint8_t pinIn, unsigned long duration) {
    currentMode = static_cast<DisplayMode>((currentMode + 1) % END);
    
    modeSelectedTime = millis();
    
    updateDisplay();
}

/* Updating data */

void updateDisplay() {
    switch (currentMode) {
        case live:
            printTempAndHumidityOverview(liveData, NULL);
            break;
        
        case min:
            printTempAndHumidityOverview(minData, printTempHumidityMinimumSigns);
            break;
        
        case max:
            printTempAndHumidityOverview(maxData, printTempHumidityMaximumSigns);
            break;
        
        case humidifierEnabled:
            printHumidifierConfigPrefix();
            printBooleanConfigurationStatus(persistentConfiguration.humidifierEnabled);
            break;
        
        case humidifierLowerOnThreshold: {
            printHumidifierConfigPrefix();
            printHumidifierConfigValueWithBlinking(persistentConfiguration.humidifierLowerOnThreshold, B00001000);
            break;
        }
        
        case humidifierUpperOffThreshold: {
            printHumidifierConfigPrefix();
            printHumidifierConfigValueWithBlinking(persistentConfiguration.humidifierUpperOffThreshold, B01000000);
            
            break;
        }
    }
}

void updateRelayStatus() {
    if (!humidifierOn && liveData.humidity <= persistentConfiguration.humidifierLowerOnThreshold) {
        humidifierOn = true;
    } else if (humidifierOn && liveData.humidity >= persistentConfiguration.humidifierUpperOffThreshold) {
        humidifierOn = false;
    }
    
    digitalWrite(PIN_HUMIDIFIER_RELAY, !(humidifierOn && persistentConfiguration.humidifierEnabled));
}

/* Display data printing methods */ 

/**
 * Returns whether any blinking data should currently show its first or second content.
 */
boolean getBlinkStatus() {
    return (millis() - modeSelectedTime) % 1000 > 500;
}

/**
   Prints the status of the given bool to the right side [---- XXXX] as either [ OFF] or [ On ].
*/
void printBooleanConfigurationStatus(bool status) {
    if (status) {
        display.write(4, B00000000, 0);
        display.write(3, B01111110, 0); // O
        display.write(2, B00010101, 0); // n
        display.write(1, B00000000, 0);
    } else {
        display.write(4, B00000000, 0);
        display.write(3, B01111110, 0); // O
        display.write(2, B01000111, 0); // F
        display.write(1, B01000111, 0); // F
    }
}

/**
 * Prints [HUFI ----] for configuring the humidifier.
 */
void printHumidifierConfigPrefix() {
    display.write(8, B00110111, 0); // H
    display.write(7, B00111110, 0); // U
    display.write(6, B01000111, 0); // F
    display.write(5, B00000110, 0); // I
}

/**
 * Prints [---- XXrH] where XX is the given value.
 * The "rH" digits will blink, toggling betweeh "rH" and the given blink byte.
 */
void printHumidifierConfigValueWithBlinking(byte value, byte blinkByte) {       
    if (getBlinkStatus()) {
        display.write(2, B00000101, 0); // r
        display.write(1, B00110111, 0); // H
    } else {
        display.write(2, blinkByte, 0);
        display.write(1, blinkByte, 0);
    }
    
    printTwoDigitValue(value, 2);
}

/**
 * Prints temperature and humidity data from the given DisplayData (all 8 digits overwritten).
 */
void printTempAndHumidityOverview(DisplayData& data, void* signsFunction()) {
    if (getBlinkStatus() || signsFunction == NULL) {
        printCelsiusAndRelHumiditySigns();
    } else if (signsFunction != NULL) {
        (*signsFunction)();
    }
    
    printTwoDigitValue(data.temperature, 6);
    printTwoDigitValue(data.humidity, 2);
}

/**
 * Prints the given temperature value (between 0 and 99) to positions [nn-- ----]
 */
void printTwoDigitValue(int value, int digit) {
    if (value < 10) {
        display.write(digit + 2, 0, 0); // Clear higher digit to "0"
    } else if (value > 99) {
        value = 99;
    }
    
    display.printDigit(value, DISPLAY_NCHIP, digit);
}

/**
 * Prints [--°C --rH]
 * Used in conjunction with printTemperature and printHumidity.
 */
void printCelsiusAndRelHumiditySigns() {
    display.write(6, B01100011, 0); // °
    display.write(5, B01001110, 0); // C
    
    display.write(2, B00000101, 0); // r
    display.write(1, B00110111, 0); // H
}

/**
 * Prints [--‾‾ --‾‾]
 * Used for blinking in conjunction with printTemperature and printHumidity.
 */
void printTempHumidityMaximumSigns() {
    display.write(6, B01000000, 0);
    display.write(5, B01000000, 0);
    
    display.write(2, B01000000, 0);
    display.write(1, B01000000, 0);
}

/**
 * Prints [--__ --__]
 * Used for blinking in conjunction with printTemperature and printHumidity.
 */
void printTempHumidityMinimumSigns() {
    display.write(6, B00001000, 0);
    display.write(5, B00001000, 0);
    
    display.write(2, B00001000, 0);
    display.write(1, B00001000, 0);
}

void presentResetAnimation() {
    static byte RESET_ANIMATION_MAX[] = {6, 1, 2, 3, 4, 5};
    static byte RESET_ANIMATION_MIN[] = {3, 4, 5, 6, 1, 2};
    
    if (currentMode == min) {
        presentResetAnimationWithSegments(RESET_ANIMATION_MIN);
    } else if (currentMode == max) {
        presentResetAnimationWithSegments(RESET_ANIMATION_MAX);
    }
}

void presentResetAnimationWithSegments(byte activeSegment[]) {
    for (int i = 0; i < 6; i++) {
        display.write(3, 1 << activeSegment[i], 0);
        display.write(4, 1 << activeSegment[i], 0);
        display.write(7, 1 << activeSegment[i], 0);
        display.write(8, 1 << activeSegment[i], 0);
        
        delay(50);
    }
}

/* Arduino methods */

void setup() {
    Serial.begin(9600);
    
    EEPROM.get(0, persistentConfiguration);
    
    if (persistentConfiguration.magicNumber != EEPROM_MAGIC_NUMBER) {
        EEPROM.put(0, PersistentConfiguration());
        EEPROM.get(0, persistentConfiguration);
        Serial.println(F("Initialized EEPROM default config"));
    } else {
        Serial.println(F("Loaded config from EEPROM"));
    }
    
    pinMode(PIN_HUMIDIFIER_RELAY, OUTPUT);
    
    display.on(DISPLAY_NCHIP);
    
    sensor.setup(PIN_SENSOR_DATA, DHTesp::DHT22);
    
    buttonAction.registerCallbacks(NULL, NULL, onActionButton, NULL);
    buttonNextModeButton.registerCallbacks(NULL, NULL, onNextModeButton, NULL);
    
    buttonAction.setup(PIN_BUTTON_ACTION, DEBOUNCE_DELAY_MS, InputDebounce::PIM_INT_PULL_UP_RES, 50);
    buttonNextModeButton.setup(PIN_BUTTON_NEXT_MODE, DEBOUNCE_DELAY_MS, InputDebounce::PIM_INT_PULL_UP_RES, 50);
}

void updateSensorData() {
    liveData.temperature = round(sensor.getTemperature());
    liveData.humidity = round(sensor.getHumidity());
    
    static bool hasInitializedMinMax = false;
    
    if (hasInitializedMinMax == false) {
        minData = DisplayData(liveData);
        maxData = DisplayData(liveData);
        
        hasInitializedMinMax = true;
    }
    
    if (liveData.temperature < minData.temperature) {
        minData.temperature = liveData.temperature;
    }
    
    if (liveData.temperature > maxData.temperature) {
        maxData.temperature = liveData.temperature;
    }
    
    if (liveData.humidity < minData.humidity) {
        minData.humidity = liveData.humidity;
    }
    
    if (liveData.humidity > maxData.humidity) {
        maxData.humidity = liveData.humidity;
    }
}

void loop() {
    delay(2);
    
    unsigned long now = millis();
    
    buttonAction.process(now);
    buttonNextModeButton.process(now);
    
    static unsigned long nextSensorSampling = 0;
    
    if (now > nextSensorSampling) {
        nextSensorSampling = now + sensor.getMinimumSamplingPeriod();
        updateSensorData();
        updateRelayStatus();
    }
    
    updateDisplay();
}
