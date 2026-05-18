#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ThingSpeak.h>
#include <EEPROM.h>
#include <math.h>

#define SDA_PIN D2
#define SCL_PIN D1
#define MPU_ADDR 0x68M

char ssid[] = "Nan sevapu";
char pass[] = "7397769774";

WiFiClient client;

unsigned long channelID = 3327729;
const char * writeAPIKey = "P8J43EBXX3SWTJ96";

float baselineRMS = 0;
bool baselineStored = false;

float AccX, AccY, AccZ;
float RMS;

// smoothing
#define FILTER_SIZE 10
float rmsBuffer[FILTER_SIZE];
int bufferIndex = 0;

unsigned long lastUpdate = 0;
int warmupCounter = 0;

#define BASELINE_SAMPLES 50

void setup()
{
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(50000);

  EEPROM.begin(512);

  wakeMPU();

  delay(2000);  // stabilization

  readBaseline();

  connectWiFi();

  Serial.println("Type T + ENTER to retrain fingerprint");
}

void loop()
{
  warmupCounter++;

  checkSerialCommand();

  if(!readMPU())
  {
    Serial.println("MPU Read Error");
    return;
  }

  float rawRMS = calculateRMS();
  RMS = smoothRMS(rawRMS);

  Serial.print("Current RMS = ");
  Serial.println(RMS);

  if(!baselineStored && warmupCounter > 5)
  {
    trainFingerprint();
  }

  int status = compareFingerprint();

  printStatus(status);

  uploadToThingSpeak(status);

  delay(2000);
}

void wakeMPU()
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
}

bool readMPU()
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);

  if(Wire.endTransmission(false) != 0)
    return false;

  if(Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6) != 6)
    return false;

  AccX = Wire.read()<<8 | Wire.read();
  AccY = Wire.read()<<8 | Wire.read();
  AccZ = Wire.read()<<8 | Wire.read();

  return true;
}

float calculateRMS()
{
  return sqrt((AccX*AccX + AccY*AccY + AccZ*AccZ)/3.0) / 16384.0;
}

// smoothing filter
float smoothRMS(float newValue)
{
  rmsBuffer[bufferIndex] = newValue;
  bufferIndex = (bufferIndex + 1) % FILTER_SIZE;

  float sum = 0;
  for(int i=0;i<FILTER_SIZE;i++)
    sum += rmsBuffer[i];

  return sum / FILTER_SIZE;
}

void trainFingerprint()
{
  Serial.println("Training fingerprint... KEEP MOTOR STEADY");

  delay(2000);  // stabilize before capture

  float sum = 0;

  for(int i=0;i<BASELINE_SAMPLES;i++)
  {
    if(readMPU())
    {
      sum += calculateRMS();
    }
    delay(50);
  }

  baselineRMS = sum / BASELINE_SAMPLES;

  EEPROM.put(0, baselineRMS);
  EEPROM.commit();

  baselineStored = true;

  Serial.print("Fingerprint stored: ");
  Serial.println(baselineRMS);
}

int compareFingerprint()
{
  float diff = abs(RMS - baselineRMS);

  if(diff < 0.20)
    return 0;   // NORMAL

  else if(diff < 0.50)
    return 1;   // WARNING

  else
    return 2;   // FAULT
}

void printStatus(int status)
{
  if(status == 0)
    Serial.println("STATUS: NORMAL");

  else if(status == 1)
    Serial.println("STATUS: WARNING");

  else
    Serial.println("STATUS: FAULT");
}

void uploadToThingSpeak(int status)
{
  if(millis() - lastUpdate > 15000)
  {
    ThingSpeak.setField(1, RMS);
    ThingSpeak.setField(2, baselineRMS);
    ThingSpeak.setField(3, status);

    ThingSpeak.writeFields(channelID, writeAPIKey);

    lastUpdate = millis();
  }
}

void connectWiFi()
{
  WiFi.begin(ssid, pass);

  Serial.print("Connecting WiFi");

  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" Connected");

  ThingSpeak.begin(client);
}

void readBaseline()
{
  EEPROM.get(0, baselineRMS);

  if(baselineRMS > 0 && baselineRMS < 5)
  {
    baselineStored = true;

    Serial.print("Loaded baseline: ");
    Serial.println(baselineRMS);
  }
}

void checkSerialCommand()
{
  if(Serial.available())
  {
    char input = Serial.read();

    if(input == 'T' || input == 't')
    {
      baselineStored = false;
      Serial.println("Retraining...");
    }
  }
}
