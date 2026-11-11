#include "http_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_events.h"
#include "button_service.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "stock_service.h"
#include "wifi_manager.h"

#define HTTP_API_JSON_BODY_MAX 2048u

static const char *TAG = "config_api";
static httpd_handle_t s_server;

extern const uint8_t index_html_gz_start[]
    asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]
    asm("_binary_index_html_gz_end");
extern const uint8_t manifest_webmanifest_start[]
    asm("_binary_manifest_webmanifest_start");
extern const uint8_t manifest_webmanifest_end[]
    asm("_binary_manifest_webmanifest_end");
extern const uint8_t icon_svg_start[] asm("_binary_icon_svg_start");
extern const uint8_t icon_svg_end[] asm("_binary_icon_svg_end");

static esp_err_t send_json(httpd_req_t *request, cJSON *root)
{
    char *payload;
    esp_err_t err;

    if (root == NULL) {
        return httpd_resp_send_500(request);
    }
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return httpd_resp_send_500(request);
    }

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    err = httpd_resp_send(request, payload, HTTPD_RESP_USE_STRLEN);
    cJSON_free(payload);
    return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *status,
                            const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();

    if (root == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", code);
    cJSON_AddStringToObject(root, "message", message);
    httpd_resp_set_status(request, status);
    return send_json(request, root);
}

static esp_err_t receive_json(httpd_req_t *request, cJSON **root)
{
    char *body;
    size_t received = 0u;

    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *root = NULL;
    if (request->content_len <= 0 ||
        request->content_len > HTTP_API_JSON_BODY_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    body = malloc((size_t)request->content_len + 1u);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)request->content_len) {
        int read_size = httpd_req_recv(
            request, body + received,
            (size_t)request->content_len - received);

        if (read_size <= 0) {
            free(body);
            return read_size == HTTPD_SOCK_ERR_TIMEOUT
                       ? ESP_ERR_TIMEOUT
                       : ESP_FAIL;
        }
        received += (size_t)read_size;
    }
    body[received] = '\0';
    *root = cJSON_Parse(body);
    free(body);
    return *root == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static const char *feedback_mode_name(stock_feedback_mode_t mode)
{
    switch (mode) {
    case STOCK_FEEDBACK_VIBRATE:
        return "vibrate";
    case STOCK_FEEDBACK_SOUND:
        return "sound";
    case STOCK_FEEDBACK_SILENT:
        return "silent";
    case STOCK_FEEDBACK_BOTH:
    default:
        return "both";
    }
}

static bool parse_feedback_mode(const char *name,
                                stock_feedback_mode_t *mode)
{
    if (name == NULL || mode == NULL) {
        return false;
    }
    if (strcmp(name, "both") == 0) {
        *mode = STOCK_FEEDBACK_BOTH;
    } else if (strcmp(name, "vibrate") == 0) {
        *mode = STOCK_FEEDBACK_VIBRATE;
    } else if (strcmp(name, "sound") == 0) {
        *mode = STOCK_FEEDBACK_SOUND;
    } else if (strcmp(name, "silent") == 0) {
        *mode = STOCK_FEEDBACK_SILENT;
    } else {
        return false;
    }
    return true;
}

static bool parse_button_id(const char *name, button_id_t *button)
{
    if (name == NULL || button == NULL) {
        return false;
    }
    if (strcmp(name, "k1") == 0) {
        *button = BUTTON_SERVICE_K1;
    } else if (strcmp(name, "k2") == 0) {
        *button = BUTTON_SERVICE_K2;
    } else if (strcmp(name, "k3") == 0) {
        *button = BUTTON_SERVICE_K3;
    } else if (strcmp(name, "k4") == 0) {
        *button = BUTTON_SERVICE_K4;
    } else {
        return false;
    }
    return true;
}

static bool parse_button_event(const char *name, button_event_type_t *type)
{
    if (name == NULL || type == NULL) {
        return false;
    }
    if (strcmp(name, "short") == 0) {
        *type = BUTTON_EVENT_SHORT_PRESS;
    } else if (strcmp(name, "long") == 0) {
        *type = BUTTON_EVENT_LONG_PRESS;
    } else {
        return false;
    }
    return true;
}

