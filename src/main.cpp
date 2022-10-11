/*******************************************************************************************************************
 *
 *                         ESP32Cam timelapse sketch using Arduino IDE or PlatformIO
 *                                    Based on my demo sketch: https://github.com/alanesq/ESP32Cam-demo
 *
 *                                     Tested with ESP32 board manager version  1.0.6
 *
 *      Note:  You can use ffmpeg to combine the resulting files in to a Video - https://www.ffmpeg.org/
 *             command = ffmpeg -framerate 10 -pattern_type glob -i '*.jpg' -c:v libx264 -profile:v high -crf 20 -pix_fmt yuv420p timelapse.mp4
 *
 ********************************************************************************************************************/

#if !defined ESP32
#error This sketch is only for an ESP32Cam module
#endif

#include "esp_camera.h" // https://github.com/espressif/esp32-camera

//   ---------------------------------------------------------------------------------------------------------

//         Wifi Settings - uncomment and set here if not stored in platformio.ini

//  #define SSID_NAME "Ap14"
//  #define SSID_PASWORD "12345678"

//   ---------------------------------------------------------------------------------------------------------

// Required by PlatformIO

#include <Arduino.h>

// forward declarations
bool initialiseCamera();
bool cameraImageSettings();
String localTime();
void flashLED(int reps);
bool storeImage();
void handleRoot();
void handlePhoto();
void handleNotFound();
bool getNTPtime(int sec);
bool handleJPG();
void handleStream();
int requestWebPage(String *, String *, int);
void handleTest();
void brightLed(byte ledBrightness);
void setupFlashPWM();
void changeResolution(framesize_t);
void resetCamera(bool);
void handleData();

// ---------------------------------------------------------------
//                           -SETTINGS
// ---------------------------------------------------------------

const char *stitle = "ESP32Cam_Time-lapse"; // title of this sketch
const char *sversion = "27Jan22";           // Sketch version

bool timelapseEnabled = 0;     // set to 1 to start recording at startup
float timeBetweenShots = 10.0; // default time between image captures (seconds)

byte flashRequired = 0;     // default flash status: 0=no flash, 1=use flash, 2=use external flash (via gpio pin), 3=use flash and external
uint32_t extFlashDelay = 0; // delay after turning external light on before taking picture (ms)

const bool serialDebug = 1;   // show debug info. on serial port (1=enabled, disable if using pins 1 and 3 as gpio)
uint32_t sdReadTimeout = 25;  // timeout if its taking too long to read all the files on the sd card (seconds)
uint16_t datarefresh = 3100;  // how often to refresh data on root web page (ms)
uint16_t imagerefresh = 6050; // how often to refresh the image on root web page (ms)

// Access point wifi settings (used if unable to connect to wifi)
const uint32_t wifiTimeout = 12; // timeout when connecting to wifi in seconds
const char *APSSID = "timelapse";
const char *APPWD = "12345678";
IPAddress Ip(192, 168, 4, 1);      // ip address
IPAddress NMask(255, 255, 255, 0); // net mask

// Camera related
framesize_t FRAME_SIZE_IMAGE = FRAMESIZE_XGA; // Image resolution:
                                              //               default = "const framesize_t FRAME_SIZE_IMAGE = FRAMESIZE_VGA"
                                              //               160x120 (QQVGA), 128x160 (QQVGA2), 176x144 (QCIF), 240x176 (HQVGA),
                                              //               320x240 (QVGA), 400x296 (CIF), 640x480 (VGA, default), 800x600 (SVGA),
                                              //               1024x768 (XGA), 1280x1024 (SXGA), 1600x1200 (UXGA)
#define PIXFORMAT PIXFORMAT_JPEG;             // image format, Options =  YUV422, GRAYSCALE, RGB565, JPEG, RGB888
int cameraImageExposure = 0;                  // Camera exposure (0 - 1200)   If gain and exposure both set to zero then auto adjust is enabled
int cameraImageGain = 0;                      // Image gain (0 - 30)
int cameraImageBrightness = 0;                // Image brightness (-2 to +2)

const int TimeBetweenStatus = 600; // speed of flashing system running ok status light (milliseconds)

const int indicatorLED = 33; // onboard small LED pin (33)

// Bright LED (Flash)
const int brightLED = 4;      // onboard Illumination/flash LED pin (4)
int brightLEDbrightness = 0;  // initial brightness (0 - 255)
const int ledFreq = 5000;     // PWM settings
const int ledChannel = 15;    // camera uses timer1
const int ledRresolution = 8; // resolution (8 = from 0 to 255)

const int iopinA = 13; // general io pin 13
const int iopinB = 12; // general io pin 12 (must not be high at boot)

const int serialSpeed = 115200; // Serial data speed to use

// NTP - Internet time
const char *ntpServer = "pool.ntp.org";
const char *TZ_INFO = "GMT+0BST-1,M3.5.0/01:00:00,M10.5.0/02:00:00"; // enter your time zone (https://remotemonitoringsystems.ca/time-zone-abbreviations.php)
long unsigned lastNTPtime;
tm timeinfo;
time_t now;

// camera settings (for the standard - OV2640 - CAMERA_MODEL_AI_THINKER)
// see: https://randomnerdtutorials.com/esp32-cam-camera-pin-gpios/
// set camera resolution etc. in 'initialiseCamera()' and 'cameraImageSettings()'
#define CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32  // power to camera (on/off)
#define RESET_GPIO_NUM -1 // -1 = not used
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26 // i2c sda
#define SIOC_GPIO_NUM 27 // i2c scl
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25 // vsync_pin
#define HREF_GPIO_NUM 23  // href_pin
#define PCLK_GPIO_NUM 22  // pixel_clock_pin

// ******************************************************************************************************************

//#include "esp_camera.h"         // https://github.com/espressif/esp32-camera
// #include "camera_pins.h"
#include <base64.h> // for encoding buffer to display image on page
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "driver/ledc.h" // used to configure pwm on illumination led

