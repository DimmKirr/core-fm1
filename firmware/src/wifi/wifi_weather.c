/* SPDX-License-Identifier: GPL-3.0-only */
/* Async two-step weather fetcher: geo-locate via ip-api.com, then
 * current weather from Open-Meteo. Non-blocking, callback-driven. */

#ifdef CONFIG_WIFI_LWIP

/* ---- weather data ----------------------------------------------------- */

struct weather_data {
    int16_t  temp_c;       /* temperature x10 (225 = 22.5C) */
    uint8_t  humidity;     /* 0-100% */
    uint8_t  wmo_code;     /* WMO weather code */
    uint16_t wind_kmh;     /* wind speed x10 */
    char     city[24];
    char     country[4];
    uint32_t updated_ms;   /* fm1_ms of last update */
    uint8_t  valid;        /* 1 if data populated */
};

/* ---- state machine ---------------------------------------------------- */

#define WEATHER_IDLE          0
#define WEATHER_GEO_FETCH     1
#define WEATHER_GEO_WAIT      2
#define WEATHER_WEATHER_FETCH 3
#define WEATHER_WEATHER_WAIT  4
#define WEATHER_DONE          5

#define WEATHER_REFRESH_MS    (15u * 60u * 1000u)  /* 15 minutes */
#define WEATHER_RETRY_MS      (60u * 1000u)         /* 60 seconds */
#define WEATHER_MAX_RETRIES   3

static struct weather_data weather_dat;
static uint8_t  weather_state;
static uint32_t weather_last_ms;
static uint8_t  weather_retries;

/* geo data (statics, kept between steps) */
static char weather_lat[12];
static char weather_lon[12];

/* ---- minimal JSON key-value scanner ----------------------------------- */

/* Scan for "key":value in a JSON blob. Writes the value (unquoted for
 * strings, raw for numbers) into out[]. Returns length written, 0 on miss. */
static uint32_t mini_json_extract(const uint8_t *json, uint32_t json_len,
                                  const char *key, char *out, uint32_t out_max)
{
    uint32_t klen = 0;
    uint32_t i, j;
    const char *k = key;
    while (*k++) klen++;

    if (out_max == 0) return 0;

    for (i = 0; i + klen + 2 < json_len; i++) {
        /* look for "key" */
        if (json[i] != '"') continue;
        {
            uint32_t m;
            uint8_t match = 1;
            for (m = 0; m < klen; m++) {
                if (i + 1 + m >= json_len || json[i + 1 + m] != (uint8_t)key[m]) {
                    match = 0;
                    break;
                }
            }
            if (!match) continue;
            if (i + 1 + klen >= json_len || json[i + 1 + klen] != '"') continue;
        }

        /* skip past "key": */
        j = i + 1 + klen + 1; /* past closing quote */
        while (j < json_len && (json[j] == ':' || json[j] == ' ')) j++;
        if (j >= json_len) return 0;

        if (json[j] == '"') {
            /* string value */
            uint32_t start = j + 1;
            uint32_t end = start;
            uint32_t n;
            while (end < json_len && json[end] != '"') end++;
            n = end - start;
            if (n >= out_max) n = out_max - 1;
            {
                uint32_t c;
                for (c = 0; c < n; c++)
                    out[c] = (char)json[start + c];
            }
            out[n] = 0;
            return n;
        } else {
            /* numeric value (may include '-', '.', digits) */
            uint32_t start = j;
            uint32_t end = start;
            uint32_t n;
            while (end < json_len && json[end] != ',' && json[end] != '}'
                   && json[end] != ' ' && json[end] != '\r'
                   && json[end] != '\n')
                end++;
            n = end - start;
            if (n >= out_max) n = out_max - 1;
            {
                uint32_t c;
                for (c = 0; c < n; c++)
                    out[c] = (char)json[start + c];
            }
            out[n] = 0;
            return n;
        }
    }
    out[0] = 0;
    return 0;
}