static bool parse_alert_price(const cJSON *item, uint32_t *price_units)
{
    double price;
    long value;

    if (price_units == NULL) {
        return false;
    }
    if (item == NULL || cJSON_IsNull(item)) {
        *price_units = 0u;
        return true;
    }
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    price = item->valuedouble;
    if (price <= 0.0 ||
        price > (double)STOCK_ALERT_PRICE_MAX_UNITS /
                    (double)STOCK_ALERT_PRICE_UNITS_PER_YUAN) {
        return false;
    }
    value = lround(price * (double)STOCK_ALERT_PRICE_UNITS_PER_YUAN);
    if (value <= 0 ||
        value > (long)STOCK_ALERT_PRICE_MAX_UNITS) {
        return false;
    }
    *price_units = (uint32_t)value;
    return true;
}

static double finite_number(double value)
{
    return isfinite(value) ? value : 0.0;
}

static bool add_stock_config(cJSON *root)
{
    stock_service_config_t config;
    stock_service_snapshot_t snapshot;
    cJSON *stock;
    cJSON *symbols;
    cJSON *items;

    stock_service_get_config(&config);
    stock_service_get_snapshot(&snapshot);
    stock = cJSON_AddObjectToObject(root, "stock");
    if (stock == NULL) {
        return false;
    }
    cJSON_AddNumberToObject(stock, "refresh_seconds",
                            config.refresh_seconds);
    cJSON_AddBoolToObject(stock, "alerts_enabled", config.alerts_enabled);
    cJSON_AddStringToObject(stock, "feedback_mode",
                            feedback_mode_name(config.feedback_mode));
    symbols = cJSON_AddArrayToObject(stock, "symbols");
    if (symbols == NULL) {
        return false;
    }
    for (size_t index = 0u; index < config.symbol_count; index++) {
        cJSON_AddItemToArray(
            symbols, cJSON_CreateString(config.symbols[index]));
    }
    items = cJSON_AddArrayToObject(stock, "items");
    if (items == NULL) {
        return false;
    }
    for (size_t index = 0u; index < config.symbol_count; index++) {
        const stock_quote_t *quote = &snapshot.quotes[index];
        cJSON *item = cJSON_CreateObject();

        if (item == NULL) {
            return false;
        }
        cJSON_AddStringToObject(item, "symbol", config.symbols[index]);
        cJSON_AddBoolToObject(item, "valid", quote->valid);
        cJSON_AddNumberToObject(item, "price",
                                finite_number(quote->price));
        cJSON_AddNumberToObject(item, "high", finite_number(quote->high));
        cJSON_AddNumberToObject(item, "low", finite_number(quote->low));
        cJSON_AddNumberToObject(item, "change_percent",
                                finite_number(quote->change_percent));
        cJSON_AddStringToObject(item, "updated_at", quote->updated_at);
        cJSON_AddNumberToObject(
            item, "alert_upper_price",
            (double)config.alert_upper_price_units[index] /
                (double)STOCK_ALERT_PRICE_UNITS_PER_YUAN);
        cJSON_AddNumberToObject(
            item, "alert_lower_price",
            (double)config.alert_lower_price_units[index] /
                (double)STOCK_ALERT_PRICE_UNITS_PER_YUAN);
        cJSON_AddItemToArray(items, item);
    }
    return true;
}

static bool add_board_config(cJSON *root)
{
    const app_board_config_t *board = &app_config_get()->board;
    cJSON *object = cJSON_AddObjectToObject(root, "board");
    cJSON *buttons;

    if (object == NULL) {
        return false;
    }
    cJSON_AddBoolToObject(object, "enabled", board->enabled);
    cJSON_AddStringToObject(
        object, "oled_controller",
        board->oled_controller == APP_OLED_CONTROLLER_SH1106
            ? "sh1106"
            : "ssd1306");
    buttons = cJSON_AddArrayToObject(object, "button_gpio");
    if (buttons == NULL) {
        return false;
    }
    for (size_t index = 0u; index < 4u; index++) {
        cJSON_AddItemToArray(
            buttons, cJSON_CreateNumber(board->button_gpio[index]));
    }
    return true;
}