WebServer server(80); // serve web pages on port 80

// Used to disable brownout detection
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// sd-card
#include "SD_MMC.h" // sd card - see https://randomnerdtutorials.com/esp32-cam-take-photo-save-microsd-card/
#include <SPI.h>
#include <FS.h> // gives file access
#define SD_CS 5 // sd chip select pin = 5

// Define some global variables:
uint32_t lastStatus = millis();       // last time status light changed status (to flash all ok led)
bool sdcardPresent;                   // flag if an sd card is detected
uint32_t imageCounter;                // image file name on sd card counter
String spiffsFilename = "/image.jpg"; // image name to use when storing in spiffs
String ImageResDetails = "Unknown";   // image resolution info

// ******************************************************************************************************************

// ---------------------------------------------------------------
//    -SETUP     SETUP     SETUP     SETUP     SETUP     SETUP
// ---------------------------------------------------------------

void setup()
{

  if (serialDebug)
  {
    Serial.begin(serialSpeed); // Start serial communication
    // Serial.setDebugOutput(true);

    Serial.println("\n\n\n"); // line feeds
    Serial.println("-----------------------------------");
    Serial.printf("Starting - %s - %s \n", stitle, sversion);
    Serial.println("-----------------------------------");
    // Serial.print("Reset reason: " + ESP.getResetReason());
  }

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Turn-off the 'brownout detector'

  // small indicator led on rear of esp32cam board
  pinMode(indicatorLED, OUTPUT);

  // Connect to wifi
  digitalWrite(indicatorLED, LOW); // small indicator led on
  if (serialDebug)
  {
    Serial.print("\nConnecting to ");
    Serial.print(SSID_NAME);
    Serial.print("\n   ");
  }
  WiFi.begin(SSID_NAME, SSID_PASWORD);
  uint32_t timera = millis();
  while (WiFi.status() != WL_CONNECTED && (unsigned long)(millis() - timera) < (wifiTimeout * 1000))
  {
    delay(500);
    if (serialDebug)
      Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (serialDebug)
    {
      Serial.print("\nWiFi connected, ");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }
  }
  else
  {
    // not connected to wifi so set up as an access point instead
    WiFi.mode(WIFI_AP);
    if (serialDebug)
      Serial.print("Unable to connect to wifi so starting access point: ");
    if (serialDebug)
      Serial.print(APSSID);
    WiFi.softAP(APSSID, APPWD);
    delay(150);
    WiFi.softAPConfig(Ip, Ip, NMask);
    IPAddress myIP = WiFi.softAPIP();
    if (serialDebug)
      Serial.print("Started - IP: ");
    if (serialDebug)
      Serial.println(myIP);
  }

  server.begin();                   // start web server
  digitalWrite(indicatorLED, HIGH); // small indicator led off

  // define the web pages (i.e. call these procedures when url is requested)
  server.on("/", handleRoot);         // root page
  server.on("/data", handleData);     // suplies data to periodically update root (AJAX)
  server.on("/jpg", handleJPG);       // capture image and send as jpg
  server.on("/stream", handleStream); // stream live video
  server.on("/photo", handlePhoto);   // save image to sd card
  server.on("/test", handleTest);     // Testing procedure
  server.onNotFound(handleNotFound);  // invalid url requested

  // NTP - internet time
  if (serialDebug)
    Serial.println("\nGetting real time (NTP)");
  configTime(0, 0, ntpServer);
  setenv("TZ", TZ_INFO, 1);
  if (getNTPtime(10))
  { // wait up to 10 sec to sync
  }
  else
  {
    if (serialDebug)
      Serial.println("Time not set");
  }
  lastNTPtime = time(&now);

  // set up camera
  if (serialDebug)
    Serial.print(("\nInitialising camera: "));
  if (initialiseCamera())
  {
    if (serialDebug)
      Serial.println("OK");
  }
  else
  {
    if (serialDebug)
      Serial.println("failed");
  }

  // SD Card - if one is detected set 'sdcardPresent' High
  if (serialDebug)
    Serial.println("Opening sd card");
  if (!SD_MMC.begin("/sdcard", true))
  { // if loading sd card fails
    // note: ('/sdcard", true)' = 1bit mode - see: https://www.reddit.com/r/esp32/comments/d71es9/a_breakdown_of_my_experience_trying_to_talk_to_an/
    if (serialDebug)
      Serial.println("No SD Card detected");
    sdcardPresent = 0; // flag no sd card available
  }
  else
  {
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE)
    { // if invalid card found
      if (serialDebug)
        Serial.println("SD Card type detect failed");
      sdcardPresent = 0; // flag no sd card available
    }
    else
    {
      // valid sd card detected
      uint32_t SDfreeSpace = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
      if (serialDebug)
        Serial.printf("SD Card found, free space = %dMB \n", SDfreeSpace);
      sdcardPresent = 1; // flag sd card available
    }
  }
  fs::FS &fs = SD_MMC; // sd card file system
  if (!sdcardPresent)
  {
    if (serialDebug)
      Serial.println("Error: no sd card detected");
  }

  // discover the number of image files already stored in '/img' folder of the sd card and set image file counter accordingly
  imageCounter = 0;
  uint32_t tTimer = millis(); // for timeout if there are too many files
  if (sdcardPresent)
  {
    int tq = fs.mkdir("/img"); // create the '/img' folder on sd card (in case it is not already there)
    if (!tq)
    {
      if (serialDebug)
        Serial.println("Unable to create IMG folder on sd card");
    }

    // open the image folder and step through all files in it
    File root = fs.open("/img");
    while ((millis() - tTimer) < (sdReadTimeout * 1000))
    {
      File entry = root.openNextFile(); // open next file in the folder
      if (!entry)
        break;        // if no more files in the folder
      imageCounter++; // increment image counter
      entry.close();
    }
    root.close();
    if ((millis() - tTimer) >= sdReadTimeout)
    {
      imageCounter = 0;
      if (serialDebug)
        Serial.println("Taking too long to count the files on sd card so setting to 0");
    }
    else
    {
      if (serialDebug)
        Serial.printf("Image file count = %d \n", imageCounter);
    }
  }

  // define i/o pins
  pinMode(indicatorLED, OUTPUT);    // defined again as sd card config can reset it
  digitalWrite(indicatorLED, HIGH); // led off = High
  pinMode(iopinA, INPUT);           // pin 13 - free io pin, can be used for input or output
  pinMode(iopinB, OUTPUT);          // pin 12 - free io pin, can be used for input or output (must not be high at boot)

  setupFlashPWM(); // configure PWM for the illumination LED

  // startup complete
  if (serialDebug)
    Serial.println("\nStarted...");
  flashLED(2);   // flash the onboard indicator led
  brightLed(64); // change bright LED
  delay(100);
  brightLed(0); // change bright LED

} // setup

