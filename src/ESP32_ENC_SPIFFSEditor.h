/****************************************************************************************************************************
  ESP32_ENC_SPIFFSEditor.h - Dead simple Ethernet AsyncWebServer.

  For ENC28J60 Ethernet in ESP32 (ESP32 + ENC28J60)

  AsyncWebServer_ESP32_ENC is a library for the Ethernet ENC28J60 in ESSP32 to run AsyncWebServer

  Based on and modified from ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
  Built by Khoi Hoang https://github.com/khoih-prog/AsyncWebServer_ESP32_ENC
  Licensed under GPLv3 license

  Original author: Hristo Gochkov

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.

  This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License along with this library;
  if not, write to the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Version: 1.6.3

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.6.2   K Hoang      27/11/2022 Initial porting for ENC28J60 + ESP32. Sync with AsyncWebServer_WT32_ETH01 v1.6.2
  1.6.3   K Hoang      05/12/2022 Add Async_WebSocketsServer, MQTT examples
 *****************************************************************************************************************************/

#ifndef ESP32_ENC_SPIFFSEditor_H_
#define ESP32_ENC_SPIFFSEditor_H_

#include "AsyncWebServer_ESP32_ENC.h"

/////////////////////////////////////////////////

class SPIFFSEditor: public AsyncWebHandler
{
  private:
    fs::FS _fs;
    String _username;
    String _password;
    bool _authenticated;
    uint32_t _startTime;

  public:
    SPIFFSEditor(const fs::FS& fs, const String& username = String(), const String& password = String());
    virtual bool canHandle(AsyncWebServerRequest *request) override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                              size_t len, bool final) override final;

    virtual bool isRequestHandlerTrivial() override final
    {
      return false;
    }
};

/////////////////////////////////////////////////

#endif    // SPIFFSEditor_H_