static bool add_wifi_config(cJSON *root)
{
    const app_config_t *config = app_config_get();
    wifi_manager_status_t status;
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");

    if (wifi == NULL) {
        return false;
    }
    wifi_manager_get_status(&status);
    cJSON_AddStringToObject(wifi, "ap_ssid", config->wifi.ap_ssid);
    cJSON_AddStringToObject(wifi, "ssid", status.ssid);
    cJSON_AddBoolToObject(wifi, "connected", status.connected);
    cJSON_AddBoolToObject(wifi, "connecting", status.connecting);
    cJSON_AddBoolToObject(wifi, "saved", status.saved);
    cJSON_AddStringToObject(wifi, "ip", status.ip);
    cJSON_AddNumberToObject(wifi, "rssi", status.rssi);
    cJSON_AddStringToObject(wifi, "last_error", status.last_error);
    return true;
}

static esp_err_t build_config_response(cJSON **root)
{
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *root = cJSON_CreateObject();
    if (*root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(*root, "ok", true);
    if (!add_wifi_config(*root) || !add_stock_config(*root) ||
        !add_board_config(*root)) {
        cJSON_Delete(*root);
        *root = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    cJSON *root;
    esp_err_t err = build_config_response(&root);

    ESP_LOGI(TAG, "GET %s", request->uri);
    if (err != ESP_OK) {
        return httpd_resp_send_500(request);
    }
    return send_json(request, root);
}

static esp_err_t button_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *button_name;
    cJSON *action_name;
    button_id_t button;
    button_event_type_t type;
    cJSON *response;
    const char *status;
    const char *code;
    const char *message;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json(request, &root);
    if (err != ESP_OK || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_json",
                          "请求内容不是有效 JSON");
    }

    button_name = cJSON_GetObjectItemCaseSensitive(root, "button");
    action_name = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(button_name) || button_name->valuestring == NULL ||
        !cJSON_IsString(action_name) || action_name->valuestring == NULL ||
        !parse_button_id(button_name->valuestring, &button) ||
        !parse_button_event(action_name->valuestring, &type)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_button",
                          "按键名称或操作类型无效");
    }

    err = button_service_inject_event(button, type);
    if (err != ESP_OK) {
        if (err == ESP_ERR_TIMEOUT) {
            status = "503 Service Unavailable";
            code = "button_queue_busy";
            message = "按键事件队列繁忙，请稍后重试";
        } else if (err == ESP_ERR_INVALID_STATE) {
            status = "409 Conflict";
            code = "button_service_unavailable";
            message = "按键服务当前不可用";
        } else {
            status = "500 Internal Server Error";
            code = "button_event_failed";
            message = "按键事件发送失败";
        }
        cJSON_Delete(root);
        return send_error(request, status, code, message);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "button",
                            button_name->valuestring);
    cJSON_AddStringToObject(response, "action",
                            action_name->valuestring);
    cJSON_Delete(root);
    httpd_resp_set_status(request, "202 Accepted");
    return send_json(request, response);
}