// ******************************************************************************************************************

// ----------------------------------------------------------------
//   -LOOP     LOOP     LOOP     LOOP     LOOP     LOOP     LOOP
// ----------------------------------------------------------------

void loop()
{

  server.handleClient(); // handle any incoming web page requests

  //  record timelapse image
  static uint32_t lastCamera = millis(); // last time an image was captured
  if (timelapseEnabled)
  {
    int tTime = timeBetweenShots * 1000;
    if (((unsigned long)(millis() - lastCamera) >= tTime) && sdcardPresent)
    {
      lastCamera = millis(); // reset timer
      storeImage();          // save an image to sd card
      if (serialDebug)
        Serial.println("Time lapse image captured");
    }
  }

  // flash status LED to show sketch is running ok
  if ((unsigned long)(millis() - lastStatus) >= TimeBetweenStatus)
  {
    lastStatus = millis();                                  // reset timer
    digitalWrite(indicatorLED, !digitalRead(indicatorLED)); // flip indicator led status
  }

} // loop

// ******************************************************************************************************************

// ----------------------------------------------------------------
//                        Initialise the camera
// ----------------------------------------------------------------
// returns TRUE if successfull
// see:  https://github.com/espressif/esp32-camera

bool initialiseCamera()
{

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;       // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
  config.pixel_format = PIXFORMAT;      // Options =  YUV422, GRAYSCALE, RGB565, JPEG, RGB888
  config.frame_size = FRAME_SIZE_IMAGE; // Image sizes: 160x120 (QQVGA), 128x160 (QQVGA2), 176x144 (QCIF), 240x176 (HQVGA), 320x240 (QVGA),
                                        //              400x296 (CIF), 640x480 (VGA, default), 800x600 (SVGA), 1024x768 (XGA), 1280x1024 (SXGA),
                                        //              1600x1200 (UXGA)
  config.jpeg_quality = 15;             // 0-63 lower number means higher quality (can cause failed image capture if set too low at higher resolutions)
  config.fb_count = 1;                  // if more than one, i2s runs in continuous mode. Use only with JPEG

  // check the esp32cam board has a psram chip installed (extra memory used for storing captured images)
  //    Note: if not using "AI thinker esp32 cam" in the Arduino IDE, SPIFFS must be enabled
  if (!psramFound())
  {
    if (serialDebug)
      Serial.println("Warning: No PSRam found so defaulting to image size 'CIF'");
    config.frame_size = FRAMESIZE_CIF;
  }

  //#if defined(CAMERA_MODEL_ESP_EYE)
  //  pinMode(13, INPUT_PULLUP);
  //  pinMode(14, INPUT_PULLUP);
  //#endif

  esp_err_t camerr = esp_camera_init(&config); // initialise the camera
  if (camerr != ESP_OK)
  {
    if (serialDebug)
      Serial.printf("ERROR: Camera init failed with error 0x%x", camerr);
  }

  cameraImageSettings(); // apply custom camera settings

  return (camerr == ESP_OK); // return boolean result of camera initialisation
}

// ******************************************************************************************************************

// ----------------------------------------------------------------
//                   -Change camera image settings
// ----------------------------------------------------------------
// Adjust image properties (brightness etc.)
// Defaults to auto adjustments if exposure and gain are both set to zero
// - Returns TRUE if successful
// More info: https://randomnerdtutorials.com/esp32-cam-ov2640-camera-settings/
//            interesting info on exposure times here: https://github.com/raduprv/esp32-cam_ov2640-timelapse

bool cameraImageSettings()
{

  if (serialDebug)
    Serial.println("Applying camera settings");

  sensor_t *s = esp_camera_sensor_get();
  // something to try?:     if (s->id.PID == OV3660_PID)
  if (s == NULL)
  {
    if (serialDebug)
      Serial.println("Error: problem reading camera sensor settings");
    return 0;
  }

  // if both set to zero enable auto adjust
  if (cameraImageExposure == 0 && cameraImageGain == 0)
  {
    // enable auto adjust
    s->set_gain_ctrl(s, 1);                      // auto gain on
    s->set_exposure_ctrl(s, 1);                  // auto exposure on
    s->set_awb_gain(s, 1);                       // Auto White Balance enable (0 or 1)
    s->set_brightness(s, cameraImageBrightness); // (-2 to 2) - set brightness
  }
  else
  {
    // Apply manual settings
    s->set_gain_ctrl(s, 0);                      // auto gain off
    s->set_awb_gain(s, 1);                       // Auto White Balance enable (0 or 1)
    s->set_exposure_ctrl(s, 0);                  // auto exposure off
    s->set_brightness(s, cameraImageBrightness); // (-2 to 2) - set brightness
    s->set_agc_gain(s, cameraImageGain);         // set gain manually (0 - 30)
    s->set_aec_value(s, cameraImageExposure);    // set exposure manually  (0-1200)
  }

  return 1;
} // cameraImageSettings

