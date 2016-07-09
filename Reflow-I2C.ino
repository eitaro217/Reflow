#define MAX6675 1
#define MAX31855 2

// MAX6675/MAX31855
#define THERMO_CHIP MAX6675

#define allOFF 0
#define upON   1
#define downON 2
#define allON  3
#define ControlDataLen 3
int temperature_control_data[ControlDataLen + 1][3] = {
  {allON , 170, 100},  // 1&2ON , temperature170, keep100sec
  {allON , 235,   0},  // 1&2ON , temperature235, keep0sec
  {downON, 200,  30},  // 2ON   , temperature200, keep30sec
  {allOFF,   0,   0}   // 1&2OFF, temperature0  , keep0sec
};

#include <SPI.h>
#include <Switches.h> // switch control
#include "U8glib.h"

// setup u8g object, please remove comment from one of the following constructor calls
// IMPORTANT NOTE: The following list is incomplete. The complete list of supported
// devices with all constructor calls is here: https://github.com/olikraus/u8glib/wiki/device
U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST); // Fast I2C / TWI

// switches
#define START_SW_PIN A2 // スタートスイッチ
#define STOP_SW_PIN  A1 // ストップスイッチ

/** ************************************************************************** */
/**
   push switch
*/
Switches sw_start;
Switches sw_stop;

// Beep
#define TonePin 4 // Buzzer

// Tempratier
#define TemperatureSlavePin 10 // ~~SS
#define TemperatureMisoPin  12 // MISO
#define TemperatureSckPin   13 // SCK

// PowerControl
#define Heat1Pin 5
#define Heat2Pin 6

// colling fan (0-255)
#define FAN_PWM_PIN 9
#define FAN_PWM_STOP   0    // 待機時のファン回転
#define FAN_PWM_RUN   90    // 通常時のファン回転
#define FAN_PWM_COOL 160    // 冷却時のファン回転（最大）

// blink
#define BLINK_TIME   1      // ブリンク時間(sec)

// graph
#define GRAPH_TIME    3     // グラフを描く間隔(sec)
#define GRAPH_HEIGHT 24     // LCDの垂直方向のピクセル数
#define GRAPH_WIDTH 128     // グラフの水平方向のピクセル数
#define GRAPH_LOW 10.0      // グラフの下限の温度
#define GRAPH_HIGH 270.0    // グラフの上限の温度
byte TempHist[GRAPH_WIDTH]; // 過去の温度のグラフの長さ

// delay Wait(msec)
#define DELAY_WAIT 200

//
byte state;               // main program mode
byte heatMode;            // UpDown heater mode
byte heatState;           // UpDown heater status
byte fanState;            // fan status
byte tableCounter;        // data table counter
float temperature;        // Temperature
float temperatureMax;     // target Temprature
boolean blinkFlag;        // blink ON/OFF flag
unsigned long blinkTimer; // blink timer
unsigned long loopTimer;  // loop wait timer
unsigned long graphTimer; // graph update timer
long keepTimer;           // temprature keep timer
unsigned long subTimer;   // keep timer sub
unsigned long time;       // tmp

byte errorStatus = 0;

// set font donfigure
void u8g_prepare(void) {
  u8g.setFontRefHeightExtendedText();
  u8g.setDefaultForegroundColor();
  u8g.setFontPosTop();
}

// set char-locale and draw String
void drawStr(int x, int y, const char *s) {
  u8g.drawStr(x * u8g.getStrWidth("W"), y * u8g.getFontLineSpacing(), s);
}

void setTempratureData() {
  heatMode = temperature_control_data[tableCounter][0];
  temperatureMax = temperature_control_data[tableCounter][1];
  keepTimer = (long) temperature_control_data[tableCounter][2] * 1000;
  heatState = heatMode;
}

void tempratureRead() {
  unsigned int thermocouple;
  unsigned int internal;
  float disp;
  errorStatus = 0;

#if THERMO_CHIP==MAX6675  //MAX6675
  // 連続で読み込むとちゃんと読まないので対策。2回に一回読み込み
  static boolean readTempFlag = true;

  if (readTempFlag == true) {
    // read temp
    digitalWrite(TemperatureSlavePin, LOW);
    thermocouple = (unsigned int)SPI.transfer16(0x0000);
    digitalWrite(TemperatureSlavePin, HIGH);

    if ((thermocouple & 0x0004) != 0) {
      errorStatus = 0xFF;
    } else {
      temperature = (thermocouple >> 3) * 0.25;
    }
  }
  readTempFlag = !readTempFlag;

#else // MAX31855
  digitalWrite(TemperatureSlavePin, LOW);
  thermocouple = (unsigned int)SPI.transfer16(0x0000);
  internal = (unsigned int)SPI.transfer16(0x0000);
  digitalWrite(TemperatureSlavePin, HIGH);

  if ((thermocouple & 0x0001) != 0) {
    if ((internal & 0x0004) != 0) {
      errorStatus |= B00000001; // Short to Vcc
    }
    if ((internal & 0x0002) != 0) {
      errorStatus |= B00000010; // Short to GND
    }
    if ((internal & 0x0001) != 0) {
      errorStatus |= B00000100; // Open Circuit
    }
  } else {
    if ((thermocouple & 0x8000) == 0) {
      temperature = (thermocouple >> 2) * 0.25;
    } else {
      temperature = (0x3fff - (thermocouple >> 2) + 1)  * -0.25;
    }
  }
#endif
}

