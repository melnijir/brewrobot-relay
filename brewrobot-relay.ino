#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Wifi settings
const char* ssid                   = "";              // REQUIRED - WiFi SSID
const char* password               = "";              // REQUIRED - WiFi password
const unsigned long reconnect_time = 60000;           // Try to reconnect every 60s

// MQTT broker settings
const char *mqtt_broker            = "";              // REQUIRED - MQTT broker IP address
const char *mqtt_username          = "";              // REQUIRED - MQTT broker user
const char *mqtt_password          = "";              // REQUIRED - MQTT broker password
const char *mqtt_device            = "BrewrobotRelay";
const char *mqtt_base_addr         = "homeassistant/switch/";
const int mqtt_port                = 1883;

// Sync settings
const int relay_gpio               = 0;               // Relay is connected to GPIO0
const unsigned long relay_sync     = 5000;            // Send relay state every 5s

// Loop variables
unsigned long wifi_last_conn       = 0;               // Value in miliseconds when was the last connection time for WiFi
unsigned long mqtt_last_conn       = 0;               // Value in miliseconds when was the last connection time for MQTT
unsigned long relay_last_conn      = 0;               // Value in miliseconds when was the last connection time for relay state
int curr_relay_state               = LOW;

/*
 * WiFi and MQTT
 */
WiFiClient esp_client;
PubSubClient pubsub_client(esp_client);

String get_device_id() {
  String chip_ip = String(WiFi.macAddress().substring(9, 11)+
                          WiFi.macAddress().substring(12,14)+
                          WiFi.macAddress().substring(15,17));
  return (String(mqtt_device)+chip_ip);
}

bool wifi_connect() {
  WiFi.begin(ssid, password);
  int connect_timeout = 50;
  while (WiFi.status() != WL_CONNECTED && connect_timeout-- > 0) {
      delay(100);
  }
  if (connect_timeout > 0) {
    return true;
  }
  return false;
}

bool wifi_connected() {
  return (WiFi.status() == WL_CONNECTED);
}

bool wifi_reconnect() {
    WiFi.disconnect();
    return WiFi.reconnect();
}

bool mqtt_connected() {
  return pubsub_client.connected();
}

bool mqtt_reconnect() {
  if (pubsub_client.connect(get_device_id().c_str(), mqtt_username, mqtt_password)) {
    return true;
  }
  return false;
}

bool mqtt_put_message(String topic, String payload) {
      pubsub_client.beginPublish(topic.c_str(), payload.length(), true);
      for (size_t pos = 0; pos <= payload.length(); pos+=64) {
          pubsub_client.print(payload.substring(pos,pos+64).c_str());
      }
      return pubsub_client.endPublish();
}

bool send_device_registration() {
  if (!mqtt_connected())
    return false;
  String registration_json = R"({
   "name":"XXX",
   "state_topic":"homeassistant/switch/XXX/state",
   "command_topic":"homeassistant/switch/XXX/set",
   "optimistic":true,
   "unique_id":"XXX",
   "device":{
      "identifiers":[
          "XXX"
      ],
      "name":"Switch",
      "manufacturer":"Brewrobot",
      "model":"Switch for ESP8266",
      "model_id":"A1",
      "serial_number":"00001",
      "hw_version":"1.00",
      "sw_version":"2025.0.0",
      "configuration_url": "https://github.com/melnijir/brewrobot-relay"
    }
  })";
  registration_json.replace("XXX",get_device_id());
  return mqtt_put_message(String(mqtt_base_addr+get_device_id()+"/config"), registration_json);
}

bool send_device_state(bool value) {
  if (!mqtt_connected())
    return false;
  return mqtt_put_message(String(mqtt_base_addr+get_device_id()+"/state"), value ? "ON" : "OFF");
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if ((char)payload[1] == 'N' || (char)payload[1] == 'n') {
    curr_relay_state = LOW;
  } else {
    curr_relay_state = HIGH;
  }
  digitalWrite(relay_gpio,curr_relay_state);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Serial connection ready.");

  // Set default relay mode
  pinMode(relay_gpio,OUTPUT);
  digitalWrite(relay_gpio, LOW);

  // Setup WiFi connection
  if (wifi_connect()) {
    Serial.println("WiFi connected.");
  } else {
    Serial.println("Could not connect to WiFi.");
  }

  // Setup MQTT connection
  pubsub_client.setServer(mqtt_broker, mqtt_port);
  pubsub_client.setCallback(mqtt_callback);
}

void loop() {
  // Get current timing for the jobs
  unsigned long current_time = millis();

  // WiFi connection checks
  // ----------------------
  if (!wifi_connected()) {
    Serial.println("WiFi disconnected, will try to reconnect.");
    if (current_time - wifi_last_conn >= reconnect_time) {
      Serial.println("WiFi reconnecting...");
      wifi_reconnect();
      wifi_last_conn = current_time;
    }
  } else {
    Serial.println("WiFi connected.");
  }

  // MQTT connection checks
  // ----------------------
  if (!mqtt_connected()) {
    Serial.println("MQTT disconnected.");
    if (current_time - mqtt_last_conn >= reconnect_time) {
      Serial.println("MQTT reconnecting...");
      mqtt_reconnect();
      send_device_registration();
      pubsub_client.subscribe(String(mqtt_base_addr+get_device_id()+"/set").c_str());
      mqtt_last_conn = current_time;
    }
  } else {
    Serial.println("MQTT connected.");
  }

  // Sending current state
  // ----------------------
  if (current_time - relay_last_conn >= relay_sync) {
    send_device_state(curr_relay_state == LOW);
    relay_last_conn = current_time;
  }
  
   // Run MQTT rutines
  pubsub_client.loop();
  // Sleep a while before the next run
  delay(200);
}