/* ---- simple string-to-fixed-point parser ------------------------------ */

/* Parse a decimal string like "-12.5" into a fixed-point x10 int.
 * Returns e.g. -125 for "-12.5". */
static int32_t parse_fixed10(const char *s)
{
    int32_t sign = 1;
    int32_t whole = 0;
    int32_t frac = 0;
    uint8_t has_dot = 0;

    if (*s == '-') { sign = -1; s++; }
    while (*s) {
        if (*s == '.') { has_dot = 1; s++; break; }
        if (*s >= '0' && *s <= '9')
            whole = whole * 10 + (*s - '0');
        s++;
    }
    if (has_dot && *s >= '0' && *s <= '9')
        frac = *s - '0';

    return sign * (whole * 10 + frac);
}

/* ---- geo callback ----------------------------------------------------- */

static void weather_geo_cb(int status, const uint8_t *body,
                           uint32_t body_len, void *ctx)
{
    char tmp[32];
    (void)ctx;

    if (status != 200 || !body || body_len == 0) {
        cfm1_log_int("wifi: geo failed status=", (int32_t)status);
        weather_state = WEATHER_IDLE;
        weather_retries++;
        weather_last_ms = fm1_ms;
        return;
    }

    /* extract lat, lon, city, countryCode */
    if (mini_json_extract(body, body_len, "lat", weather_lat, sizeof(weather_lat)) == 0 ||
        mini_json_extract(body, body_len, "lon", weather_lon, sizeof(weather_lon)) == 0) {
        cfm1_log("wifi: geo missing lat/lon");
        weather_state = WEATHER_IDLE;
        weather_retries++;
        weather_last_ms = fm1_ms;
        return;
    }

    if (mini_json_extract(body, body_len, "city", tmp, sizeof(tmp)) > 0) {
        uint32_t i;
        for (i = 0; i < sizeof(weather_dat.city) - 1 && tmp[i]; i++)
            weather_dat.city[i] = tmp[i];
        weather_dat.city[i] = 0;
    }

    if (mini_json_extract(body, body_len, "countryCode", tmp, sizeof(tmp)) > 0) {
        uint32_t i;
        for (i = 0; i < sizeof(weather_dat.country) - 1 && tmp[i]; i++)
            weather_dat.country[i] = tmp[i];
        weather_dat.country[i] = 0;
    }

    cfm1_log("wifi: geo ok");
    weather_state = WEATHER_WEATHER_FETCH;
}

/* ---- weather callback ------------------------------------------------- */

static void weather_wx_cb(int status, const uint8_t *body,
                          uint32_t body_len, void *ctx)
{
    char tmp[32];
    (void)ctx;

    if (status != 200 || !body || body_len == 0) {
        cfm1_log_int("wifi: weather failed status=", (int32_t)status);
        weather_state = WEATHER_IDLE;
        weather_retries++;
        weather_last_ms = fm1_ms;
        return;
    }

    /* temperature_2m */
    if (mini_json_extract(body, body_len, "temperature_2m", tmp, sizeof(tmp)) > 0)
        weather_dat.temp_c = (int16_t)parse_fixed10(tmp);

    /* relative_humidity_2m */
    if (mini_json_extract(body, body_len, "relative_humidity_2m", tmp, sizeof(tmp)) > 0) {
        int32_t v = parse_fixed10(tmp);
        weather_dat.humidity = (uint8_t)(v / 10);
    }

    /* weather_code */
    if (mini_json_extract(body, body_len, "weather_code", tmp, sizeof(tmp)) > 0) {
        int32_t v = parse_fixed10(tmp);
        weather_dat.wmo_code = (uint8_t)(v / 10);
    }

    /* wind_speed_10m */
    if (mini_json_extract(body, body_len, "wind_speed_10m", tmp, sizeof(tmp)) > 0)
        weather_dat.wind_kmh = (uint16_t)parse_fixed10(tmp);

    weather_dat.updated_ms = fm1_ms;
    weather_dat.valid = 1;
    cfm1_log_int("wifi: weather ok temp=", (int32_t)weather_dat.temp_c);
    weather_state = WEATHER_DONE;
    weather_last_ms = fm1_ms;
    weather_retries = 0;
}