void heatControl() {
  if (temperature > temperatureMax) {
    heatState = 0;
  } else if (temperature < (temperatureMax - 0.5)) {
    heatState = heatMode;
  }
  if ((heatState & 1) == 0) {
    digitalWrite(Heat1Pin, LOW);
  } else {
    digitalWrite(Heat1Pin, HIGH);
  }
  if ((heatState & 2) == 0) {
    digitalWrite(Heat2Pin, LOW);
  } else {
    digitalWrite(Heat2Pin, HIGH);
  }

  // fan
  if (state > 1 && state < 5) {
    fanState = 2;
    analogWrite(FAN_PWM_PIN, FAN_PWM_RUN);

  } else {
    if (temperature > 150.0) {
      fanState = 2;
      analogWrite(FAN_PWM_PIN, FAN_PWM_RUN);
    } else if (temperature > 60.0) {
      fanState = 1;
      analogWrite(FAN_PWM_PIN, FAN_PWM_COOL);
    } else {
      if (fanState == 1) {
        tone(TonePin, 900, 1200); // EndSound
      }
      fanState = 0;
      if (temperature > 45.0) {
        analogWrite(FAN_PWM_PIN, FAN_PWM_RUN);
      } else {
        analogWrite(FAN_PWM_PIN, FAN_PWM_STOP);
      }
    }
  }
}

unsigned long defMillis(unsigned long *beforeMillis, unsigned long *nowMillis) {
  *nowMillis = millis();
  if (*nowMillis > *beforeMillis) {
    return *nowMillis - *beforeMillis;
  } else {
    return (unsigned long) 0 - *beforeMillis  + *nowMillis;
  }
}

void draw() {
  char strTmp[32];
  u8g.setFont(u8g_font_7x13B);
  u8g_prepare();
  if (errorStatus == 0) {
    drawStr(0, 0, "STATUS:");
    switch (state) {
      case 0: // initialize
      case 1: // start switch wait
        drawStr(7, 0, "-------");
        if (blinkFlag == true) {
          drawStr(2, 1, "PUSH START!");
        }
        drawStr(1, 4, "SABAKAN of EITARO");
        break;
      case 2: // target Temperature
      case 3: // keep time
      case 4: // Loop or Finish?
      case 5: // finish switch wait
        if (state != 5) {
          if (blinkFlag == true) {
            drawStr(7, 0, "RUNNING");
          }
        } else {
          drawStr(7, 0, "FINISH!");
        }
        if ((heatState & 1) == 0) {
          drawStr(0, 1, "H1:OFF ");
        } else {
          drawStr(0, 1, "H1:ON  ");
        }
        if ((heatState & 2) == 0) {
          drawStr(7, 1, "H2:OFF");
        } else {
          drawStr(7, 1, "H2:ON");
        }

        drawStr(5, 3, "WAIT:");
        if (state == 3) {
          sprintf(strTmp, "%3d.%dsec", (int) (keepTimer / 1000), (int) ((keepTimer % 1000) / 100));
          drawStr(10, 3, strTmp);
        } else {
          drawStr(10, 3, "---.-");
        }
        break;
    }

    sprintf(strTmp, "%3d.%02d / %3d.%02d", (int) temperature, (int) ((temperature - (float) (int) temperature) * 100),
            (int) temperatureMax, (int) ((temperatureMax - (float) (int) temperatureMax) * 100));
    drawStr(2, 2, strTmp);

    // fan status
    if (state > 1) {
      switch (fanState) {
        case 0:
          drawStr(0, 3, "STOP ");
          break;
        case 1:
          drawStr(0, 3, "COOL ");
          break;
        case 2:
          drawStr(0, 3, "RUN  ");
          break;
      }
    }
    drawGraph();
  } else {  // Thermometer error
    if (blinkFlag == true) {
      u8g.setFont(u8g_font_helvB12);
      u8g_prepare();
      drawStr(0, 0, "ERROR!");
      u8g.setFont(u8g_font_7x13B);
      u8g_prepare();
    }
    int x;
    x = 2;
    if (errorStatus & B10000000) {
      drawStr(0, x, "Thermometer error.");
      x += 1;
    }
    if (errorStatus & B00000001) {
      drawStr(2, x, "Short to Vcc.");
      x += 1;
    }
    if (errorStatus & B00000010) {
      drawStr(2, x, "Short to GND.");
      x += 1;
    }
    if (errorStatus & B00000100) {
      drawStr(2, x, "Open Circuit.");
      x += 1;
    }
  }

  // blink control
  if (defMillis(&blinkTimer, &time) > BLINK_TIME * 1000) {
    blinkTimer = time;
    blinkFlag = !blinkFlag;
  }
}

