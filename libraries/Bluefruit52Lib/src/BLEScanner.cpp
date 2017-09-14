/**************************************************************************/
/*!
    @file     BLEScanner.cpp
    @author   hathach

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2017, Adafruit Industries (adafruit.com)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/

#include "bluefruit.h"

BLEScanner::BLEScanner(void)
{
  _runnning            = false;
  _start_if_disconnect = true;

  _filter_rssi         = INT8_MIN;

  _filter_msd_en       = false;
  _filter_msd_id       = 0; // Irrelevant

  _filter_uuid_count   = 0;
  _filter_uuid         = NULL;

  _rx_cb               = NULL;
  _stop_cb             = NULL;

  _param  = (ble_gap_scan_params_t) {
    .active      = 0,
    .selective   = 0,
    .p_whitelist = NULL,
    .interval    = BLE_SCAN_INTERVAL_DFLT,
    .window      = BLE_SCAN_WINDOW_DFLT,
    .timeout     = 0, // no timeout
  };
}

void BLEScanner::useActiveScan(bool enable)
{
  _param.active = enable;
}

void BLEScanner::setInterval(uint16_t interval, uint16_t window)
{
  _param.interval = interval;
  _param.window   = window;
}

void BLEScanner::setIntervalMS(uint16_t interval, uint16_t window)
{
  _param.interval = MS1000TO625(interval);
  _param.window   = MS1000TO625(window);
}

bool BLEScanner::isRunning(void)
{
  return _runnning;
}

void BLEScanner::setRxCallback(rx_callback_t fp)
{
  _rx_cb = fp;
}

void BLEScanner::restartOnDisconnect(bool enable)
{
  _start_if_disconnect = enable;
}

ble_gap_scan_params_t* BLEScanner::getParams(void)
{
  return &_param;
}

bool BLEScanner::start(uint16_t timeout)
{
  _param.timeout = timeout;
  VERIFY_STATUS( sd_ble_gap_scan_start(&_param), false );

  Bluefruit._startConnLed(); // start blinking
  _runnning = true;

  return true;
}

bool BLEScanner::stop(void)
{
  VERIFY_STATUS( sd_ble_gap_scan_stop(), false );

  Bluefruit._stopConnLed(); // stop blinking
  _runnning = false;

  return true;
}

/*------------------------------------------------------------------*/
/* Paser helper
 *------------------------------------------------------------------*/

 /**
  * @param scandata
  * @param scanlen
  * @param type
  * @param buf
  * @param bufsize If bufsize is skipped (zero), len check will be skipped
  * @return number of written bytes
  */
uint8_t BLEScanner::parseReportByType(const uint8_t* scandata, uint8_t scanlen, uint8_t type, uint8_t* buf, uint8_t bufsize)
{
  uint8_t len = 0;
  uint8_t const* ptr = NULL;

  // len (1+data), type, data
  while ( scanlen )
  {
    if ( scandata[1] == type )
    {
      len = (scandata[0]-1);
      ptr = (scandata + 2);
      break;
    }
    else
    {
      scanlen  -= (scandata[0] + 1);
      scandata += (scandata[0] + 1);
    }
  }

  // not found return 0
  if (ptr == NULL) return 0;

  // Only check len if bufsize is input
  if (bufsize > 0) len = min8(bufsize, len);

  memcpy(buf, ptr, len);
  return len;
}

uint8_t BLEScanner::parseReportByType(const ble_gap_evt_adv_report_t* report, uint8_t type, uint8_t* buf, uint8_t bufsize)
{
  return parseReportByType(report->data, report->dlen, type, buf, bufsize);
}

bool BLEScanner::checkReportForUuid(const ble_gap_evt_adv_report_t* report, BLEUuid ble_uuid)
{
  const uint8_t* uuid;
  uint8_t uuid_len = ble_uuid.size();

  uint8_t type_arr[2];

  // Check both more available and complete list
  if ( uuid_len == 16)
  {
    type_arr[0] = BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE;
    type_arr[1] = BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE;

    uuid = (uint8_t*) &ble_uuid._uuid.uuid;
  }else
  {
    type_arr[0] = BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE;
    type_arr[1] = BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE;

    uuid = ble_uuid._uuid128;
  }

  uuid_len /= 8; // convert to number of bytes

  for (int i=0; i<2; i++)
  {
    uint8_t buffer[BLE_GAP_ADV_MAX_SIZE] = { 0 };
    uint8_t len = parseReportByType(report, type_arr[i], buffer);

    uint8_t* ptr = buffer;

    // look for uuid in the uuid list
    while( len )
    {
      // found matched
      if ( !memcmp(ptr, uuid, uuid_len) )
      {
        return true;
      }else
      {
        ptr += uuid_len;
        len -= uuid_len;
      }
    }
  }

  return false;
}