/* ---- public API ------------------------------------------------------- */

static void weather_init(void)
{
    weather_state = WEATHER_IDLE;
    weather_last_ms = 0;
    weather_retries = 0;
    weather_dat.valid = 0;
    weather_lat[0] = 0;
    weather_lon[0] = 0;
}

static void weather_fetch_start(void)
{
    if (weather_state != WEATHER_IDLE && weather_state != WEATHER_DONE)
        return;
    cfm1_log("wifi: weather fetch start");
    weather_state = WEATHER_GEO_FETCH;
    weather_retries = 0;
}

static void weather_poll(void)
{
    switch (weather_state) {

    case WEATHER_GEO_FETCH:
        if (http_busy()) break;
        if (http_get("ip-api.com", "/json?fields=lat,lon,city,countryCode",
                     weather_geo_cb, 0) == 0) {
            weather_state = WEATHER_GEO_WAIT;
        } else {
            weather_state = WEATHER_IDLE;
            weather_retries++;
            weather_last_ms = fm1_ms;
        }
        break;

    case WEATHER_GEO_WAIT:
        /* waiting for geo callback */
        break;

    case WEATHER_WEATHER_FETCH: {
        char path[160];
        uint32_t pi = 0;
        const char *s;

        if (http_busy()) break;

        /* build URL path */
        s = "/v1/forecast?latitude=";
        while (*s) path[pi++] = *s++;
        s = weather_lat;
        while (*s && pi < sizeof(path) - 80) path[pi++] = *s++;
        s = "&longitude=";
        while (*s) path[pi++] = *s++;
        s = weather_lon;
        while (*s && pi < sizeof(path) - 60) path[pi++] = *s++;
        s = "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
        while (*s && pi < sizeof(path) - 1) path[pi++] = *s++;
        path[pi] = 0;

        if (http_get("api.open-meteo.com", path, weather_wx_cb, 0) == 0) {
            weather_state = WEATHER_WEATHER_WAIT;
        } else {
            weather_state = WEATHER_IDLE;
            weather_retries++;
            weather_last_ms = fm1_ms;
        }
        break;
    }

    case WEATHER_WEATHER_WAIT:
        /* waiting for weather callback */
        break;

    case WEATHER_DONE:
        /* auto-refresh after 15 minutes */
        if (fm1_ms - weather_last_ms > WEATHER_REFRESH_MS)
            weather_state = WEATHER_GEO_FETCH;
        break;

    case WEATHER_IDLE:
        /* retry after error if retries remain */
        if (weather_retries > 0 && weather_retries <= WEATHER_MAX_RETRIES &&
            fm1_ms - weather_last_ms > WEATHER_RETRY_MS) {
            weather_state = WEATHER_GEO_FETCH;
        }
        break;
    }
}

static int weather_fetching(void)
{
    return weather_state != WEATHER_IDLE && weather_state != WEATHER_DONE;
}

static const struct weather_data *weather_get(void)
{
    return &weather_dat;
}

#else /* !CONFIG_WIFI_LWIP */

struct weather_data {
    int16_t  temp_c;
    uint8_t  humidity;
    uint8_t  wmo_code;
    uint16_t wind_kmh;
    char     city[24];
    char     country[4];
    uint32_t updated_ms;
    uint8_t  valid;
};

static struct weather_data weather_dat;
static void weather_init(void) {}
static void weather_fetch_start(void) {}
static void weather_poll(void) {}
static int  weather_fetching(void) { return 0; }
static const struct weather_data *weather_get(void) { return &weather_dat; }

#endif /* CONFIG_WIFI_LWIP */