//    // More camera settings available:
//    // If you enable gain_ctrl or exposure_ctrl it will prevent a lot of the other settings having any effect
//    // more info on settings here: https://randomnerdtutorials.com/esp32-cam-ov2640-camera-settings/
//    s->set_gain_ctrl(s, 0);                       // auto gain off (1 or 0)
//    s->set_exposure_ctrl(s, 0);                   // auto exposure off (1 or 0)
//    s->set_agc_gain(s, cameraImageGain);          // set gain manually (0 - 30)
//    s->set_aec_value(s, cameraImageExposure);     // set exposure manually  (0-1200)
//    s->set_vflip(s, cameraImageInvert);           // Invert image (0 or 1)
//    s->set_quality(s, 10);                        // (0 - 63)
//    s->set_gainceiling(s, GAINCEILING_32X);       // Image gain (GAINCEILING_x2, x4, x8, x16, x32, x64 or x128)
//    s->set_brightness(s, cameraImageBrightness);  // (-2 to 2) - set brightness
//    s->set_lenc(s, 1);                            // lens correction? (1 or 0)
//    s->set_saturation(s, 0);                      // (-2 to 2)
//    s->set_contrast(s, cameraImageContrast);      // (-2 to 2)
//    s->set_sharpness(s, 0);                       // (-2 to 2)
//    s->set_hmirror(s, 0);                         // (0 or 1) flip horizontally
//    s->set_colorbar(s, 0);                        // (0 or 1) - show a testcard
//    s->set_special_effect(s, 0);                  // (0 to 6?) apply special effect
//    s->set_whitebal(s, 0);                        // white balance enable (0 or 1)
//    s->set_awb_gain(s, 1);                        // Auto White Balance enable (0 or 1)
//    s->set_wb_mode(s, 0);                         // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
//    s->set_dcw(s, 0);                             // downsize enable? (1 or 0)?
//    s->set_raw_gma(s, 1);                         // (1 or 0)
//    s->set_aec2(s, 0);                            // automatic exposure sensor?  (0 or 1)
//    s->set_ae_level(s, 0);                        // auto exposure levels (-2 to 2)
//    s->set_bpc(s, 0);                             // black pixel correction
//    s->set_wpc(s, 0);                             // white pixel correction

// ******************************************************************************************************************

//                          Misc -small procedures

// ----------------------------------------------------------------
//       set up PWM for the illumination LED (flash)
// ----------------------------------------------------------------
// note: I am not sure PWM is very reliable on the esp32cam - requires more testing
void setupFlashPWM()
{
  ledcSetup(ledChannel, ledFreq, ledRresolution);
  ledcAttachPin(brightLED, ledChannel);
  brightLed(brightLEDbrightness);
}

// ----------------------------------------------------------------
// change illumination LED brightness
// ----------------------------------------------------------------
void brightLed(byte ledBrightness)
{
  brightLEDbrightness = ledBrightness;  // store setting
  ledcWrite(ledChannel, ledBrightness); // change LED brightness (0 - 255)
  if (serialDebug)
    Serial.println("Brightness changed to " + String(ledBrightness));
}

// ----------------------------------------------------------------
//          returns the current real time as a String
// ----------------------------------------------------------------
//   see: https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/
String localTime()
{
  struct tm timeinfo;
  char ttime[40];
  if (!getLocalTime(&timeinfo))
    return "unknown";
  // strftime(ttime,40,  "%A, %B %d %Y %H:%M:%S", &timeinfo);
  strftime(ttime, 40, "%Y%m%d-%H%M%S", &timeinfo);
  return ttime;
}

/*
    Time/data format:
    %A	Full weekday name	Saturday
    %a	Abbreviated weekday name	Sat
    %B	Full month name	January
    %b or %h	Abbreviated month name	Jan
    %D	Short MM/DD/YY date	09/12/07
    %d	Day of the month (01-31)	12
    %F	Short YYYY-MM-DD date	2007-09-12
    %H	The hour in 24-hour format (00-23)	13
    %I	The hour in 12-hour format (01-12)	08
    %j	Day of the year (001-366)	78
    %Y	Year	2021
    %y	Last two digits of the year (00-99)	21
    %m	Month as a decimal number (01-12)	02
    %M	Minute (00-59)	12
    %p	AM or PM	AM
    %r	12-hour clock time	05:12:32 am
    %R	24-hour time HH: MM	13:22
    %S	Second (00-59)	32
    %T	Time format HH: MM: SS	09:12:01
*/

// ----------------------------------------------------------------
//        flash the indicator led 'reps' number of times
// ----------------------------------------------------------------
void flashLED(int reps)
{
  for (int x = 0; x < reps; x++)
  {
    digitalWrite(indicatorLED, LOW);
    delay(1000);
    digitalWrite(indicatorLED, HIGH);
    delay(500);
  }
}

// ----------------------------------------------------------------
//      send standard html header (i.e. start of web page)
// ----------------------------------------------------------------
void sendHeader(WiFiClient &client, char *hTitle)
{
  // Start page
  client.write("HTTP/1.1 200 OK\r\n");
  client.write("Content-Type: text/html\r\n");
  client.write("Connection: close\r\n");
  client.write("\r\n");
  client.write("<!DOCTYPE HTML><html lang='en'>\n");
  // HTML / CSS
  client.printf(R"=====(
        <head>
          <meta name='viewport' content='width=device-width, initial-scale=1.0'>
          <title>%s</title>
          <style>
            body {
              color: black;
              background-color: #FFFF00;
              text-align: center;
            }
            input, a {
              background-color: #FF9900;
              border: 2px #FF9900;
              color: blue;
              padding: 3px 6px;
              text-align: center;
              text-decoration: none;
              display: inline-block;
              font-size: 16px;
              cursor: pointer;
              border-radius: 7px;
            }
            input:hover, a:hover {
              background-color: #FF4400;
            }
          </style>
        </head>
        <body>
        <h1 style='color:red;'>%s</H1>
      )=====",
                hTitle, hTitle);
}

