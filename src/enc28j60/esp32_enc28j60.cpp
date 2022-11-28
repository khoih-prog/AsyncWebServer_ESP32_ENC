/****************************************************************************************************************************
  esp32_enc28j60.cpp

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

  Version: 1.6.2

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.6.2   K Hoang      27/11/2022 Initial porting for ENC28J60 + ESP32. Sync with AsyncWebServer_WT32_ETH01 v1.6.2
 *****************************************************************************************************************************/
/*
  Based on ETH.h from arduino-esp32 and esp-idf
  and Tobozo ESP32-ENC28J60 library
*/

#include "AsyncWebServer_ESP32_ENC_Debug.h"
#include "esp32_enc28j60.h"

extern "C"
{
  esp_eth_mac_t* enc28j60_begin(int MISO_GPIO, int MOSI_GPIO, int SCLK_GPIO, int CS_GPIO, int INT_GPIO, int SPI_CLOCK_MHZ,
                                int SPI_HOST);
#include "extmod/esp_eth_enc28j60.h"
}

#include "esp_event.h"
#include "esp_eth_phy.h"
#include "esp_eth_mac.h"
#include "esp_eth_com.h"

#if CONFIG_IDF_TARGET_ESP32
  #include "soc/emac_ext_struct.h"
  #include "soc/rtc.h"
#endif

#include "lwip/err.h"
#include "lwip/dns.h"

extern void tcpipInit();

////////////////////////////////////////

ESP32_ENC::ESP32_ENC()
  : initialized(false)
  , staticIP(false)
  , eth_handle(NULL)
  , started(false)
  , eth_link(ETH_LINK_DOWN)
{
}

////////////////////////////////////////

ESP32_ENC::~ESP32_ENC()
{}

////////////////////////////////////////

bool ESP32_ENC::begin(int MISO_GPIO, int MOSI_GPIO, int SCLK_GPIO, int CS_GPIO, int INT_GPIO, int SPI_CLOCK_MHZ,
                      int SPI_HOST, uint8_t *ENC28J60_Mac)
{
  tcpipInit();

  esp_base_mac_addr_set( ENC28J60_Mac );

  tcpip_adapter_set_default_eth_handlers();

  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&cfg);

  esp_eth_mac_t *eth_mac = enc28j60_begin(MISO_GPIO, MOSI_GPIO, SCLK_GPIO, CS_GPIO, INT_GPIO, SPI_CLOCK_MHZ, SPI_HOST);

  if (eth_mac == NULL)
  {
    AWS_LOGERROR("esp_eth_mac_new_esp32 failed");

    return false;
  }

  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.autonego_timeout_ms = 0;       // ENC28J60 doesn't support auto-negotiation
  phy_config.reset_gpio_num = -1;           // ENC28J60 doesn't have a pin to reset internal PHY
  esp_eth_phy_t *eth_phy = esp_eth_phy_new_enc28j60(&phy_config);

  if (eth_phy == NULL)
  {
    AWS_LOGERROR("esp_eth_phy_new failed");

    return false;
  }

  eth_handle = NULL;
  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(eth_mac, eth_phy);

  if (esp_eth_driver_install(&eth_config, &eth_handle) != ESP_OK || eth_handle == NULL)
  {
    AWS_LOG("esp_eth_driver_install failed");

    return false;
  }

  eth_mac->set_addr(eth_mac, ENC28J60_Mac);

  if (emac_enc28j60_get_chip_info(eth_mac) < ENC28J60_REV_B5 && SPI_CLOCK_MHZ < 8)
  {
    AWS_LOGERROR("SPI Clock must be >= 8 MHz for ENC28J60_REV_B5");
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  /* attach Ethernet driver to TCP/IP stack */
  if (esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)) != ESP_OK)
  {
    AWS_LOGERROR("esp_netif_attach failed");

    return false;
  }

  if (esp_eth_start(eth_handle) != ESP_OK)
  {
    AWS_LOG("esp_eth_start failed");

    return false;
  }

  // holds a few microseconds to let DHCP start and enter into a good state
  // FIX ME -- adresses issue https://github.com/espressif/arduino-esp32/issues/5733
  delay(50);

  return true;
}

////////////////////////////////////////

