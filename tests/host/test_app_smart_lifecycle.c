#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static time_t fixture_time(time_t *);
/* Compile and execute the production owner, including private transitions.
 * Only external hardware/network/RTOS boundaries below are stubbed. */
#define time fixture_time
#include "../../components/app_controller/app_controller.c"
#undef time

static time_t utc;
static int64_t mono;
static EventBits_t event_bits;
static command_t queued[32];
static size_t queue_count;
static bool queue_full, wifi_connected, wifi_provisioning, wifi_credentials, portal_running;
static bool physical_ok_pressed;
static unsigned station_starts, station_stops, provisioning_starts, provisioning_stops;
static unsigned portal_starts, portal_stops, nfc_stops, beeps, reminder_chimes;
static unsigned transcribes, interprets, saves, cancels;
static unsigned alert_volume_sets;
static uint8_t applied_alert_volume;
static esp_err_t save_result, load_result, audio_result, alert_save_result;
static smart_todo_list_t persisted;
static llm_settings_t loaded_llm;
static smart_todo_action_t interpreted;
static esp_err_t transcribe_result, interpret_result;
static int transcribe_http, interpret_http;
static bool cancel_in_interpret, cancel_in_apply_time, cancel_after_commit;
static unsigned voice_log_count, minimum_heap_reads;
static char voice_log[512];
static const char private_title[] = "PRIVATE_TITLE_fixture_7129";
static const char private_transcript[] = "PRIVATE_TRANSCRIPT_fixture_8427";
static const char private_chat_key[] = "PRIVATE_CHAT_KEY_fixture_1267";
static const char private_asr_key[] = "PRIVATE_ASR_KEY_fixture_9483";
static const char private_endpoint[] = "https://PRIVATE_ENDPOINT_fixture.example.com/v1/chat/completions";
static const char voice_log_format[] =
    "voice phase=%u committed=%u http=%d system=0x%x cancelled=%u "
    "heap_internal=%u heap_min_internal=%u largest_internal=%u stack_free=%u";

static time_t fixture_time(time_t *out) {
    /* At this point Chat has returned but smart_todo_apply's time argument is
     * still being evaluated; cancellation must be checked again before save. */
    if (cancel_in_apply_time && interprets && !saves) atomic_store(&s_voice_cancelled, true);
    if (out) *out = utc;
    return utc;
}
int64_t esp_timer_get_time(void) { return mono; }
void fixture_log(const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "vibe_controller"));
    char rendered[1024];
    va_list args; va_start(args, format);
    int count = vsnprintf(rendered, sizeof(rendered), format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(rendered));
    /* Check every actual formatted controller log, including ordinary state
     * logs emitted while voice_recording updates the display. */
    assert(!strstr(rendered, private_title) && !strstr(rendered, private_transcript));
    assert(!strstr(rendered, private_chat_key) && !strstr(rendered, private_asr_key));
    assert(!strstr(rendered, private_endpoint));
    if (!strncmp(format, "voice ", 6)) {
        assert(!strcmp(format, voice_log_format));
        assert((size_t)count < sizeof(voice_log)); strcpy(voice_log, rendered); ++voice_log_count;
    }
}
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 65536; }
size_t heap_caps_get_minimum_free_size(unsigned caps) { assert(caps == MALLOC_CAP_INTERNAL); ++minimum_heap_reads; return 12345; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 32768; }
uint32_t esp_random(void) { return 42; }
const esp_app_desc_t *esp_app_get_description(void) { static const esp_app_desc_t desc = {.version = "fixture"}; return &desc; }
void esp_restart(void) { assert(!"Unexpected restart"); }
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_netif_sntp_sync_wait(TickType_t ticks) { (void)ticks; return ESP_OK; }
void esp_netif_sntp_deinit(void) {}
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size) { assert(count == APP_QUEUE_DEPTH && size == sizeof(command_t)); return (void *)1; }
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t ticks) {
    (void)ticks; assert(queue == (void *)1);
    if (queue_full) return pdFALSE;
    assert(queue_count < 32); queued[queue_count++] = *(const command_t *)data; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t ticks) {
    (void)ticks; assert(queue == (void *)1);
    if (!queue_count) return pdFALSE;
    *(command_t *)data = queued[0]; --queue_count;
    memmove(queued, queued + 1, queue_count * sizeof(*queued)); return pdTRUE;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) { (void)ticks; assert(semaphore == (void *)1); return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) { assert(semaphore == (void *)1); return pdTRUE; }
