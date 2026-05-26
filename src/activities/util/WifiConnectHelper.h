#pragma once
#include <WiFi.h>
#include "WifiCredentialStore.h"
#include <Logging.h>

namespace WifiConnectHelper {
  inline bool connectToDefaultWifi() {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }

    WiFi.mode(WIFI_STA);
    
    // Check if there is a last connected SSID in WIFI_STORE
    WIFI_STORE.loadFromFile();
    const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
    if (!lastSsid.empty()) {
      const auto* cred = WIFI_STORE.findCredential(lastSsid);
      if (cred) {
        LOG_DBG("WIFI_HELP", "Auto-connecting to saved SSID: %s", lastSsid.c_str());
        WiFi.persistent(false);
        if (!cred->password.empty()) {
          WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
        } else {
          WiFi.begin(cred->ssid.c_str());
        }
        
        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 25) { // 2.5 seconds timeout
          delay(100);
          retries++;
        }
        if (WiFi.status() == WL_CONNECTED) {
          LOG_DBG("WIFI_HELP", "Successfully connected to %s", lastSsid.c_str());
          return true;
        }
      }
    }
    
    // Otherwise try standard begin (fallback to NVS/SDK credentials)
    LOG_DBG("WIFI_HELP", "Fallback begin connecting...");
    WiFi.begin();
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15) { // 1.5 seconds timeout
      delay(100);
      retries++;
    }
    return WiFi.status() == WL_CONNECTED;
  }
}