static esp_err_t config_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *items;
    cJSON *symbols;
    cJSON *entries;
    cJSON *refresh;
    cJSON *alerts;
    cJSON *feedback;
    stock_service_config_t config = {0};
    stock_feedback_mode_t mode;
    bool item_mode;
    int entry_count;
    esp_err_t err;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json(request, &root);
    if (err != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json",
                          "请求内容不是有效 JSON");
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_json",
                          "请求内容必须是 JSON 对象");
    }

    items = cJSON_GetObjectItemCaseSensitive(root, "items");
    symbols = cJSON_GetObjectItemCaseSensitive(root, "symbols");
    item_mode = items != NULL;
    entries = item_mode ? items : symbols;
    refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_seconds");
    alerts = cJSON_GetObjectItemCaseSensitive(root, "alerts_enabled");
    feedback = cJSON_GetObjectItemCaseSensitive(root, "feedback_mode");
    if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) < 1 ||
        cJSON_GetArraySize(entries) > STOCK_SERVICE_MAX_SYMBOLS ||
        !cJSON_IsNumber(refresh) || !cJSON_IsBool(alerts) ||
        !cJSON_IsString(feedback) ||
        feedback->valuestring == NULL ||
        !parse_feedback_mode(feedback->valuestring, &mode) ||
        !isfinite(refresh->valuedouble) ||
        refresh->valuedouble < (double)STOCK_REFRESH_MIN_SECONDS ||
        refresh->valuedouble > (double)STOCK_REFRESH_MAX_SECONDS ||
        floor(refresh->valuedouble) != refresh->valuedouble) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_stock_config",
                          "股票、刷新周期或反馈方式无效");
    }

    entry_count = cJSON_GetArraySize(entries);
    config.symbol_count = (uint8_t)entry_count;
    config.refresh_seconds =
        (uint16_t)lround(refresh->valuedouble);
    config.alerts_enabled = cJSON_IsTrue(alerts);
    config.feedback_mode = mode;
    for (size_t index = 0u; index < config.symbol_count; index++) {
        cJSON *entry = cJSON_GetArrayItem(entries, (int)index);
        cJSON *symbol;

        if (item_mode) {
            if (!cJSON_IsObject(entry)) {
                cJSON_Delete(root);
                return send_error(
                    request, "400 Bad Request", "invalid_stock_item",
                    "每只股票必须是独立配置对象");
            }
            symbol = cJSON_GetObjectItemCaseSensitive(entry, "symbol");
            if (!parse_alert_price(
                    cJSON_GetObjectItemCaseSensitive(
                        entry, "alert_upper_price"),
                    &config.alert_upper_price_units[index]) ||
                !parse_alert_price(
                    cJSON_GetObjectItemCaseSensitive(
                        entry, "alert_lower_price"),
                    &config.alert_lower_price_units[index])) {
                cJSON_Delete(root);
                return send_error(
                    request, "400 Bad Request", "invalid_alert_price",
                    "股票提醒价格应在 0.01 到 10000 元之间，留空表示关闭");
            }
        } else {
            symbol = entry;
        }
        if (!cJSON_IsString(symbol) || symbol->valuestring == NULL ||
            strlen(symbol->valuestring) > STOCK_SYMBOL_MAX_LEN) {
            cJSON_Delete(root);
            return send_error(request, "400 Bad Request",
                              "invalid_stock_symbol", "股票代码格式无效");
        }
        strcpy(config.symbols[index], symbol->valuestring);
    }
    cJSON_Delete(root);

    err = stock_service_set_config(&config);
    if (err != ESP_OK) {
        (void)app_events_publish(
            APP_EVENT_CONFIG_SAVE_FAILED, APP_EVENT_SOURCE_SYSTEM,
            APP_EVENT_SEVERITY_WARNING, err, config.symbol_count,
            config.feedback_mode);
        return send_error(
            request,
            err == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                       : "500 Internal Server Error",
            "stock_config_save_failed",
            err == ESP_ERR_INVALID_ARG
                ? "股票代码、刷新周期或价格提醒设置无效"
                : "股票配置保存失败");
    }
    (void)app_events_publish(
        APP_EVENT_CONFIG_UPDATED, APP_EVENT_SOURCE_SYSTEM,
        APP_EVENT_SEVERITY_INFO, config.symbol_count,
        config.refresh_seconds, config.feedback_mode);

    cJSON *response;
    err = build_config_response(&response);
    if (err != ESP_OK) {
        return httpd_resp_send_500(request);
    }
    return send_json(request, response);
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *request)
{
    wifi_manager_scan_result_t results[WIFI_MANAGER_SCAN_MAX_RESULTS];
    size_t count = 0u;
    esp_err_t err;
    cJSON *root;
    cJSON *networks;

    ESP_LOGI(TAG, "GET %s", request->uri);
    err = wifi_manager_scan(results, WIFI_MANAGER_SCAN_MAX_RESULTS, &count);
    if (err != ESP_OK) {
        return send_error(request, "409 Conflict", "wifi_scan_failed",
                          "无法扫描 Wi-Fi，请稍后重试");
    }

    root = cJSON_CreateObject();
    networks = cJSON_AddArrayToObject(root, "networks");
    if (root == NULL || networks == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    for (size_t index = 0u; index < count; index++) {
        cJSON *network = cJSON_CreateObject();

        if (network == NULL) {
            cJSON_Delete(root);
            return httpd_resp_send_500(request);
        }
        cJSON_AddStringToObject(network, "ssid", results[index].ssid);
        cJSON_AddNumberToObject(network, "rssi", results[index].rssi);
        cJSON_AddBoolToObject(network, "secure", results[index].secure);
        cJSON_AddItemToArray(networks, network);
    }
    return send_json(request, root);
}

static esp_err_t wifi_post_handler(httpd_req_t *request)
{
    cJSON *root = NULL;
    cJSON *ssid;
    cJSON *password;
    esp_err_t err;
    cJSON *response;

    ESP_LOGI(TAG, "POST %s", request->uri);
    err = receive_json(request, &root);
    if (err != ESP_OK) {
        return send_error(request, "400 Bad Request", "invalid_json",
                          "请求内容不是有效 JSON");
    }
    ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring == NULL ||
        password == NULL ||
        (!cJSON_IsString(password) && !cJSON_IsNull(password))) {
        cJSON_Delete(root);
        return send_error(request, "400 Bad Request", "invalid_wifi",
                          "Wi-Fi 名称或密码格式无效");
    }

    err = wifi_manager_connect_station(
        ssid->valuestring,
        cJSON_IsString(password) ? password->valuestring : "");
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return send_error(
            request,
            err == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                       : "409 Conflict",
            "wifi_connect_failed",
            err == ESP_ERR_INVALID_ARG
                ? "Wi-Fi 名称或密码不符合要求"
                : "无法发起 Wi-Fi 连接");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return httpd_resp_send_500(request);
    }
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddBoolToObject(response, "connecting", true);
    httpd_resp_set_status(request, "202 Accepted");
    return send_json(request, response);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    const size_t size =
        (size_t)(index_html_gz_end - index_html_gz_start);

    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(request, "X-Web-Build", "2026.11.11-stock-r3");
    httpd_resp_set_hdr(
        request, "Content-Security-Policy",
        "default-src 'self'; style-src 'self' 'unsafe-inline'; "
        "script-src 'self' 'unsafe-inline'; connect-src 'self'; "
        "img-src 'self' data:; base-uri 'none'; form-action 'none'");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(request, (const char *)index_html_gz_start,
                           (ssize_t)size);
}