EventGroupHandle_t xEventGroupCreate(void) { return (void *)1; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) { assert(group == (void *)1); event_bits |= bits; return event_bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) { assert(group == (void *)1); event_bits &= ~bits; return event_bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear, BaseType_t all, TickType_t wait) {
    assert(group == (void *)1); (void)bits; (void)clear; (void)all; (void)wait; return event_bits;
}
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *task) {
    (void)entry; (void)name; (void)stack; (void)arg; (void)priority; *task = (void *)1; return pdPASS;
}
void vTaskDelay(TickType_t ticks) { mono += (int64_t)ticks * 1000; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 8192; }

esp_err_t wifi_adapter_init(const wifi_adapter_config_t *config) { (void)config; return ESP_OK; }
bool wifi_adapter_has_credentials(void) { return wifi_credentials; }
bool wifi_adapter_is_connected(void) { return wifi_connected; }
bool wifi_adapter_is_provisioning(void) { return wifi_provisioning; }
esp_err_t wifi_adapter_start_station(void) { ++station_starts; return ESP_OK; }
void wifi_adapter_stop_station(void) { ++station_stops; wifi_connected = false; }
esp_err_t wifi_adapter_start_provisioning(void) { ++provisioning_starts; wifi_provisioning = true; return ESP_OK; }
void wifi_adapter_stop_provisioning(void) { ++provisioning_stops; wifi_provisioning = false; }
void wifi_adapter_set_language(const char *language) { (void)language; }
const char *wifi_adapter_ap_ssid(void) { return "VibeFixture-1234"; }
const char *wifi_adapter_portal_url(void) { return "http://192.168.4.1"; }
esp_err_t wifi_adapter_clear_credentials(void) { wifi_credentials = false; return ESP_OK; }
esp_err_t llm_portal_start(void) { ++portal_starts; portal_running = true; return ESP_OK; }
void llm_portal_stop(void) { ++portal_stops; portal_running = false; }
bool llm_portal_is_running(void) { return portal_running; }
const char *llm_portal_url(void) { return "http://192.168.1.20/?token=fixture"; }
esp_err_t llm_settings_load(llm_settings_t *settings) { *settings = loaded_llm; return load_result; }
bool llm_settings_valid(const llm_settings_t *settings) { return settings->chat_model[0] != 0; }
esp_err_t llm_settings_clear(void) { memset(&loaded_llm, 0, sizeof(loaded_llm)); return ESP_OK; }
esp_err_t board_nfc_last_error(void) { return ESP_OK; }
void board_nfc_stop(void) { ++nfc_stops; }
esp_err_t board_nfc_set_wifi(const char *ssid, const char *password, const char *url) { (void)ssid; assert(!password[0]); (void)url; return ESP_OK; }
esp_err_t board_audio_beep(void) { ++beeps; return audio_result; }
esp_err_t board_audio_reminder(void) { ++reminder_chimes; return audio_result; }
esp_err_t board_audio_set_alert_volume(uint8_t volume) {
    assert(volume <= 100); applied_alert_volume = volume; ++alert_volume_sets; return ESP_OK;
}
bool board_ok_is_pressed(void) { return physical_ok_pressed; }
esp_err_t smart_todo_load(smart_todo_list_t *out) { *out = persisted; return ESP_OK; }
esp_err_t smart_todo_save(const smart_todo_list_t *next) {
    ++saves;
    if (save_result == ESP_OK) {
        persisted = *next;
        if (cancel_after_commit) atomic_store(&s_voice_cancelled, true);
    }
    return save_result;
}
esp_err_t smart_todo_clear(void) { smart_todo_init(&persisted); return ESP_OK; }
esp_err_t smart_todo_transcribe(const llm_settings_t *settings, const smart_todo_voice_callbacks_t *cb, char *text, size_t capacity, int *status) {
    assert(!strcmp(settings->api_key, private_chat_key) && !strcmp(settings->asr_key, private_asr_key));
    ++transcribes; assert(capacity > sizeof(private_transcript) && cb->held(cb->context));
    cb->recording(true, cb->context); cb->recording(false, cb->context);
    if (transcribe_result == ESP_OK) strcpy(text, private_transcript);
    *status = transcribe_http; return transcribe_result;
}
esp_err_t smart_todo_interpret(const llm_settings_t *settings, const smart_todo_list_t *items, const char *text,
    const smart_todo_voice_callbacks_t *cb, smart_todo_action_t *action, int *status) {
    (void)settings; (void)items; (void)cb; assert(!strcmp(text, private_transcript));
    ++interprets;
    if (cancel_in_interpret) atomic_store(&s_voice_cancelled, true);
    if (interpret_result == ESP_OK) *action = interpreted;
    *status = interpret_http; return interpret_result;
}
esp_err_t device_config_save(device_config_t *config) { (void)config; return ESP_OK; }
esp_err_t device_config_save_language(vibe_language_t language) { (void)language; return ESP_OK; }
esp_err_t device_config_save_alert_volume(uint8_t volume) {
    (void)volume; return alert_save_result;
}
vibe_language_t vibe_language_next(vibe_language_t language) { return (vibe_language_t)((language + 1) % VIBE_LANG_COUNT); }
const char *vibe_language_code(vibe_language_t language) { (void)language; return "en-US"; }
const char *vibe_timezone_posix(vibe_timezone_t timezone) { (void)timezone; return "UTC0"; }
const char *vibe_error_name(vibe_error_t error) { (void)error; return "fixture"; }
esp_err_t vibe_init(const vibe_config_t *config) { (void)config; return ESP_OK; }
esp_err_t vibe_request_device_code(vibe_device_code_t *out) { memset(out, 0, sizeof(*out)); return ESP_FAIL; }
esp_err_t vibe_poll_device_code(const char *code, vibe_auth_result_t *out) { (void)code; memset(out, 0, sizeof(*out)); return ESP_FAIL; }
esp_err_t vibe_fetch_today(vibe_usage_snapshot_t *out) { (void)out; return ESP_FAIL; }
esp_err_t vibe_begin_reconcile(bool today) { (void)today; return ESP_OK; }
esp_err_t vibe_reconcile(vibe_usage_snapshot_t *out) { (void)out; return ESP_FAIL; }
bool vibe_reconcile_pending(void) { return false; }
esp_err_t vibe_snapshot(vibe_window_t window, vibe_usage_snapshot_t *out) { (void)window; memset(out, 0, sizeof(*out)); return ESP_OK; }
esp_err_t vibe_unlink(void) { return ESP_OK; }
esp_err_t vibe_factory_reset(void) { return ESP_OK; }
esp_err_t vibe_factory_reset_pending(bool *pending) { *pending = false; return ESP_OK; }
esp_err_t vibe_factory_reset_resume_local(void) { return ESP_OK; }
esp_err_t vibe_factory_reset_complete(void) { return ESP_OK; }
void vibe_cancel(void) { ++cancels; }
bool vibe_has_auth(void) { return true; }
uint32_t vibe_account_generation(void) { return 1; }
vibe_error_t vibe_last_error(void) { return VIBE_OK; }
int vibe_last_http_status(void) { return 0; }
uint32_t vibe_last_retry_after_seconds(void) { return 0; }
uint32_t vibe_retry_remaining_seconds(void) { return 0; }

