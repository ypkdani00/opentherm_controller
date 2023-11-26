#include <Blinker.h>

extern Variables vars;
extern Settings settings;

extern MqttTask* tMqtt;
extern SensorsTask* tSensors;
extern OpenThermTask* tOt;
extern EEManager eeSettings;
extern TinyLogger Log;
#if USE_TELNET
  extern ESPTelnetStream TelnetStream;
#endif


class MainTask : public Task {
public:
  MainTask(bool _enabled = false, unsigned long _interval = 0) : Task(_enabled, _interval) {}

protected:
  Blinker* blinker = nullptr;
  unsigned long lastHeapInfo = 0;
  unsigned long firstFailConnect = 0;
  unsigned int heapSize = 0;
  unsigned int minFreeHeapSize = 0;
  unsigned long restartSignalTime = 0;

  const char* getTaskName() {
    return "Main";
  }

  int getTaskCore() {
    return 1;
  }

  void setup() {
    #ifdef LED_STATUS_PIN
      pinMode(LED_STATUS_PIN, OUTPUT);
      digitalWrite(LED_STATUS_PIN, false);
    #endif

    #if defined(ESP32)
      heapSize = ESP.getHeapSize();
    #elif defined(ESP8266)
      heapSize = 81920;
    #elif
      heapSize = 99999;
    #endif
    minFreeHeapSize = heapSize;
  }

  void loop() {
    if (eeSettings.tick()) {
      Log.sinfoln("MAIN", PSTR("Settings updated (EEPROM)"));
    }

    #if USE_TELNET
      TelnetStream.loop();
    #endif

    if (vars.actions.restart) {
      Log.sinfoln("MAIN", PSTR("Restart signal received. Restart after 10 sec."));
      eeSettings.updateNow();
      restartSignalTime = millis();
      vars.actions.restart = false;
    }

    if (WiFi.status() == WL_CONNECTED) {
      //timeClient.update();
      vars.sensors.rssi = WiFi.RSSI();

      if (!tMqtt->isEnabled() && strlen(settings.mqtt.server) > 0) {
        tMqtt->enable();
      }

      if (firstFailConnect != 0) {
        firstFailConnect = 0;
      }

    } else {
      if (tMqtt->isEnabled()) {
        tMqtt->disable();
      }

      if (settings.emergency.enable && !vars.states.emergency) {
        if (firstFailConnect == 0) {
          firstFailConnect = millis();
        }

        if (millis() - firstFailConnect > EMERGENCY_TIME_TRESHOLD) {
          vars.states.emergency = true;
          Log.sinfoln("MAIN", PSTR("Emergency mode enabled"));
        }
      }
    }

    if (!tOt->isEnabled() && settings.opentherm.inPin > 0 && settings.opentherm.outPin > 0 && settings.opentherm.inPin != settings.opentherm.outPin) {
      tOt->enable();
    }

    #ifdef LED_STATUS_PIN
      ledStatus(LED_STATUS_PIN);
      yield();
    #endif
    heap();

    // anti memory leak
    for (Stream* stream : Log.getStreams()) {
      stream->flush();

      while (stream->available()) {
        stream->read();
        yield();
      }
    }

    if (restartSignalTime > 0 && millis() - restartSignalTime > 10000) {
      restartSignalTime = 0;
      ESP.restart();
    }
  }

  void heap() {
    if (!settings.debug) {
      return;
    }

    unsigned int freeHeapSize = ESP.getFreeHeap();
    unsigned int minFreeHeapSizeDiff = 0;

    if (freeHeapSize < minFreeHeapSize) {
      minFreeHeapSizeDiff = minFreeHeapSize - freeHeapSize;
      minFreeHeapSize = freeHeapSize;
    }

    if (millis() - lastHeapInfo > 60000 || minFreeHeapSizeDiff > 0) {
      Log.sverboseln("MAIN", PSTR("Free heap size: %u of %u bytes, min: %u bytes (diff: %u bytes)"), freeHeapSize, heapSize, minFreeHeapSize, minFreeHeapSizeDiff);
      lastHeapInfo = millis();
    }
  }

  void ledStatus(uint8_t ledPin) {
    uint8_t errors[4];
    uint8_t errCount = 0;
    static uint8_t errPos = 0;
    static unsigned long endBlinkTime = 0;
    static bool ledOn = false;

    if (this->blinker == nullptr) {
      this->blinker = new Blinker(ledPin);
    }

    if (WiFi.status() != WL_CONNECTED) {
      errors[errCount++] = 2;
    }

    if (!vars.states.otStatus) {
      errors[errCount++] = 3;
    }

    if (vars.states.fault) {
      errors[errCount++] = 4;
    }

    if (vars.states.emergency) {
      errors[errCount++] = 5;
    }


    if (this->blinker->ready()) {
      endBlinkTime = millis();
    }

    if (!this->blinker->running() && millis() - endBlinkTime >= 5000) {
      if (errCount == 0) {
        if (!ledOn) {
          digitalWrite(ledPin, true);
          ledOn = true;
        }

        return;

      } else if (ledOn) {
        digitalWrite(ledPin, false);
        ledOn = false;
        endBlinkTime = millis();
        return;
      }

      if (errPos >= errCount) {
        errPos = 0;

        // end of error list
        this->blinker->blink(10, 50, 50);

      } else {
        this->blinker->blink(errors[errPos++], 300, 300);
      }
    }

    this->blinker->tick();
  }
};