// ----------------------------------------------------------------
//       send standard html footer (i.e. end of web page)
// ----------------------------------------------------------------
void sendFooter(WiFiClient &client)
{
  client.write("</body></html>\n");
  delay(3);
  client.stop();
}

// ----------------------------------------------------------------
//                        reset the camera
// ----------------------------------------------------------------
// either hardware(1) or software(0)
void resetCamera(bool type = 0)
{
  if (serialDebug)
    Serial.println("Resetting camera");
  if (type == 1)
  {
    // power cycle the camera
    digitalWrite(PWDN_GPIO_NUM, HIGH); // turn power off to camera module
    delay(500);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(400);
    initialiseCamera();
  }
  else
  {
    // reset via software
    esp_camera_deinit();
    delay(50);
    initialiseCamera();
  }
}

// ----------------------------------------------------------------
//                    -change image resolution
// ----------------------------------------------------------------
// if required resolution not supplied it cycles through several
// note: this stops PWM on the flash working for some reason
void changeResolution(framesize_t tRes = FRAMESIZE_96X96)
{
  esp_camera_deinit(); // disable camera
  delay(50);
  if (tRes == FRAMESIZE_96X96)
  { // taken as none supplied so cycle through several
    if (FRAME_SIZE_IMAGE == FRAMESIZE_QVGA)
      tRes = FRAMESIZE_VGA;
    else if (FRAME_SIZE_IMAGE == FRAMESIZE_VGA)
      tRes = FRAMESIZE_XGA;
    else if (FRAME_SIZE_IMAGE == FRAMESIZE_XGA)
      tRes = FRAMESIZE_UXGA;
    else
      tRes = FRAMESIZE_QVGA;
  }
  FRAME_SIZE_IMAGE = tRes;
  initialiseCamera();
  if (serialDebug)
    Serial.println("Camera resolution changed to " + String(tRes));
  ImageResDetails = "Unknown"; // will be set next time image captured
}

// ----------------------------------------------------------------
// -delete all images on sd card (i.e. all files in '/img' folder)
// ----------------------------------------------------------------
void deleteAllImages()
{
  if (!sdcardPresent)
    return;
  imageCounter = 0;
  fs::FS &fs = SD_MMC; // sd card file system
  if (serialDebug)
    Serial.println("Deleting all images on sd card");
  File root = fs.open("/img");
  uint32_t tTimer = millis();
  while ((millis() - tTimer) < (sdReadTimeout * 1000))
  {
    File entry = root.openNextFile();
    if (!entry)
      break; // stop
    if (entry.isDirectory())
    {
      if (serialDebug)
        Serial.println("directory found");
    }
    else
    {
      // delete file
      if (serialDebug)
        Serial.println("Deleting file " + String(entry.name()));
      fs.remove(String(entry.name()));
    }
  } // while loop
  if ((millis() - tTimer) >= sdReadTimeout)
  {
    if (serialDebug)
      Serial.println("Taking too long to delete all the files on sd card so timed out");
  }
}

// ******************************************************************************************************************

// ----------------------------------------------------------------
//        Capture image from camera and save to sd card
// ----------------------------------------------------------------

bool storeImage()
{

  byte sRes = 0;       // result flag
  fs::FS &fs = SD_MMC; // sd card file system

  // capture the image from camera
  uint8_t currentBrightness = brightLEDbrightness;
  // flash modes: 0=no flash, 1=use flash, 2=use external flash (via gpio pin), 3=use flash and external
  if (flashRequired == 1 || flashRequired == 3)
    brightLed(255); // change LED brightness (0 - 255)
  if (flashRequired == 2 || flashRequired == 3)
  {
    digitalWrite(iopinB, HIGH); // external flash on
    delay(extFlashDelay);       // delay to let light turn on
  }
  camera_fb_t *fb = esp_camera_fb_get(); // capture image frame from camera
  if (flashRequired == 1 || flashRequired == 3)
    brightLed(currentBrightness); // change LED brightness back to previous state
  if (flashRequired == 2 || flashRequired == 3)
    digitalWrite(iopinB, LOW); // external flash off
  if (!fb)
  {
    if (serialDebug)
      Serial.println("Error: Camera capture failed");
    return 0;
  }

  // save the image to sd card
  if (serialDebug)
    Serial.printf("Storing image #%d to sd card \n", imageCounter);
  // create file name
  char buffer[40];
  String tTime = localTime();
  sprintf(buffer, "/img/%06d-%s.jpg", imageCounter, tTime.c_str());
  File file = fs.open(String(buffer), FILE_WRITE); // create file on sd card
  if (!file)
  {
    if (serialDebug)
      Serial.println("Error: Failed to create file on sd-card: " + String(imageCounter));
  }
  else
  {
    if (file.write(fb->buf, fb->len))
    { // File created ok so save image to it
      if (serialDebug)
        Serial.println("Image saved to sd card");
      imageCounter++; // increment image counter
      sRes = 1;       // flag as saved ok
    }
    else
    {
      if (serialDebug)
        Serial.println("Error: failed to save image data file on sd card");
    }
    file.close(); // close image file on sd card
  }

  esp_camera_fb_return(fb); // return frame so memory can be released

  // check free space on sd card and stop capture if under 500k
  uint32_t SDfreeSpace = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1024;
  if (SDfreeSpace < 500 && timelapseEnabled)
  {
    if (serialDebug)
      Serial.println("Capture turned off as very low on sd card space");
    timelapseEnabled = 0;
  }

  return sRes;

} // storeImage

// ******************************************************************************************************************

// ----------------------------------------------------------------
//            -Action any user input on root web page
// ----------------------------------------------------------------

