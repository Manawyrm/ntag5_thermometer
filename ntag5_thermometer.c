#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <notification/notification_messages.h>

#include "body_temp_estimator.h"
#include "ntag5_sensor.h"

typedef enum {
    ThermometerViewLocation,
    ThermometerViewReading,
} ThermometerView;

typedef struct {
    BodyTempLocation location;
    Ntag5SensorStatus status;
    bool has_value; /**< temperature holds the last good reading */
    float temperature; /**< Skin temperature */
} ThermometerReadingModel;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    Submenu* location_menu;
    View* reading_view;
    Ntag5Sensor* sensor;
} ThermometerApp;

static void thermometer_draw_centered(Canvas* canvas, const char* first, const char* second) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, first);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, second);
}

/* Short note under a held value: why it is not current */
static const char* thermometer_status_note(Ntag5SensorStatus status) {
    switch(status) {
    case Ntag5SensorStatusWaiting:
        return "Last value, tag removed";
    case Ntag5SensorStatusNotSensor:
        return "Last value, not a patch";
    case Ntag5SensorStatusNoPower:
        return "Last value, sensor off";
    case Ntag5SensorStatusNoValue:
        return "Last value, no measurement";
    default:
        return NULL;
    }
}

static void thermometer_draw_temperature(Canvas* canvas, const ThermometerReadingModel* model) {
    char text[32];

    const float estimate = body_temp_estimate(model->temperature, model->location);

    snprintf(text, sizeof(text), "%.2f", (double)estimate);
    canvas_set_font(canvas, FontBigNumbers);
    const uint16_t width = canvas_string_width(canvas, text);
    const uint16_t x = (128 - width - 12) / 2;
    canvas_draw_str(canvas, x, 44, text);

    // The fonts have no degree sign
    canvas_draw_circle(canvas, x + width + 4, 26, 2);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x + width + 7, 36, "C");

    const char* note = thermometer_status_note(model->status);
    if(note == NULL) {
        snprintf(text, sizeof(text), "Sensor %.2f C", (double)model->temperature);
        note = text;
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, note);
}

static void thermometer_reading_draw_callback(Canvas* canvas, void* model_pointer) {
    const ThermometerReadingModel* model = model_pointer;
    char title[32];

    canvas_clear(canvas);

    snprintf(title, sizeof(title), "Patch: %s", body_temp_location_name(model->location));
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, title);

    if(model->has_value) {
        thermometer_draw_temperature(canvas, model);
        return;
    }

    switch(model->status) {
    case Ntag5SensorStatusWaiting:
    case Ntag5SensorStatusOk:
        thermometer_draw_centered(canvas, "Hold the patch to", "the back of the Flipper");
        break;
    case Ntag5SensorStatusNotSensor:
        thermometer_draw_centered(canvas, "This is not a", "temperature patch");
        break;
    case Ntag5SensorStatusNoPower:
        thermometer_draw_centered(canvas, "Sensor not powered:", "energy harvesting is off");
        break;
    case Ntag5SensorStatusNoValue:
        thermometer_draw_centered(canvas, "The sensor returned", "no measurement");
        break;
    }
}

/* NFC worker thread */
static void thermometer_sensor_callback(const Ntag5SensorReading* reading, void* context) {
    ThermometerApp* app = context;

    with_view_model(
        app->reading_view,
        ThermometerReadingModel * model,
        {
            model->status = reading->status;
            if(reading->status == Ntag5SensorStatusOk) {
                model->has_value = true;
                model->temperature = reading->temperature;
            }
        },
        true);

    if(reading->status == Ntag5SensorStatusOk) {
        notification_message(app->notifications, &sequence_blink_green_10);
    }
}

static void thermometer_reading_enter_callback(void* context) {
    ThermometerApp* app = context;
    ntag5_sensor_start(app->sensor, thermometer_sensor_callback, app);
}

static void thermometer_reading_exit_callback(void* context) {
    ThermometerApp* app = context;
    ntag5_sensor_stop(app->sensor);
}

static uint32_t thermometer_reading_previous_callback(void* context) {
    UNUSED(context);
    return ThermometerViewLocation;
}

static void thermometer_location_callback(void* context, uint32_t index) {
    ThermometerApp* app = context;

    with_view_model(
        app->reading_view,
        ThermometerReadingModel * model,
        {
            model->location = (BodyTempLocation)index;
            model->has_value = false;
        },
        false);

    view_dispatcher_switch_to_view(app->view_dispatcher, ThermometerViewReading);
}

static bool thermometer_navigation_callback(void* context) {
    ThermometerApp* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static ThermometerApp* thermometer_app_alloc(void) {
    ThermometerApp* app = malloc(sizeof(ThermometerApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->sensor = ntag5_sensor_alloc();

    app->location_menu = submenu_alloc();
    submenu_set_header(app->location_menu, "Where is the patch?");
    for(uint32_t location = 0; location < BodyTempLocationCount; location++) {
        submenu_add_item(
            app->location_menu,
            body_temp_location_name(location),
            location,
            thermometer_location_callback,
            app);
    }

    app->reading_view = view_alloc();
    view_allocate_model(app->reading_view, ViewModelTypeLocking, sizeof(ThermometerReadingModel));
    view_set_context(app->reading_view, app);
    view_set_draw_callback(app->reading_view, thermometer_reading_draw_callback);
    view_set_enter_callback(app->reading_view, thermometer_reading_enter_callback);
    view_set_exit_callback(app->reading_view, thermometer_reading_exit_callback);
    view_set_previous_callback(app->reading_view, thermometer_reading_previous_callback);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(
        app->view_dispatcher, ThermometerViewLocation, submenu_get_view(app->location_menu));
    view_dispatcher_add_view(app->view_dispatcher, ThermometerViewReading, app->reading_view);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, thermometer_navigation_callback);

    return app;
}

static void thermometer_app_free(ThermometerApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, ThermometerViewLocation);
    view_dispatcher_remove_view(app->view_dispatcher, ThermometerViewReading);
    view_dispatcher_free(app->view_dispatcher);

    view_free(app->reading_view);
    submenu_free(app->location_menu);
    ntag5_sensor_free(app->sensor);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t ntag5_thermometer_app(void* p) {
    UNUSED(p);

    ThermometerApp* app = thermometer_app_alloc();

    view_dispatcher_switch_to_view(app->view_dispatcher, ThermometerViewLocation);
    view_dispatcher_run(app->view_dispatcher);

    thermometer_app_free(app);

    return 0;
}