static void reset_fixture(void) {
    memset(&s_controller, 0, sizeof(s_controller));
    s_controller.initialized = s_controller.started = true;
    s_controller.queue = s_controller.view_mutex = s_controller.events = (void *)1;
    s_controller.online = true; s_controller.view.lifecycle = VIBE_APP_DASHBOARD;
    s_controller.config.settings.refresh_seconds = 300;
    s_controller.config.settings.alert_volume = DEVICE_ALERT_VOLUME_DEFAULT;
    s_controller.view.alert_volume = DEVICE_ALERT_VOLUME_DEFAULT;
    smart_todo_init(&s_todos); smart_todo_init(&persisted);
    s_todo_storage_ok = true; s_llm_deadline_us = s_reminder_retry_us = 0;
    atomic_store(&s_voice_held, false); atomic_store(&s_voice_pending, false); atomic_store(&s_voice_cancelled, false);
    utc = 1760000000; mono = 1000000; event_bits = 0; queue_count = 0; queue_full = false;
    wifi_connected = wifi_credentials = true; wifi_provisioning = portal_running = false;
    physical_ok_pressed = true;
    station_starts = station_stops = provisioning_starts = provisioning_stops = 0;
    portal_starts = portal_stops = nfc_stops = beeps = reminder_chimes = 0;
    transcribes = interprets = saves = cancels = 0;
    alert_volume_sets = 0; applied_alert_volume = 0;
    save_result = load_result = audio_result = alert_save_result = ESP_OK;
    memset(&loaded_llm, 0, sizeof(loaded_llm)); loaded_llm.enabled = true; strcpy(loaded_llm.chat_model, "fixture");
    strcpy(loaded_llm.api_key, private_chat_key); strcpy(loaded_llm.asr_key, private_asr_key);
    strcpy(loaded_llm.chat_url, private_endpoint);
    memset(&interpreted, 0, sizeof(interpreted)); interpreted.kind = SMART_TODO_ADD; strcpy(interpreted.title, private_title);
    transcribe_result = interpret_result = ESP_OK; transcribe_http = interpret_http = 200;
    cancel_in_interpret = cancel_in_apply_time = cancel_after_commit = false;
    voice_log_count = minimum_heap_reads = 0; voice_log[0] = '\0';
}
static void queue_voice(void) {
    assert(app_controller_dispatch(APP_INTENT_TODO_VOICE_START) == ESP_OK);
    assert(queue_count == 1 && queued[0].value.intent == APP_INTENT_TODO_VOICE_START);
    queue_count = 0; handle_intent(APP_INTENT_TODO_VOICE_START);
}
static void expect_voice_log(unsigned phase, unsigned committed, int http, esp_err_t system, unsigned cancelled) {
    unsigned actual_phase, actual_committed, actual_system, actual_cancelled;
    unsigned heap, minimum_heap, largest_heap, stack;
    int actual_http, end = -1;
    assert(voice_log_count == 1 && minimum_heap_reads == 1);
    int count = sscanf(voice_log,
        "voice phase=%u committed=%u http=%d system=0x%x cancelled=%u "
        "heap_internal=%u heap_min_internal=%u largest_internal=%u stack_free=%u%n",
        &actual_phase, &actual_committed, &actual_http, &actual_system, &actual_cancelled,
        &heap, &minimum_heap, &largest_heap, &stack, &end);
    assert(count == 9 && end >= 0 && voice_log[end] == '\0');
    assert(actual_phase == phase && actual_committed == committed && actual_http == http);
    assert(actual_system == (unsigned)system && actual_cancelled == cancelled);
    assert(heap == 65536 && minimum_heap == 12345 && largest_heap == 32768 && stack == 8192);
}
int main(void) {
    reset_fixture();
    const uint8_t expected_volumes[] = {75, 100, 0, 25, 50};
    const unsigned expected_beeps[] = {1, 2, 2, 3, 4};
    for (size_t i = 0; i < sizeof(expected_volumes); ++i) {
        handle_intent(APP_INTENT_CYCLE_ALERT_VOLUME);
        assert(s_controller.view.alert_volume == expected_volumes[i] &&
               s_controller.config.settings.alert_volume == expected_volumes[i]);
        assert(applied_alert_volume == expected_volumes[i] && alert_volume_sets == i + 1 &&
               beeps == expected_beeps[i]);
    }
    alert_save_result = ESP_FAIL;
    handle_intent(APP_INTENT_CYCLE_ALERT_VOLUME);
    assert(s_controller.view.alert_volume == 50 && alert_volume_sets == 5 && beeps == 4);
    assert(!strcmp(s_controller.view.detail, "Could not save alert volume"));
    reset_fixture(); s_controller.view.llm_config_open = true; portal_running = true;
    handle_shutdown();
    assert(s_controller.shutting_down && !s_controller.online && !portal_running);
    assert(s_controller.view.lifecycle == VIBE_APP_PREPARE_SLEEP && (event_bits & APP_SHUTDOWN_READY_BIT));
    unsigned stopped = nfc_stops;
    const wifi_adapter_event_t events[] = {WIFI_ADAPTER_PORTAL_EXIT, WIFI_ADAPTER_GOT_IP, WIFI_ADAPTER_PORTAL_ENTER, WIFI_ADAPTER_DISCONNECTED};
    for (size_t i = 0; i < sizeof(events) / sizeof(*events); ++i) handle_wifi_event(events[i]);
    handle_intent(APP_INTENT_LLM_CONFIG); handle_intent(APP_INTENT_TODO_VOICE_START); run_due_actions();
    assert(!station_starts && !provisioning_starts && !portal_starts && !transcribes && nfc_stops == stopped);
    assert(s_controller.view.lifecycle == VIBE_APP_PREPARE_SLEEP);
    assert(app_controller_dispatch(APP_INTENT_LLM_CONFIG) == ESP_ERR_INVALID_STATE);
    assert(app_controller_dispatch(APP_INTENT_TODO_VOICE_START) == ESP_ERR_INVALID_STATE);
    assert(!atomic_load(&s_voice_held) && !atomic_load(&s_voice_pending));
    reset_fixture(); atomic_store(&s_voice_held, true); atomic_store(&s_voice_pending, true);
    wifi_event_callback(WIFI_ADAPTER_DISCONNECTED, NULL);
    assert(!atomic_load(&s_voice_cancelled) && atomic_load(&s_voice_held));
    assert(queue_count == 1 && cancels == 1); /* Before queued event can execute. */
    reset_fixture(); s_controller.view.llm_config_open = true; s_controller.view.wifi_provisioning = true;
    wifi_provisioning = true; portal_running = true;
    handle_intent(APP_INTENT_LLM_CONFIG_CLOSE);
    assert(!wifi_provisioning && !portal_running && provisioning_stops == 1);
    assert(!s_controller.view.llm_config_open && !s_controller.view.wifi_provisioning && !s_controller.view.llm_portal_url[0]);
    reset_fixture(); save_result = ESP_FAIL;
    smart_todo_list_t before = s_todos; queue_voice();
    assert(transcribes == 1 && interprets == 1 && saves == 1 && beeps == 0);
    assert(!memcmp(&s_todos, &before, sizeof(before)) && s_controller.view.todo_count == 0);
    assert(!strcmp(s_controller.view.todo_status, "Could not save TODO"));
    assert(!atomic_load(&s_voice_pending) && !atomic_load(&s_voice_held));
    save_result = ESP_OK; queue_voice();
    assert(s_todos.count == 1 && persisted.count == 1 && s_controller.view.todo_count == 1 && beeps == 1);
    reset_fixture(); interpreted.delay_seconds = 60; interpreted.repeat_seconds = 60;
    assert(smart_todo_apply(&s_todos, &interpreted, utc - 61) == ESP_OK); persisted = s_todos;
    s_controller.online = false; smart_reminders();
    assert(beeps == 0 && reminder_chimes == 1 && saves == 1 &&
           s_controller.view.todo_alert_sequence == 1);
    uint32_t id = s_controller.view.todo_alert_id; assert(id != 0);
    utc += 60; mono += 60000000; smart_reminders();
    assert(beeps == 0 && reminder_chimes == 2 &&
           s_controller.view.todo_alert_sequence == 2 && s_controller.view.todo_alert_id == id);
    before = s_todos; save_result = ESP_FAIL; utc += 60; mono += 60000000; smart_reminders();
    assert(!memcmp(&s_todos, &before, sizeof(before)) && s_controller.view.todo_alert_sequence == 3);
    assert(!strcmp(s_controller.view.todo_status, "Reminder save failed"));
    reset_fixture(); queue_full = true;
    assert(app_controller_dispatch(APP_INTENT_TODO_VOICE_START) == ESP_ERR_TIMEOUT);
    assert(!atomic_load(&s_voice_pending) && !atomic_load(&s_voice_held));
    reset_fixture(); s_todo_storage_ok = false; queue_voice(); assert(!transcribes && !saves);
    reset_fixture(); physical_ok_pressed = false; queue_voice();
    assert(!transcribes && !interprets && !saves);
    assert(!atomic_load(&s_voice_held) && !atomic_load(&s_voice_pending));
    reset_fixture(); transcribe_result = ESP_ERR_INVALID_RESPONSE; transcribe_http = 401;
    queue_voice(); expect_voice_log(1, 0, 401, ESP_ERR_INVALID_RESPONSE, 0);
    assert(transcribes == 1 && !interprets && !saves && !s_todos.count);
    reset_fixture(); s_controller.online = false; wifi_connected = false;
    queue_voice(); expect_voice_log(1, 0, 200, ESP_ERR_INVALID_STATE, 0);
    assert(transcribes == 1 && !interprets && !saves);
    assert(!strcmp(s_controller.view.todo_status, "Connect Wi-Fi first"));
    reset_fixture(); queue_voice(); expect_voice_log(4, 1, 200, ESP_OK, 0);
    assert(s_todos.count == 1 && !strcmp(s_todos.items[0].title, private_title));
    reset_fixture(); interpret_result = ESP_ERR_INVALID_RESPONSE; interpret_http = 429;
    queue_voice(); expect_voice_log(2, 0, 429, ESP_ERR_INVALID_RESPONSE, 0);
    assert(transcribes == 1 && interprets == 1 && !saves);
    reset_fixture(); save_result = ESP_FAIL;
    queue_voice(); expect_voice_log(4, 0, 200, ESP_FAIL, 0);
    assert(saves == 1 && !s_todos.count);
    reset_fixture(); interpreted.kind = SMART_TODO_DELETE; interpreted.title[0] = '\0'; interpreted.id = 99;
    queue_voice(); expect_voice_log(3, 0, 200, ESP_ERR_NOT_FOUND, 0);
    assert(!saves);
    reset_fixture(); cancel_in_interpret = true;
    queue_voice(); expect_voice_log(2, 0, 200, ESP_ERR_INVALID_STATE, 1);
    assert(!saves && !s_todos.count && !persisted.count && !strcmp(s_controller.view.todo_status, "Voice cancelled"));
    reset_fixture(); cancel_in_apply_time = true;
    queue_voice(); expect_voice_log(4, 0, 200, ESP_ERR_INVALID_STATE, 1);
    assert(!saves && !s_todos.count && !persisted.count);
    reset_fixture(); cancel_after_commit = true;
    queue_voice(); expect_voice_log(4, 1, 200, ESP_OK, 1);
    assert(saves == 1 && s_todos.count == 1 && persisted.count == 1);
    assert(!strcmp(s_todos.items[0].title, private_title));
    assert(!strcmp(s_controller.view.todo_status, "TODO added"));
    assert(!atomic_load(&s_voice_pending) && !atomic_load(&s_voice_held));
    reset_fixture(); loaded_llm.enabled = false;
    queue_voice(); expect_voice_log(0, 0, 0, ESP_ERR_INVALID_STATE, 0);
    assert(!transcribes && !interprets && !saves);
    puts("Controller smart lifecycle: shutdown stale events/intents, offline local voice capture, AP close, repeated reminder sequence and failed-save isolation: PASS");
    puts("Controller voice telemetry: exact stage/status/commit fields, genuine heap-low-water API, no title/transcript/key/endpoint in formatted logs: PASS");
}