void rootUserInput(WiFiClient &client)
{

  // if button1 was pressed (toggle io pin A)
  //        Note:  if using an input box etc. you would read the value with the command:    String Bvalue = server.arg("demobutton1");

  // if button1 was pressed (toggle io pin B)
  if (server.hasArg("button1"))
  {
    if (serialDebug)
      Serial.println("Button 1 pressed");
    digitalWrite(iopinB, !digitalRead(iopinB)); // toggle output pin on/off
  }

  // if button2 was pressed (toggle flash LED)
  if (server.hasArg("button2"))
  {
    if (serialDebug)
      Serial.println("Button 2 pressed");
    if (brightLEDbrightness == 0)
      brightLed(10); // turn led on dim
    else if (brightLEDbrightness == 10)
      brightLed(40); // turn led on medium
    else if (brightLEDbrightness == 40)
      brightLed(255); // turn led on full
    else
      brightLed(0); // turn led off
  }

  // if button3 was pressed (Toggle flash)
  if (server.hasArg("button3"))
  {
    flashRequired++;
    if (flashRequired > 3)
      flashRequired = 0;
    if (serialDebug)
      Serial.println("Flash toggled to: " + String(flashRequired));
  }

  // if button4 was pressed (change resolution)
  if (server.hasArg("button4"))
  {
    if (serialDebug)
      Serial.println("Button 4 pressed");
    changeResolution(); // cycle through some options
  }

  // if button5 was pressed (delete all images)
  if (server.hasArg("button5"))
  {
    deleteAllImages();
  }

  // if button6 was pressed (reset camera)
  if (server.hasArg("button6"))
  {
    resetCamera(0);
  }

  // if buttonE was pressed (enable/disable timelapse recording)
  if (server.hasArg("buttonE"))
  {
    if (timelapseEnabled)
    {
      if (serialDebug)
        Serial.println("Button E pressed, disabling timelapse recording");
      timelapseEnabled = 0;
    }
    else
    {
      if (serialDebug)
        Serial.println("Button E pressed, enabling timelapse recording");
      timelapseEnabled = 1;
    }
  }

  // if timelapse was adjusted - timeBetweenShots (float)
  if (server.hasArg("timelapse"))
  {
    String Tvalue = server.arg("timelapse"); // read value
    if (Tvalue != NULL)
    {
      float val = Tvalue.toFloat();
      if (val > 0.0 && val <= 3600.0 && val != timeBetweenShots)
      {
        if (serialDebug)
          Serial.printf("Exposure changed to %4.1f\n", val);
        timeBetweenShots = val;
      }
    }
  }

  // if brightness was adjusted - cameraImageBrightness
  if (server.hasArg("bright"))
  {
    String Tvalue = server.arg("bright"); // read value
    if (Tvalue != NULL)
    {
      int val = Tvalue.toInt();
      if (val >= -2 && val <= 2 && val != cameraImageBrightness)
      {
        if (serialDebug)
          Serial.printf("Brightness changed to %d\n", val);
        cameraImageBrightness = val;
        cameraImageSettings(); // Apply camera image settings
      }
    }
  }

  // if exposure was adjusted - cameraImageExposure
  if (server.hasArg("exp"))
  {
    String Tvalue = server.arg("exp"); // read value
    if (Tvalue != NULL)
    {
      int val = Tvalue.toInt();
      if (val >= 0 && val <= 1200 && val != cameraImageExposure)
      {
        if (serialDebug)
          Serial.printf("Exposure changed to %d\n", val);
        cameraImageExposure = val;
        cameraImageSettings(); // Apply camera image settings
      }
    }
  }

  // if image gain was adjusted - cameraImageGain
  if (server.hasArg("gain"))
  {
    String Tvalue = server.arg("gain"); // read value
    if (Tvalue != NULL)
    {
      int val = Tvalue.toInt();
      if (val >= 0 && val <= 31 && val != cameraImageGain)
      {
        if (serialDebug)
          Serial.printf("Gain changed to %d\n", val);
        cameraImageGain = val;
        cameraImageSettings(); // Apply camera image settings
      }
    }
  }
}

// ----------------------------------------------------------------
//       -root web page requested    i.e. http://x.x.x.x/
// ----------------------------------------------------------------
// web page with control buttons, links etc.