void drawGraph() {
  if (defMillis(&graphTimer, &time) > GRAPH_TIME * 1000) {
    graphTimer = time;
    for (int i = 1; i < GRAPH_WIDTH; i++) {
      TempHist[i - 1] = TempHist[i];
    } // for i
    if (temperature <= GRAPH_LOW) {
      TempHist[GRAPH_WIDTH - 1] = 0;
    } else if (temperature > GRAPH_HIGH) {
      TempHist[GRAPH_WIDTH - 1] = GRAPH_HEIGHT;
    } else {
      TempHist[GRAPH_WIDTH - 1] = byte((temperature - GRAPH_LOW) / (GRAPH_HIGH - GRAPH_LOW) * GRAPH_HEIGHT);
    } // if
  } // if

  for (int i = 0; i < GRAPH_WIDTH; i++) {
    int x = i;
    if (TempHist[i] > 0) {
      u8g.drawLine((int) x, (int) (u8g.getHeight() - 1), (int) x, (int) (u8g.getHeight() - (int) TempHist[i]));
    } // if
  } // for i
}

void setup(void) {
  // u8g
  // flip screen, if required
  // u8g.setRot180();

  // set SPI backup if required
  //u8g.setHardwareBackup(u8g_backup_avr_spi);

  // assign default color value
  if ( u8g.getMode() == U8G_MODE_R3G3B2 ) {
    u8g.setColorIndex(255);     // white
  }
  else if ( u8g.getMode() == U8G_MODE_GRAY2BIT ) {
    u8g.setColorIndex(3);         // max intensity
  }
  else if ( u8g.getMode() == U8G_MODE_BW ) {
    u8g.setColorIndex(1);         // pixel on
  }
  else if ( u8g.getMode() == U8G_MODE_HICOLOR ) {
    u8g.setHiColorByRGB(255, 255, 255);
  }

  // degug Initialize(SerialMonitor)
  Serial.begin(9600);

  // TempHistの初期化(要らんかも)
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    TempHist[i] = 0;
  } // for i

  // FAN
  analogWrite(FAN_PWM_PIN, FAN_PWM_STOP);

  // PowerControl initialize
  pinMode(Heat1Pin, OUTPUT);
  pinMode(Heat2Pin, OUTPUT);

  // Temprature initialize
  pinMode(TemperatureSlavePin, OUTPUT);
  digitalWrite(TemperatureSlavePin, HIGH);
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV4);
  SPI.setDataMode(SPI_MODE0);

  //push switch
  pinMode(START_SW_PIN, INPUT_PULLUP);
  pinMode(STOP_SW_PIN,  INPUT_PULLUP);
  sw_start.init(START_SW_PIN, true, SWITCH_NLEVEL);
  sw_stop.init(STOP_SW_PIN, true, SWITCH_NLEVEL);

  // memory initialize
  state = 0;

  blinkTimer = millis();
  graphTimer = millis();
  loopTimer = millis();
}

void loop(void) {
  tempratureRead();
  if (errorStatus != 0) {

  } else {

    switch (state) {
      case 0: // initialize
        heatMode = 0;
        temperatureMax = 0.0;
        tableCounter = 0;
        state++;
        break;
      case 1: // start switch wait
        if (sw_start() == true) {
          tone(TonePin, 600, 800); // StartSound
          setTempratureData();
          state++;
        }
        break;
      case 2: // target Temperature
        if (temperatureMax <= temperature) {
          state++;
          subTimer = millis();
        }
        break;
      case 3: // keep time
        keepTimer = keepTimer - defMillis(&subTimer, &time);
        subTimer = time;
        //  Serial.print(keepTimer);
        //  Serial.print(",");
        //  Serial.print(subTimer);
        //  Serial.println();
        if (keepTimer <= 0) {
          state++;
        }

        break;
      case 4: // Loop or Finish?
        tableCounter++;
        setTempratureData();
        if (tableCounter < ControlDataLen) {
          tone(TonePin, 1200, 150); // LoopSound
          state = 2;
        } else {
          tone(TonePin, 600, 1500); // FinishSound
          state++;
        }
        break;
      case 5: // finish switch wait
        if (sw_start() == true) {
          tone(TonePin, 1500, 150); // ResetSound
          state = 0;
        }
        break;
    }
    if (sw_stop() == true) {
      state = 0;
      heatState = 0;
      tone(TonePin, 294, 1500); // ResetSound
    }

    heatControl();
  }
  u8g.firstPage();
  do {
    draw();
  } while ( u8g.nextPage() );

  unsigned long waitTime;
  waitTime = defMillis(&loopTimer, &time);
  loopTimer = time;
  if (DELAY_WAIT > waitTime) {
    delay(DELAY_WAIT - waitTime);
  }
}