bool BLEScanner::checkReportForService(const ble_gap_evt_adv_report_t* report, BLEClientService svc)
{
  return checkReportForUuid(report, svc.uuid);
}

bool BLEScanner::checkReportForService(const ble_gap_evt_adv_report_t* report, BLEService svc)
{
  return checkReportForUuid(report, svc.uuid);
}

void BLEScanner::filterRssi(int8_t min_rssi)
{
  _filter_rssi = min_rssi;
}

void BLEScanner::filterUuid(BLEUuid ble_uuid)
{
  filterUuid(&ble_uuid, 1);
}

void BLEScanner::filterUuid(BLEUuid ble_uuid1, BLEUuid ble_uuid2)
{
  BLEUuid bleuuid[] = {ble_uuid1, ble_uuid2};
  filterUuid( bleuuid , 2);
}

void BLEScanner::filterUuid(BLEUuid ble_uuid1, BLEUuid ble_uuid2, BLEUuid ble_uuid3)
{
  BLEUuid bleuuid[] = {ble_uuid1, ble_uuid2, ble_uuid3};
  filterUuid( bleuuid , 3);
}

void BLEScanner::filterUuid(BLEUuid ble_uuid1, BLEUuid ble_uuid2, BLEUuid ble_uuid3, BLEUuid ble_uuid4)
{
  BLEUuid bleuuid[] = {ble_uuid1, ble_uuid2, ble_uuid3, ble_uuid4};
  filterUuid( bleuuid , 4);
}

void BLEScanner::filterUuid(BLEUuid ble_uuid[], uint8_t count)
{
  if (_filter_uuid_count) delete[] _filter_uuid;

  _filter_uuid_count = count;
  if (count == 0) return;

  _filter_uuid = new BLEUuid[count];

  for(uint8_t i=0; i<count; i++) _filter_uuid[i] = ble_uuid[i];
}

void BLEScanner::filterMSD(uint16_t manuf_id)
{
  _filter_msd_en = true;
  _filter_msd_id = manuf_id;
}

void BLEScanner::clearFilters(void)
{
  _filter_rssi   = INT8_MIN;
  _filter_msd_en = false;

  if ( _filter_uuid_count )
  {
    delete[] _filter_uuid;
    _filter_uuid = NULL;

    _filter_uuid_count = 0;
  }

}

/**
 * Event Handler
 * @param evt
 */
void BLEScanner::_eventHandler(ble_evt_t* evt)
{
  switch ( evt->header.evt_id  )
  {
    case BLE_GAP_EVT_ADV_REPORT:
    { // evt_conn_hdl is equal to BLE_CONN_HANDLE_INVALID
      ble_gap_evt_adv_report_t const* evt_report = &evt->evt.gap_evt.params.adv_report;

      // filter by rssi
      if ( _filter_rssi > evt_report->rssi ) break;

      // filter by uuid
      if ( _filter_uuid_count )
      {
        uint8_t i;
        for(i=0; i<_filter_uuid_count; i++)
        {
          if ( checkReportForUuid(evt_report, _filter_uuid[i]) ) break;
        }

        // If there is no matched UUID in the list --> filter failed
        if ( i ==  _filter_uuid_count ) break;
      }

      // filter by MSD if present
      if ( _filter_msd_en )
      {
        uint16_t id;
        if ( !(parseReportByType(evt_report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, (uint8_t*)&id, 2) && (id == _filter_msd_id)) ) break;
      }

      ble_gap_evt_adv_report_t* adv_report = (ble_gap_evt_adv_report_t*) rtos_malloc( sizeof(ble_gap_evt_adv_report_t) );
      (*adv_report) = (*evt_report);
      if (_rx_cb) ada_callback(adv_report, _rx_cb, adv_report);
    }
    break;

    case BLE_GAP_EVT_CONNECTED:
    {
      ble_gap_evt_connected_t const * para = &evt->evt.gap_evt.params.connected;

      if ( para->role == BLE_GAP_ROLE_CENTRAL)
      {
        _runnning = false;

        // Turn on Conn LED
        Bluefruit._stopConnLed();
        Bluefruit._setConnLed(true);
      }
    }
    break;

    case BLE_GAP_EVT_DISCONNECTED:
      if ( BLE_GAP_ROLE_CENTRAL == Bluefruit.Gap.getRole(evt->evt.common_evt.conn_handle) )
      {
        // skip if already running
        if ( !_runnning )
        {
          // Turn off Conn LED
          Bluefruit._setConnLed(false);

          // Auto start if enabled
          if ( _start_if_disconnect )
          {
            start(_param.timeout);
          }
        }
      }
    break;

    case BLE_GAP_EVT_TIMEOUT:
      if (evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_SCAN)
      {
        if (_stop_cb) ada_callback_defer(NULL, _stop_cb);
      }
    break;

    default: break;
  }
}