void handleRoot()
{

  getNTPtime(2);                       // refresh current time from NTP server
  WiFiClient client = server.client(); // open link with client

  rootUserInput(client); // Action any user input from this web page

  // html header
  sendHeader(client, "ESP32Cam timelapse recorder");
  client.write("<FORM action='/' method='post'>\n"); // used by the buttons in the html (action = the web page to send it to

  // --------------------------------------------------------------------

  // html main body

  // ---------------------------------------------------------------------------------------------
  //  info which is periodically updated usin AJAX - https://www.w3schools.com/xml/ajax_intro.asp

  // sd card
  if (!sdcardPresent)
  {
    client.println("<p style='color:red;'>ERROR: NO SD CARD DETECTED</p>");
  }
  else
  {
    client.println("SD Card: <span id=uImages> - </span> images stored, <span id=uUsed> - </span>MB used , <span id=uRemain> - </span>MB remaining\n");
  }

  // image resolution
  client.println("<br>Image size: <span id=uRes> - </span>");

  // illumination/flash led
  client.println("<br>Illumination led brightness: <span id=uBrightness> - </span>, flash: <span id=uFlash> - </span>");

  // Current real time
  client.println("<br>Current time: <span id=uTime> - </span>");

  // gpio pin status
  client.print("<br>GPIO output pin 12 is: <span id=uGPIO> - </span>");

  // Javascript - to periodically update above getting info from http://x.x.x.x/data
  client.printf(R"=====(
           <script>
              function getData() {
                var xhttp = new XMLHttpRequest();
                xhttp.onreadystatechange = function() {
                if (this.readyState == 4 && this.status == 200) {
                  var receivedArr = this.responseText.split(',');
                  document.getElementById('uImages').innerHTML = receivedArr[0];
                  document.getElementById('uUsed').innerHTML = receivedArr[1];
                  document.getElementById('uRemain').innerHTML = receivedArr[2];
                  document.getElementById('uRes').innerHTML = receivedArr[3];
                  document.getElementById('uBrightness').innerHTML = receivedArr[4];
                  document.getElementById('uFlash').innerHTML = receivedArr[5];
                  document.getElementById('uTime').innerHTML = receivedArr[6];
                  document.getElementById('uGPIO').innerHTML = receivedArr[7];
                }
              };
              xhttp.open('GET', 'data', true);
              xhttp.send();}
              getData();
              setInterval(function() { getData(); }, %d);
           </script>
        )=====",
                datarefresh);

  // ---------------------------------------------------------------------------------------------

  // Image setting controls
  client.println("<br><br>CAMERA SETTINGS: ");
  client.printf("Brightness: <input type='number' style='width: 50px' name='bright' title='from -2 to +2' min='-2' max='2' value='%d'>  \n", cameraImageBrightness);
  client.printf("Exposure: <input type='number' style='width: 50px' name='exp' title='from 0 to 1200' min='0' max='1200' value='%d'>  \n", cameraImageExposure);
  client.printf("Gain: <input type='number' style='width: 50px' name='gain' title='from 0 to 30' min='0' max='30' value='%d'>\n", cameraImageGain);
  client.println(" - Set exposure and gain to zero for auto adjust");

  // Timelapse info
  if (timelapseEnabled)
  {
    client.print("<br><br>Images are being captured every");
  }
  else
  {
    client.print("<br><br>Images will be captured every");
  }
  client.printf(" <input type='number' step='0.1' style='width: 50px' name='timelapse' min='0.1' max='3600.0' value='%.1f'> seconds \n", timeBetweenShots);

  // submit button
  client.println("<br><br><input type='submit' name='submit' value='Submit changes'>");

  // Timelapse button
  client.print(" <input name='buttonE' type='submit' style='height: 50px; width: 160px; ");
  if (timelapseEnabled)
  {
    client.print("color:red;' value='Stop recording'>");
  }
  else
  {
    client.print("' value='Start recording'>");
  }

  // Misc bottons
  client.println("<br><br>");
  client.println("<input name='button1' value='Toggle Output Pin' type='submit'>");
  client.println("<input name='button2' value='Toggle Light' type='submit'>");
  client.println("<input name='button3' value='Toggle flash' type='submit'>");
  client.println("<input name='button4' value='Change Resolution' type='submit'>");
  client.println("<input name='button5' value='Delete all images' type='submit'>");
  client.println("<input name='button6' value='Reset the camera' type='submit'>");

  // links to the other pages available
  client.println("<br><br>");
  client.println("<a href='/photo'>CAPTURE AN IMAGE</a> ");
  client.println("<a href='/stream'>STREAM VIDEO</a> ");
  client.println("<a href='/jpg'>VIEW SINGLE IMAGE</a> ");
  client.println("<a href='/test'>TESTING PAGE</a>");

  // capture and show a jpg image
  client.println("<br><br><img id='image1' src='/jpg' width='320' height='240'/>"); // show image from http://x.x.x.x/jpg

  // javascript to refresh the image periodically
  client.printf(R"=====(
         <script>
           function refreshImage(){
               var timestamp = new Date().getTime();
               var el = document.getElementById('image1');
               var queryString = '?t=' + timestamp;
               el.src = '/jpg' + queryString;
           }
           setInterval(function() { refreshImage(); }, %d);
         </script>
      )=====",
                imagerefresh);

  // sketch info
  client.println("<br><a href='https://github.com/alanesq/esp32cam-Timelapse'>Sketch Source</a>");
  client.println("<br>Create video from jpgs with: ffmpeg -framerate 6 -pattern_type glob -i '*.jpg' -c:v libx264 -profile:v high -crf 20 -pix_fmt yuv420p timelapse.mp4");

  // --------------------------------------------------------------------

  sendFooter(client); // close web page
} // handleRoot

// ----------------------------------------------------------------
//     -data web page requested     i.e. http://x.x.x.x/data
// ----------------------------------------------------------------
// suplies changing info to update root web page as comma seperated String

void handleData()
{

  // sd sdcard
  uint32_t SDusedSpace = 0;
  uint32_t SDtotalSpace = 0;
  uint32_t SDfreeSpace = 0;
  if (sdcardPresent)
  {
    SDusedSpace = SD_MMC.usedBytes() / (1024 * 1024);
    SDtotalSpace = SD_MMC.totalBytes() / (1024 * 1024);
    SDfreeSpace = SDtotalSpace - SDusedSpace;
  }

  String reply = "";
  reply += String(imageCounter); // images stored
  reply += ",";
  if (sdcardPresent)
    reply += String(SDusedSpace); // space used on sd card
  reply += ",";
  if (sdcardPresent)
    reply += String(SDfreeSpace); // space remaining on sd card
  reply += ",";
  reply += ImageResDetails; // image resolution
  reply += ",";
  reply += String(brightLEDbrightness); // illumination led brightness
  reply += ",";
  // flash status
  String tFlash = "off";
  if (flashRequired == 1)
    tFlash = "Onboard LED enabled";
  if (flashRequired == 2)
    tFlash = "External enabled";
  if (flashRequired == 3)
    tFlash = "Onboard & External enabled";
  reply += tFlash;
  reply += ",";
  reply += localTime(); // current date/time
  reply += ",";
  reply += (digitalRead(iopinB) == 1) ? "ON" : "OFF"; // output gpio pin status
                                                      // reply += ",";

  server.send(200, "text/plane", reply); // Send millis value only to client ajax request
}

// ******************************************************************************************************************

// ----------------------------------------------------------------
//    -photo save to sd card/spiffs    i.e. http://x.x.x.x/photo
// ----------------------------------------------------------------
// web page to capture an image from camera and save to spiffs or sd card

