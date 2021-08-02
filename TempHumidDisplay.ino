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

#define HUMIDIFIER_ON_LOWER_THRESHOLD 80
#define HUMIDIFIER_OFF_UPPER_THRESHOLD 95

/**
   Magic number read/written from the EEPROM to know its initialization status.
   Change this to provide a new default configuration on next startup.
*/
#define EEPROM_MAGIC_NUMBER 0xBABE

/* Mode selection */

enum Mode {
  live,
  min,
  max,
  humidifierEnabled,
  humidifierLowerOnThreshold,
  humidifierUpperOffThreshold,

  END // Last mode entry used for integer modulo.
};

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
static Mode currentMode = live;

/**
   Timestamp when the last mode changed occurred.
*/
static long modeSelectedTime;

/* Data container */

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

static InputDebounce buttonAction;
static InputDebounce buttonNextModeButton;

static DigitLed72xx display = DigitLed72xx(PIN_DISPLAY_CS, DISPLAY_NCHIP);
static DHTesp sensor;

/* Methods */

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
      updateDisplayData();
      printResetAnimation();
      break;

    case min:
      minData = DisplayData(liveData);
      updateDisplayData();
      printResetAnimation();
      break;

    case humidifierEnabled:
      persistentConfiguration.humidifierEnabled = !persistentConfiguration.humidifierEnabled;
      EEPROM.put(0, persistentConfiguration);
      break;

    case humidifierLowerOnThreshold:
      persistentConfiguration.humidifierLowerOnThreshold = persistentConfiguration.humidifierLowerOnThreshold + 2;
      if (persistentConfiguration.humidifierLowerOnThreshold >= persistentConfiguration.humidifierUpperOffThreshold) {
        persistentConfiguration.humidifierLowerOnThreshold = 60;
      }
      EEPROM.put(0, persistentConfiguration);
      break;

    case humidifierUpperOffThreshold:
      persistentConfiguration.humidifierUpperOffThreshold = persistentConfiguration.humidifierUpperOffThreshold + 2;
      if (persistentConfiguration.humidifierUpperOffThreshold > 98) {
        persistentConfiguration.humidifierUpperOffThreshold = persistentConfiguration.humidifierLowerOnThreshold + 2;
      }
      EEPROM.put(0, persistentConfiguration);
      break;

    default:
      break;
  }

  updateDisplayData();
}

void onNextModeButton(uint8_t pinIn, unsigned long duration) {
  currentMode = static_cast<Mode>((currentMode + 1) % END);

  modeSelectedTime = millis();

  updateDisplayData();
}

void showTemperature(int temperature) {
  if (temperature < 10) {
    display.write(8, 0, 0); // Clear higher digit
  } else if (temperature > 99) {
    temperature = 99;
  }

  display.printDigit(temperature, DISPLAY_NCHIP, 6);
}

void showHumidity(int humidity) {
  if (humidity < 10) {
    display.write(4, 0, 0); // Clear higher digit
  } else if (humidity > 99) {
    humidity = 99;
  }

  display.printDigit(humidity, DISPLAY_NCHIP, 2);
}

void updateDisplayData() {
  switch (currentMode) {
    case live:
      printTempAndHumidity(liveData, NULL);
      break;

    case min:
      printTempAndHumidity(minData, printShowingMinimumSigns);
      break;

    case max:
      printTempAndHumidity(maxData, printShowingMaximumSigns);
      break;

    case humidifierEnabled:
      display.write(8, B00110111, 0); // H
      display.write(7, B00111110, 0); // U
      display.write(6, B01000111, 0); // F
      display.write(5, B00000110, 0); // I
      printBooleanStatus(persistentConfiguration.humidifierEnabled);
      break;

    case humidifierLowerOnThreshold: {
      display.write(8, B00110111, 0); // H
      display.write(7, B00111110, 0); // U
      display.write(6, B01000111, 0); // F
      display.write(5, B00000110, 0); // I

      bool blinkStatus = (millis() - modeSelectedTime) % 1000 > 500;

      if (blinkStatus) {
        display.write(2, B00000101, 0); // r
        display.write(1, B00110111, 0); // H
      } else {
        display.write(2, B00001000, 0);
        display.write(1, B00001000, 0);
      }
      
      showHumidity(persistentConfiguration.humidifierLowerOnThreshold);
      break;
    }

    case humidifierUpperOffThreshold: {
      display.write(8, B00110111, 0); // H
      display.write(7, B00111110, 0); // U
      display.write(6, B01000111, 0); // F
      display.write(5, B00000110, 0); // I

      bool blinkStatus = (millis() - modeSelectedTime) % 1000 > 500;

      if (blinkStatus) {
        display.write(2, B00000101, 0); // r
        display.write(1, B00110111, 0); // H
      } else {
        display.write(2, B01000000, 0);
        display.write(1, B01000000, 0);
      }
      
      showHumidity(persistentConfiguration.humidifierUpperOffThreshold);
      break;
    }
  }
}

/**
   Prints the status of the given bool to the right side (4 digits written), either _OFF or __ON.
*/
void printBooleanStatus(bool status) {
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

void printTempAndHumidity(DisplayData& data, void* signsFunction()) {
  bool blinkStatus = (millis() - modeSelectedTime) % 1000 > 500;

  if (blinkStatus || signsFunction == NULL) {
    printCelsiusAndRelHumiditySigns();
  } else if (signsFunction != NULL) {
    (*signsFunction)();
  }

  showTemperature(data.temperature);
  showHumidity(data.humidity);
}

void updateRelayStatus() {
  if (!humidifierOn && liveData.humidity <= HUMIDIFIER_ON_LOWER_THRESHOLD) {
    humidifierOn = true;
  } else if (humidifierOn && liveData.humidity >= HUMIDIFIER_OFF_UPPER_THRESHOLD) {
    humidifierOn = false;
  }

  digitalWrite(PIN_HUMIDIFIER_RELAY, !(humidifierOn && persistentConfiguration.humidifierEnabled));
}

void printCelsiusAndRelHumiditySigns() {
  display.write(6, B01100011, 0); // °
  display.write(5, B01001110, 0); // C

  display.write(2, B00000101, 0); // r
  display.write(1, B00110111, 0); // H
}

void printShowingMaximumSigns() {
  display.write(6, B01000000, 0);
  display.write(5, B01000000, 0);

  display.write(2, B01000000, 0);
  display.write(1, B01000000, 0);
}

void printShowingMinimumSigns() {
  display.write(6, B00001000, 0);
  display.write(5, B00001000, 0);

  display.write(2, B00001000, 0);
  display.write(1, B00001000, 0);
}

void printBlinkingError() {
  display.write(8, 0, 0);
  display.write(7, 0, 0);
  display.write(6, 0, 0);

  display.write(5, B01001111, 0);
  display.write(4, B00000101, 0);
  display.write(3, B00000101, 0);
  display.write(2, B00011101, 0);
  display.write(1, B00000101, 0);

  for (int i = 0; i < 3; i++) {
    delay(250);
    display.off();
    delay(250);
    display.on();
  }
}

static byte RESET_ANIMATION_MAX[] = {6, 1, 2, 3, 4, 5};
static byte RESET_ANIMATION_MIN[] = {3, 4, 5, 6, 1, 2};

void printResetAnimation() {
  if (currentMode == min) {
    printResetAnimationWithSegments(RESET_ANIMATION_MIN);
  } else if (currentMode == max) {
    printResetAnimationWithSegments(RESET_ANIMATION_MAX);
  }
}

void printResetAnimationWithSegments(byte activeSegment[]) {
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

  updateDisplayData();
}