bool ESP32_ENC::config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2)
{
  esp_err_t err = ESP_OK;
  tcpip_adapter_ip_info_t info;

  if (static_cast<uint32_t>(local_ip) != 0)
  {
    info.ip.addr = static_cast<uint32_t>(local_ip);
    info.gw.addr = static_cast<uint32_t>(gateway);
    info.netmask.addr = static_cast<uint32_t>(subnet);
  }
  else
  {
    info.ip.addr = 0;
    info.gw.addr = 0;
    info.netmask.addr = 0;
  }

  err = tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_ETH);

  if (err != ESP_OK && err != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STOPPED)
  {
    AWS_LOGERROR1("DHCP could not be stopped! Error =", err);
    return false;
  }

  err = tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_ETH, &info);

  if (err != ERR_OK)
  {
    AWS_LOGERROR1("STA IP could not be configured! Error = ", err);
    return false;
  }

  if (info.ip.addr)
  {
    staticIP = true;
  }
  else
  {
    err = tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_ETH);

    if (err != ESP_OK && err != ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STARTED)
    {
      AWS_LOGWARN1("DHCP could not be started! Error =", err);
      return false;
    }

    staticIP = false;
  }

  ip_addr_t d;
  d.type = IPADDR_TYPE_V4;

  if (static_cast<uint32_t>(dns1) != 0)
  {
    // Set DNS1-Server
    d.u_addr.ip4.addr = static_cast<uint32_t>(dns1);
    dns_setserver(0, &d);
  }

  if (static_cast<uint32_t>(dns2) != 0)
  {
    // Set DNS2-Server
    d.u_addr.ip4.addr = static_cast<uint32_t>(dns2);
    dns_setserver(1, &d);
  }

  return true;
}

////////////////////////////////////////

IPAddress ESP32_ENC::localIP()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return IPAddress();
  }

  return IPAddress(ip.ip.addr);
}

////////////////////////////////////////

IPAddress ESP32_ENC::subnetMask()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return IPAddress();
  }

  return IPAddress(ip.netmask.addr);
}

////////////////////////////////////////

IPAddress ESP32_ENC::gatewayIP()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return IPAddress();
  }

  return IPAddress(ip.gw.addr);
}

////////////////////////////////////////

IPAddress ESP32_ENC::dnsIP(uint8_t dns_no)
{
  const ip_addr_t * dns_ip = dns_getserver(dns_no);

  return IPAddress(dns_ip->u_addr.ip4.addr);
}

////////////////////////////////////////

IPAddress ESP32_ENC::broadcastIP()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return IPAddress();
  }

  return WiFiGenericClass::calculateBroadcast(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

////////////////////////////////////////

IPAddress ESP32_ENC::networkID()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return IPAddress();
  }

  return WiFiGenericClass::calculateNetworkID(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

////////////////////////////////////////

uint8_t ESP32_ENC::subnetCIDR()
{
  tcpip_adapter_ip_info_t ip;

  if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_ETH, &ip))
  {
    return (uint8_t)0;
  }

  return WiFiGenericClass::calculateSubnetCIDR(IPAddress(ip.netmask.addr));
}

////////////////////////////////////////

const char * ESP32_ENC::getHostname()
{
  const char * hostname;

  if (tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_ETH, &hostname))
  {
    return NULL;
  }

  return hostname;
}

////////////////////////////////////////

bool ESP32_ENC::setHostname(const char * hostname)
{
  return tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_ETH, hostname) == 0;
}

////////////////////////////////////////

bool ESP32_ENC::fullDuplex()
{
#ifdef ESP_IDF_VERSION_MAJOR
  return true;//todo: do not see an API for this
#else
  return eth_config.phy_get_duplex_mode();
#endif
}

////////////////////////////////////////

bool ESP32_ENC::linkUp()
{
#ifdef ESP_IDF_VERSION_MAJOR
  return eth_link == ETH_LINK_UP;
#else
  return eth_config.phy_check_link();
#endif
}

////////////////////////////////////////

uint8_t ESP32_ENC::linkSpeed()
{
#ifdef ESP_IDF_VERSION_MAJOR
  eth_speed_t link_speed;
  esp_eth_ioctl(eth_handle, ETH_CMD_G_SPEED, &link_speed);
  return (link_speed == ETH_SPEED_10M) ? 10 : 100;
#else
  return eth_config.phy_get_speed_mode() ? 100 : 10;
#endif
}

////////////////////////////////////////

bool ESP32_ENC::enableIpV6()
{
  return tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_ETH) == 0;
}

////////////////////////////////////////

IPv6Address ESP32_ENC::localIPv6()
{
  static ip6_addr_t addr;

  if (tcpip_adapter_get_ip6_linklocal(TCPIP_ADAPTER_IF_ETH, &addr))
  {
    return IPv6Address();
  }

  return IPv6Address(addr.addr);
}

////////////////////////////////////////

uint8_t * ESP32_ENC::macAddress(uint8_t* mac)
{
  if (!mac)
  {
    return NULL;
  }

#ifdef ESP_IDF_VERSION_MAJOR
  esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac);
#else
  esp_eth_get_mac(mac);
#endif

  return mac;
}

////////////////////////////////////////

String ESP32_ENC::macAddress()
{
  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  char macStr[18] = { 0 };
  macAddress(mac);
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  return String(macStr);
}

////////////////////////////////////////

ESP32_ENC ETH;