void handlePhoto()
{

  WiFiClient client = server.client(); // open link with client

  // log page request including clients IP
  IPAddress cIP = client.remoteIP();
  if (serialDebug)
    Serial.println("Save photo requested by " + cIP.toString());

  byte sRes = storeImage(); // capture and save an image to sd card or spiffs (store sucess or failed flag - 0=fail, 1=spiffs only, 2=spiffs and sd card)

  // html header
  sendHeader(client, "Capture and save image");

  // html body
  if (sRes == 1)
  {
    client.printf("<p>Image saved to sd card as image number %d </p>\n", imageCounter - 1);
  }
  else
  {
    client.write("<p>Error: Failed to save image to sd card</p>\n");
  }

  client.write("<a href='/'>Return</a>\n"); // link back

  // close web page
  sendFooter(client);

} // handlePhoto

// ----------------------------------------------------------------
//                      -invalid web page requested
// ----------------------------------------------------------------
// Note: shows a different way to send the HTML reply

void handleNotFound()
{

  String tReply;

  if (serialDebug)
    Serial.print("Invalid page requested");

  tReply = "File Not Found\n\n";
  tReply += "URI: ";
  tReply += server.uri();
  tReply += "\nMethod: ";
  tReply += (server.method() == HTTP_GET) ? "GET" : "POST";
  tReply += "\nArguments: ";
  tReply += server.args();
  tReply += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    tReply += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", tReply);
  tReply = ""; // clear variable

} // handleNotFound

// ----------------------------------------------------------------
//                      -get time from ntp server
// ----------------------------------------------------------------

bool getNTPtime(int sec)
{

  uint32_t start = millis(); // timeout timer

  do
  {
    time(&now);
    localtime_r(&now, &timeinfo);
    if (serialDebug)
      Serial.print(".");
    delay(100);
  } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));

  if (timeinfo.tm_year <= (2016 - 1900))
    return false; // the NTP call was not successful
  if (serialDebug)
  {
    Serial.print("now ");
    Serial.println(now);
  }

  // Display time
  if (serialDebug)
  {
    char time_output[30];
    strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
    Serial.println(time_output);
    Serial.println();
  }
  return true;
}

// ----------------------------------------------------------------
//     -capture jpg image and send    i.e. http://x.x.x.x/jpg
// ----------------------------------------------------------------

bool handleJPG()
{

  WiFiClient client = server.client(); // open link with client
  char buf[32];

  // capture the jpg image from camera
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb)
  {
    if (serialDebug)
      Serial.println("Error: failed to capture image");
    return 0;
  }

  // store image resolution info.
  ImageResDetails = String(fb->width) + "x" + String(fb->height);

  // html to send a jpg
  const char HEADER[] = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n";
  const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
  const int hdrLen = strlen(HEADER);
  const int cntLen = strlen(CTNTTYPE);
  client.write(HEADER, hdrLen);
  client.write(CTNTTYPE, cntLen);
  sprintf(buf, "%d\r\n\r\n", fb->len); // put text size in to 'buf' char array and send
  client.write(buf, strlen(buf));

  // send the captured jpg data
  client.write((char *)fb->buf, fb->len);

  // close client connection
  delay(3);
  client.stop();

  // return image frame so memory can be released
  esp_camera_fb_return(fb);

  return 1;

} // handleJPG

// ----------------------------------------------------------------
//      -stream requested     i.e. http://x.x.x.x/stream
// ----------------------------------------------------------------
// Sends video stream - thanks to Uwe Gerlach for the code showing me how to do this

void handleStream()
{

  WiFiClient client = server.client(); // open link with client
  char buf[32];
  camera_fb_t *fb = NULL;

  // log page request including clients IP
  IPAddress cIP = client.remoteIP();
  if (serialDebug)
    Serial.println("Live stream requested by " + cIP.toString());

  // html
  const char HEADER[] = "HTTP/1.1 200 OK\r\n"
                        "Access-Control-Allow-Origin: *\r\n"
                        "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
  const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";     // marks end of each image frame
  const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: "; // marks start of image data
  const int hdrLen = strlen(HEADER);                                      // length of the stored text, used when sending to web page
  const int bdrLen = strlen(BOUNDARY);
  const int cntLen = strlen(CTNTTYPE);
  client.write(HEADER, hdrLen);
  client.write(BOUNDARY, bdrLen);

  // send live jpg images until client disconnects
  while (true)
  {
    if (!client.connected())
      break;
    fb = esp_camera_fb_get(); // capture live image as jpg
    if (!fb)
    {
      if (serialDebug)
        Serial.println("Error: failed to capture jpg image");
    }
    else
    {
      // send image
      client.write(CTNTTYPE, cntLen);         // send content type html (i.e. jpg image)
      sprintf(buf, "%d\r\n\r\n", fb->len);    // format the image's size as html and put in to 'buf'
      client.write(buf, strlen(buf));         // send result (image size)
      client.write((char *)fb->buf, fb->len); // send the image data
      client.write(BOUNDARY, bdrLen);         // send html boundary      see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type
      esp_camera_fb_return(fb);               // return image buffer so memory can be released
    }
  }

  if (serialDebug)
    Serial.println("Video stream stopped");
  delay(3);
  client.stop();

} // handleStream

// ----------------------------------------------------------------
//       -test procedure    i.e. http://x.x.x.x/test
// ----------------------------------------------------------------

void handleTest()
{

  WiFiClient client = server.client(); // open link with client

  // log page request including clients IP
  IPAddress cIP = client.remoteIP();
  if (serialDebug)
    Serial.println("Test page requested by " + cIP.toString());

  // html header
  sendHeader(client, "Testing Page");

  // html body
  // -------------------------------------------------------------------

  // test html code goes here

  // -------------------------------------------------------------------

  client.println("<br><br><a href='/'>Return</a>"); // link back
  sendFooter(client);                               // close web page

} // handleTest

// ******************************************************************************************************************
// end