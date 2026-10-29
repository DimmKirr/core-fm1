#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Weather fetcher tests.

Tests the mini JSON parser and weather data extraction logic using
sample ip-api.com and Open-Meteo responses.
"""
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WEATHER_C = ROOT / 'firmware' / 'src' / 'wifi' / 'wifi_weather.c'


# ---- Python reimplementation of mini_json_extract ----------------------


def mini_json_extract(json_bytes: bytes, key: str) -> str | None:
    """Minimal JSON key-value scanner matching the C implementation.
    Returns the value as a string, or None if not found."""
    klen = len(key)
    json_len = len(json_bytes)

    i = 0
    while i + klen + 2 < json_len:
        if json_bytes[i:i+1] != b'"':
            i += 1
            continue

        # check if key matches
        if (json_bytes[i+1:i+1+klen] == key.encode() and
                i + 1 + klen < json_len and
                json_bytes[i+1+klen:i+1+klen+1] == b'"'):

            # skip past "key":
            j = i + 1 + klen + 1
            while j < json_len and json_bytes[j:j+1] in (b':', b' '):
                j += 1
            if j >= json_len:
                return None

            if json_bytes[j:j+1] == b'"':
                # string value
                start = j + 1
                end = start
                while end < json_len and json_bytes[end:end+1] != b'"':
                    end += 1
                return json_bytes[start:end].decode('utf-8', errors='replace')
            else:
                # numeric value
                start = j
                end = start
                while (end < json_len and
                       json_bytes[end:end+1] not in (b',', b'}', b' ', b'\r', b'\n')):
                    end += 1
                return json_bytes[start:end].decode('utf-8', errors='replace')

        i += 1

    return None


def parse_fixed10(s: str) -> int:
    """Parse decimal string to fixed-point x10, matching C implementation."""
    sign = 1
    idx = 0
    if s and s[0] == '-':
        sign = -1
        idx = 1

    whole = 0
    while idx < len(s) and s[idx] != '.':
        if '0' <= s[idx] <= '9':
            whole = whole * 10 + int(s[idx])
        idx += 1

    frac = 0
    if idx < len(s) and s[idx] == '.':
        idx += 1
        if idx < len(s) and '0' <= s[idx] <= '9':
            frac = int(s[idx])

    return sign * (whole * 10 + frac)


# ---- Sample API responses ----------------------------------------------

GEO_RESPONSE = b'''{
  "lat": 52.5244,
  "lon": 13.4105,
  "city": "Berlin",
  "countryCode": "DE"
}'''

WEATHER_RESPONSE = b'''{
  "current": {
    "temperature_2m": 22.5,
    "relative_humidity_2m": 65,
    "weather_code": 3,
    "wind_speed_10m": 14.2
  }
}'''

WEATHER_NEGATIVE_TEMP = b'''{
  "current": {
    "temperature_2m": -5.3,
    "relative_humidity_2m": 89,
    "weather_code": 71,
    "wind_speed_10m": 8.0
  }
}'''


# ---- Tests: mini_json_extract -----------------------------------------


class TestMiniJsonExtract:
    def test_geo_lat(self):
        assert mini_json_extract(GEO_RESPONSE, 'lat') == '52.5244'

    def test_geo_lon(self):
        assert mini_json_extract(GEO_RESPONSE, 'lon') == '13.4105'

    def test_geo_city(self):
        assert mini_json_extract(GEO_RESPONSE, 'city') == 'Berlin'

    def test_geo_country(self):
        assert mini_json_extract(GEO_RESPONSE, 'countryCode') == 'DE'

    def test_missing_key(self):
        assert mini_json_extract(GEO_RESPONSE, 'timezone') is None

    def test_weather_temp(self):
        assert mini_json_extract(WEATHER_RESPONSE, 'temperature_2m') == '22.5'

    def test_weather_humidity(self):
        assert mini_json_extract(WEATHER_RESPONSE, 'relative_humidity_2m') == '65'

    def test_weather_code(self):
        assert mini_json_extract(WEATHER_RESPONSE, 'weather_code') == '3'

    def test_weather_wind(self):
        assert mini_json_extract(WEATHER_RESPONSE, 'wind_speed_10m') == '14.2'

    def test_negative_temp(self):
        assert mini_json_extract(WEATHER_NEGATIVE_TEMP, 'temperature_2m') == '-5.3'

    def test_empty_json(self):
        assert mini_json_extract(b'{}', 'key') is None

    def test_partial_key_no_match(self):
        """Should not match 'lat' when JSON has 'latitude'."""
        data = b'{"latitude": 52.0}'
        assert mini_json_extract(data, 'lat') is None


# ---- Tests: parse_fixed10 ---------------------------------------------


class TestParseFixed10:
    def test_positive_decimal(self):
        assert parse_fixed10('22.5') == 225

    def test_negative_decimal(self):
        assert parse_fixed10('-5.3') == -53

    def test_integer(self):
        assert parse_fixed10('65') == 650

    def test_zero(self):
        assert parse_fixed10('0') == 0

    def test_zero_decimal(self):
        assert parse_fixed10('0.0') == 0

    def test_large_value(self):
        assert parse_fixed10('100.0') == 1000

    def test_negative_integer(self):
        assert parse_fixed10('-12') == -120

    def test_single_digit_frac(self):
        assert parse_fixed10('8.0') == 80

    def test_multi_digit_frac_truncated(self):
        """C impl only reads first fractional digit."""
        assert parse_fixed10('14.27') == 142


# ---- Tests: weather data extraction flow -------------------------------


class TestWeatherDataExtraction:
    """Simulate the full extraction pipeline as weather_wx_cb does."""

    def test_full_weather_parse(self):
        body = WEATHER_RESPONSE
        temp_s = mini_json_extract(body, 'temperature_2m')
        hum_s = mini_json_extract(body, 'relative_humidity_2m')
        wmo_s = mini_json_extract(body, 'weather_code')
        wind_s = mini_json_extract(body, 'wind_speed_10m')

        assert temp_s is not None
        assert hum_s is not None
        assert wmo_s is not None
        assert wind_s is not None

        temp_c = parse_fixed10(temp_s)    # x10
        humidity = parse_fixed10(hum_s) // 10
        wmo_code = parse_fixed10(wmo_s) // 10
        wind_kmh = parse_fixed10(wind_s)  # x10

        assert temp_c == 225     # 22.5C
        assert humidity == 65
        assert wmo_code == 3     # overcast
        assert wind_kmh == 142   # 14.2 km/h

    def test_negative_temp_parse(self):
        body = WEATHER_NEGATIVE_TEMP
        temp_s = mini_json_extract(body, 'temperature_2m')
        assert temp_s is not None
        temp_c = parse_fixed10(temp_s)
        assert temp_c == -53     # -5.3C

    def test_geo_parse(self):
        body = GEO_RESPONSE
        lat = mini_json_extract(body, 'lat')
        lon = mini_json_extract(body, 'lon')
        city = mini_json_extract(body, 'city')
        cc = mini_json_extract(body, 'countryCode')

        assert lat == '52.5244'
        assert lon == '13.4105'
        assert city == 'Berlin'
        assert cc == 'DE'


# ---- Tests: source file structure --------------------------------------


class TestSourceFile:
    def test_weather_file_exists(self):
        assert WEATHER_C.exists(), 'wifi_weather.c not found'

    def test_has_state_machine(self):
        text = WEATHER_C.read_text()
        for state in ['WEATHER_IDLE', 'WEATHER_GEO_FETCH',
                       'WEATHER_GEO_WAIT', 'WEATHER_WEATHER_FETCH',
                       'WEATHER_WEATHER_WAIT', 'WEATHER_DONE']:
            assert state in text, f'Missing state: {state}'

    def test_has_weather_data_struct(self):
        text = WEATHER_C.read_text()
        assert 'struct weather_data' in text
        assert 'temp_c' in text
        assert 'wmo_code' in text
        assert 'wind_kmh' in text

    def test_has_public_api(self):
        text = WEATHER_C.read_text()
        for fn in ['weather_init', 'weather_fetch_start', 'weather_poll',
                    'weather_fetching', 'weather_get']:
            assert fn in text, f'Missing function: {fn}'

    def test_has_stubs(self):
        text = WEATHER_C.read_text()
        assert '!CONFIG_WIFI_LWIP' in text, 'Missing stub guard'

    def test_refresh_timer(self):
        text = WEATHER_C.read_text()
        assert 'WEATHER_REFRESH_MS' in text
        assert '15' in text, 'Expected 15-minute refresh interval'
