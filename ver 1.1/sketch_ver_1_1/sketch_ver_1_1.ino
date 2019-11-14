// M.Dudek mr.m.dudek@gmail.com
// ver. 1.1 
// 08.11.2019

// un-comment "#define DEBUG" in DebugMacros.h to print the debugging statements 

// include
#include <ESP8266WiFi.h>
#include "HTTPSRedirect.h"
#include "DebugMacros.h"
#include <Ticker.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// define
#define DHTTYPE DHT22
#define DHTPIN  2
const int oneWireBus = 0;  

// Wifi and google script parameters
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* host = "script.google.com";
const char *GScriptId = "YOUR_SCRIPT_URL";
const int httpsPort = 443;
const char* fingerprint = "";        

// set time between measurements in seconds. Range 4 - 255 seconds.
uint8_t sec_delay = 60;
sec_delay = sec_delay-4; // DHT read + DS18b20 read + send data =~ 4s

// Prepare default url
String url = String("/macros/s/") + GScriptId + "/exec?";
String payload = "";
HTTPSRedirect* client = nullptr;
 
// DHT define
DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266
float humidity, temp_c; // Values read from sensor
unsigned long previousMillis = 0; // will store last temp was read
const long interval = 2000; // interval at which to read sensor

// DS18b20 define
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);
uint8_t numberOfDevices;// Number of temperature devices found
DeviceAddress tempDeviceAddress;// We'll use this variable to store a found device address
float DsTempC;// variable contain temperature data when single device on line

// error detect and data for reset
static int error_count = 0;  // how many count
volatile int WatchDogCount = 0; // watchdog count
Ticker Raj_Tick; // Ticker watchdog
void(* resetFunc) (void) = 0;//declare reset function at address 0

// ISR watchdog
void ISRWatchDog(){
   WatchDogCount++;
   if (WatchDogCount == (10+sec_delay)) // if main program didn't reset WatchDogCount reset
   {  
     Serial.print("Wathdog RESET ");
     resetFunc();
   }
}

// setup function
void setup() 
{

    // init serial
    Serial.begin(115200);
    Serial.flush();
    Serial.println();

    // init wifi and connect
    connect_wifi();
    connect_host_first();
    Raj_Tick.attach(1,ISRWatchDog); // 1st argument is in seconds after which the ISRwatchdog() executes
    sensors.begin();
    DS_look_for_devices();// search devices on one wire line
}

// main loop
void loop() 
{
    connect_host();

    read_sensors_and_send_data();

    if (error_count > 2){ 
      resetFunc(); //call reset
    }

    WatchDogCount = 0;
    delay(sec_delay*1000);
}

// function connect wifi
void connect_wifi(void)
{
	// wifi connect
	Serial.print("Connecting to wifi: ");
	Serial.println(ssid);
	Serial.flush();
	WiFi.begin(ssid, password);
	while (WiFi.status() != WL_CONNECTED) 
	{
	delay(500);
	Serial.print(".");
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	error_count = 0;
}

// function connect_host_first
void connect_host_first(void)
{
	// Connect to host
	client = new HTTPSRedirect(httpsPort);
	client->setInsecure();
	client->setPrintResponseBody(true);
	client->setContentTypeHeader("application/json");
	Serial.print("Connecting to ");
	Serial.println(host);

	// Try to connect for a maximum of 5 times
	bool flag = false;
	for (int i=0; i<5; i++)
	{
		int retval = client->connect(host, httpsPort);
		if (retval == 1) {
			flag = true;
			error_count = 0;
			break;
		}
		else
		{
			Serial.println("Connection failed. Retrying...");
		}
	}

	if (!flag){
		Serial.print("Could not connect to server RESET ");
		resetFunc(); //call reset 
	}

	// delete HTTPSRedirect object
	delete client;
	client = nullptr;
}

// function connect_host
void connect_host(void)
{
	static bool flag = false;

	if (!flag){
	client = new HTTPSRedirect(httpsPort);
	client->setInsecure();
	flag = true;
	client->setPrintResponseBody(true);
	client->setContentTypeHeader("application/json");
	}

	if (client != nullptr)
	{
		if (!client->connected())
		{
			client->connect(host, httpsPort);
			Serial.println("Connected do GScript");
		}
	}
	else
	{
		DPRINTLN("Error creating client object! RESET");// Debug print 
		resetFunc(); //call reset 
	}
}

// get temperature and humidity, if values are ok send to google sheet
void read_sensors_and_send_data(void)
{  
	if(!gettemperature())
	{
		temp_c = -1.1;
		humidity = -1.1;
	}

	DsTempC = DS_read_temperature_from_single_device();

	payload = url + "tDHT=" + temp_c + "&hDHT=" + humidity +"&tDS=" + DsTempC;
	if(client->GET(payload, host))
	{
	  
	}
	else{
		++error_count;
		DPRINT("Error-count while connecting: ");
		DPRINTLN(error_count);
	}
}

// function gettemperature
bool gettemperature() 
{
  // Wait at least 2 seconds seconds between measurements.
  // if the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor
  // Works better than delay for things happening elsewhere also
  unsigned long currentMillis = millis();

  if(currentMillis - previousMillis >= interval) 
  {
    // save the last time you read the sensor 
    previousMillis = currentMillis;   

    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    humidity = dht.readHumidity();          // Read humidity (percent)
    temp_c = (dht.readTemperature(true)-32)/1.8;     // Read temperature in celcius
    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp_c)) 
    {
      Serial.println("Failed to read from DHT sensor!");
      return false;
    }
  }

  return true;
}

void DS_look_for_devices(void)
{
	// Grab a count of devices on the wire
	numberOfDevices = sensors.getDeviceCount();

	// locate devices on the bus
	Serial.print("Locating devices...");
	Serial.print("Found ");
	Serial.print(numberOfDevices, DEC);
	Serial.println(" devices.");
}

 float DS_read_temperature_from_single_device(void)
{
	sensors.requestTemperatures();
	delay(750);        
	if(sensors.getAddress(tempDeviceAddress, 0))// Search the wire for address
	{
		return sensors.getTempC(tempDeviceAddress);
	}
	else return -1.1;
}