static esp_err_t manifest_get_handler(httpd_req_t *request)
{
    const size_t size =
        (size_t)(manifest_webmanifest_end - manifest_webmanifest_start);

    httpd_resp_set_type(request, "application/manifest+json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, (const char *)manifest_webmanifest_start,
                           (ssize_t)size);
}

static esp_err_t icon_get_handler(httpd_req_t *request)
{
    const size_t size = (size_t)(icon_svg_end - icon_svg_start);

    httpd_resp_set_type(request, "image/svg+xml");
    httpd_resp_set_hdr(request, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(request, (const char *)icon_svg_start,
                           (ssize_t)size);
}

static const httpd_uri_t s_uris[] = {
    {.uri = "/", .method = HTTP_GET, .handler = root_get_handler},
    {.uri = "/api/v1/config",
     .method = HTTP_GET,
     .handler = config_get_handler},
    {.uri = "/api/v1/config",
     .method = HTTP_POST,
     .handler = config_post_handler},
    {.uri = "/api/v1/buttons",
     .method = HTTP_POST,
     .handler = button_post_handler},
    {.uri = "/api/v1/wifi/scan",
     .method = HTTP_GET,
     .handler = wifi_scan_get_handler},
    {.uri = "/api/v1/wifi",
     .method = HTTP_POST,
     .handler = wifi_post_handler},
    {.uri = "/manifest.webmanifest",
     .method = HTTP_GET,
     .handler = manifest_get_handler},
    {.uri = "/icon.svg",
     .method = HTTP_GET,
     .handler = icon_get_handler},
};

esp_err_t http_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t err;

    if (s_server != NULL) {
        return ESP_OK;
    }
    config.stack_size = 6144u;
    config.max_open_sockets = 4u;
    config.max_uri_handlers =
        (uint16_t)(sizeof(s_uris) / sizeof(s_uris[0]));
    config.max_resp_headers = 10u;
    config.recv_wait_timeout = 10u;
    config.send_wait_timeout = 30u;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t index = 0u; index < sizeof(s_uris) / sizeof(s_uris[0]);
         index++) {
        err = httpd_register_uri_handler(s_server, &s_uris[index]);
        if (err != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "configuration UI ready at http://192.168.4.1");
    return ESP_OK;
}
