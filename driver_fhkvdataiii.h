/*
  Based on: https://github.com/wmbusmeters/wmbusmeters/blob/master/src/driver_fhkvdataiii.cc
  Copyright (C) 2019-2022 Fredrik Öhrström (gpl-3.0-or-later)
*/

#pragma once

#include "driver.h"

#include <vector>
#include <string>

struct FhkvdataIII: Driver
{
  FhkvdataIII(std::string key = "") : Driver(std::string("fhkvdataiii"), key) {};
  virtual esphome::optional<std::map<std::string, double>> get_values(std::vector<unsigned char> &telegram) override {
    std::map<std::string, double> ret_val{};

    // Since ~04.2026 Techem devices interleave several telegram types.
    // Only dll type 0x80 telegrams carry the FHKV data III payload; other
    // types (e.g. 0x08) must not be parsed, previously they produced bogus
    // values like 313.25 C.
    if (telegram.size() < 12 || telegram[9] != 0x80) {
      return {};
    }

    esphome::optional<size_t> payload = this->find_payload(telegram);
    if (!payload.has_value()) {
      return {};
    }
    size_t p = payload.value();

    // Payload layout per wmbusmeters fhkvdataiii grammar:
    //   version_01: '01' PrevDate PrevHca CurrDate CurrHca TempRoom TempRad
    //   version_69: '11' PrevDate PrevHca CurrDate CurrHca TempRoom TempRad
    //   version_94: '0F' PrevDate PrevHca CurrDate CurrHca Extra TempRoom TempRad
    size_t temp_offset = (telegram[p] == 0x0F) ? 10 : 9;

    add_to_map(ret_val, "previous_hca", this->get_word(telegram, p + 3));
    add_to_map(ret_val, "current_hca", this->get_word(telegram, p + 7));
    add_to_map(ret_val, "temp_room_c", this->get_temp(telegram, p + temp_offset));
    add_to_map(ret_val, "temp_radiator_c", this->get_temp(telegram, p + temp_offset + 2));

    if (ret_val.size() > 0) {
      return ret_val;
    }
    else {
      return {};
    }
  };

private:
  // The FHKV data III payload starts with a format byte ('01', '11' or '0F').
  // In the original frames it directly follows the CI field (index 11); frames
  // transmitted since ~04.2026 insert a 12 byte vendor header, moving the
  // payload to index 23. Probe both known locations instead of assuming
  // fixed field offsets (the pre-2026 behaviour of this driver).
  esphome::optional<size_t> find_payload(std::vector<unsigned char> &telegram) {
    esphome::optional<size_t> ret_val{};
    for (size_t p : {(size_t)11, (size_t)23}) {
      // longest variant: format byte + 4 words + Extra + 2 temp words
      if (telegram.size() < (p + 14)) {
        continue;
      }
      if ((telegram[p] == 0x01) || (telegram[p] == 0x11) || (telegram[p] == 0x0F)) {
        ESP_LOGVV(TAG, "Found payload version 0x%02X at offset %d", telegram[p], p);
        ret_val = p;
        return ret_val;
      }
    }
    ESP_LOGVV(TAG, "No FHKV payload found in telegram");
    return ret_val;
  };

  esphome::optional<double> get_word(std::vector<unsigned char> &telegram, size_t i) {
    esphome::optional<double> ret_val{};
    ret_val = (double)(((uint32_t)telegram[i+1] << 8) + (uint32_t)telegram[i]);
    return ret_val;
  };

  esphome::optional<double> get_temp(std::vector<unsigned char> &telegram, size_t i) {
    esphome::optional<double> ret_val{};
    double temp = (((uint32_t)telegram[i+1] << 8) + (uint32_t)telegram[i]) / 100.0;
    // Plausibility guard: if the transmitted format shifts again, refuse to
    // publish header bytes as temperature instead of reporting hundreds of C.
    if ((temp <= 0.0) || (temp >= 100.0)) {
      ESP_LOGVV(TAG, "Temperature %f out of range, dropping", temp);
      return ret_val;
    }
    ret_val = temp;
    ESP_LOGVV(TAG, "Found temperature with '%f'", temp);
    return ret_val;
  };
};
