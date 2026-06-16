#include <stdio.h>
#include <string.h>

#include "app_core.h"
#include "app_math.h"
#include "data_table.h"
#include "monitor_module.h"
#include "sim_module.h"

static int g_failures;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL: %s\n", message);
        g_failures++;
    }
}

static const monitor_region_t *find_region(const monitor_output_t *output,
                                           spray_content_t content,
                                           disease_type_t disease)
{
    uint16_t i;

    for (i = 0U; i < output->region_count; i++) {
        if ((output->region[i].spray_content == content) &&
            (output->region[i].disease_type == disease)) {
            return &output->region[i];
        }
    }

    return 0;
}

static app_result_t set_manual_plot(uint16_t plot_id, plot_state_t state, disease_type_t disease)
{
    monitor_input_t input;

    monitor_module_fill_default_input(&input);
    input.grid.rows = 10U;
    input.grid.cols = 10U;
    input.mode = MONITOR_MODE_MANUAL_SET;
    input.environment = g_data_default_environment;
    input.manual_plot_id = plot_id;
    input.manual_state = state;
    input.manual_disease_type = disease;

    return app_core_apply_manual_monitor(&input);
}

static void setup_single_water_sim(void)
{
    monitor_input_t monitor_input;
    mix_input_t mix_input;

    app_core_init();
    monitor_module_fill_default_input(&monitor_input);
    monitor_input.grid.rows = 1U;
    monitor_input.grid.cols = 1U;
    monitor_input.mode = MONITOR_MODE_MANUAL_SET;
    monitor_input.environment = g_data_default_environment;
    monitor_input.manual_plot_id = 1U;
    monitor_input.manual_state = PLOT_STATE_WATER_DEFICIT;
    monitor_input.manual_disease_type = DISEASE_TYPE_NONE;

    expect_true(app_core_run_monitor(&monitor_input) == APP_OK, "monitor manual water plot should succeed");
    mix_input.tank_capacity_ml_x10 = g_data_default_drone.tank_capacity_ml_x10;
    expect_true(app_core_run_mix_with_input(&mix_input) == APP_OK, "mix should succeed");
    expect_true(app_core_run_plan() == APP_OK, "plan should succeed");
    expect_true(app_core_start_sim() == APP_OK, "sim should start");
}

static void test_default_environment_is_suitable(void)
{
    monitor_input_t input;
    app_result_t result;
    const monitor_output_t *output;

    app_core_init();
    monitor_module_fill_default_input(&input);
    input.mode = MONITOR_MODE_INIT_ONLY;
    input.environment = g_data_default_environment;

    result = app_core_run_monitor(&input);
    expect_true(result == APP_OK, "monitor should accept default environment");

    output = app_core_get_monitor_output();
    expect_true(output->environment_status.temperature_ok != 0U, "default temperature should be suitable");
    expect_true(output->environment_status.humidity_ok != 0U, "default humidity should be suitable");
    expect_true(output->environment_status.light_ok != 0U, "default light should be suitable");
    expect_true(output->environment_status.wind_ok != 0U, "default wind should be suitable");
    expect_true(output->environment_status.suitable != 0U, "default environment should be suitable");
    expect_true(output->environment_status.environment_factor_x100 == 100U, "default environment factor should be 100%");
}

static void test_crop_stage_changes_environment_limits(void)
{
    monitor_input_t input;
    app_result_t result;
    const monitor_output_t *output;

    app_core_init();
    monitor_module_fill_default_input(&input);
    input.mode = MONITOR_MODE_INIT_ONLY;
    input.crop_type = 2U;
    input.growth_stage = 1U;
    input.environment = g_data_default_environment;
    input.environment.temperature_x10 = 310U;

    result = app_core_run_monitor(&input);
    expect_true(result == APP_OK, "monitor should accept selected crop rule");

    output = app_core_get_monitor_output();
    expect_true(output->crop_type == 2U, "monitor output should remember crop type");
    expect_true(output->growth_stage == 1U, "monitor output should remember growth stage");
    expect_true(output->environment_status.temperature_ok == 0U,
                "crop 2 stage 1 should warn at 31.0C");
    expect_true(output->environment_status.suitable == 0U,
                "crop-specific temperature warning should mark environment unsuitable");
}

static void test_region_severity_tracks_ratio_and_environment(void)
{
    monitor_input_t input;
    mix_input_t mix_input;
    app_result_t result;
    uint16_t i;
    uint32_t base_liquid;
    uint32_t stressed_liquid;
    const monitor_region_t *region;
    const monitor_output_t *monitor_output;
    const mix_output_t *mix_output;

    app_core_init();
    monitor_module_fill_default_input(&input);
    input.grid.rows = 10U;
    input.grid.cols = 10U;
    input.mode = MONITOR_MODE_INIT_ONLY;
    input.environment = g_data_default_environment;
    result = app_core_run_monitor(&input);
    expect_true(result == APP_OK, "baseline monitor init should succeed");

    for (i = 1U; i <= 5U; i++) {
        result = set_manual_plot(i, PLOT_STATE_DISEASE, DISEASE_TYPE_BLIGHT);
        expect_true(result == APP_OK, "manual disease setup should succeed");
    }
    monitor_output = app_core_get_monitor_output();
    region = find_region(monitor_output, SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT);
    expect_true(region != 0, "light disease region should exist");
    expect_true(region != 0 && region->severity_x100 == 80U, "5% disease should be light severity");

    for (i = 6U; i <= 12U; i++) {
        result = set_manual_plot(i, PLOT_STATE_DISEASE, DISEASE_TYPE_BLIGHT);
        expect_true(result == APP_OK, "manual disease setup should succeed");
    }
    region = find_region(app_core_get_monitor_output(), SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT);
    expect_true(region != 0 && region->severity_x100 == 100U, "12% disease should be medium severity");

    for (i = 13U; i <= 20U; i++) {
        result = set_manual_plot(i, PLOT_STATE_DISEASE, DISEASE_TYPE_BLIGHT);
        expect_true(result == APP_OK, "manual disease setup should succeed");
    }
    region = find_region(app_core_get_monitor_output(), SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT);
    expect_true(region != 0 && region->severity_x100 == 120U, "20% disease should be heavy severity");

    mix_input.tank_capacity_ml_x10 = g_data_default_drone.tank_capacity_ml_x10;
    result = app_core_run_mix_with_input(&mix_input);
    expect_true(result == APP_OK, "baseline mix should succeed");
    mix_output = app_core_get_mix_output();
    expect_true(mix_output->main_batch_count > 0U &&
                mix_output->main_batch[0].severity_x100 == 120U,
                "mix main batch should expose baseline severity");
    base_liquid = mix_output->summary.total_liquid_ml_x10;

    app_core_init();
    monitor_module_fill_default_input(&input);
    input.grid.rows = 10U;
    input.grid.cols = 10U;
    input.mode = MONITOR_MODE_INIT_ONLY;
    input.environment = g_data_default_environment;
    input.environment.temperature_x10 = 360U;
    input.environment.humidity_x10 = 300U;
    input.environment.light_lux = 70000UL;
    result = app_core_run_monitor(&input);
    expect_true(result == APP_OK, "stressed monitor init should succeed");

    for (i = 1U; i <= 20U; i++) {
        result = set_manual_plot(i, PLOT_STATE_DISEASE, DISEASE_TYPE_BLIGHT);
        expect_true(result == APP_OK, "manual disease setup should succeed");
    }
    monitor_output = app_core_get_monitor_output();
    expect_true(monitor_output->environment_status.suitable == 0U, "stressed environment should warn");
    expect_true(monitor_output->environment_status.environment_factor_x100 == 120U, "stressed environment factor should cap at 120%");
    region = find_region(monitor_output, SPRAY_CONTENT_PESTICIDE, DISEASE_TYPE_BLIGHT);
    expect_true(region != 0 && region->severity_x100 == 140U, "environment-adjusted severity should cap at 140%");

    result = app_core_run_mix_with_input(&mix_input);
    expect_true(result == APP_OK, "stressed mix should succeed");
    mix_output = app_core_get_mix_output();
    expect_true(mix_output->main_batch_count > 0U &&
                mix_output->main_batch[0].severity_x100 == 140U,
                "mix main batch should expose environment-adjusted severity");
    stressed_liquid = mix_output->summary.total_liquid_ml_x10;
    expect_true(stressed_liquid > base_liquid, "environment stress should increase mixed liquid");
}

static void test_manual_recall_refills_and_auto_resumes(void)
{
    monitor_input_t monitor_input;
    mix_input_t mix_input;
    app_result_t result;
    unsigned int guard;
    uint8_t saw_refilling;
    uint8_t saw_returning_to_breakpoint;

    app_core_init();
    monitor_module_fill_default_input(&monitor_input);
    monitor_input.grid.rows = 1U;
    monitor_input.grid.cols = 1U;
    monitor_input.mode = MONITOR_MODE_MANUAL_SET;
    monitor_input.environment = g_data_default_environment;
    monitor_input.manual_plot_id = 1U;
    monitor_input.manual_state = PLOT_STATE_WATER_DEFICIT;
    monitor_input.manual_disease_type = DISEASE_TYPE_NONE;

    result = app_core_run_monitor(&monitor_input);
    expect_true(result == APP_OK, "monitor manual water plot should succeed");
    mix_input.tank_capacity_ml_x10 = g_data_default_drone.tank_capacity_ml_x10;
    expect_true(app_core_run_mix_with_input(&mix_input) == APP_OK, "mix should succeed");
    expect_true(app_core_run_plan() == APP_OK, "plan should succeed");
    expect_true(app_core_start_sim() == APP_OK, "sim should start");

    for (guard = 0U; guard < 5U; guard++) {
        app_core_tick_10ms();
    }
    result = app_core_trigger_recall();
    expect_true(result == APP_OK, "manual recall should start return-to-home");

    saw_refilling = 0U;
    saw_returning_to_breakpoint = 0U;
    for (guard = 0U; guard < 20000U; guard++) {
        const sim_output_t *sim_output = app_core_get_sim_output();

        if (sim_output->state == SIM_STATE_REFILLING) {
            saw_refilling = 1U;
        }
        if (sim_output->state == SIM_STATE_RETURNING_TO_BREAKPOINT) {
            saw_returning_to_breakpoint = 1U;
        }
        if ((saw_refilling != 0U) &&
            (saw_returning_to_breakpoint != 0U) &&
            (sim_output->state == SIM_STATE_SPRAYING)) {
            break;
        }

        app_core_tick_10ms();
    }

    expect_true(saw_refilling != 0U, "recall should reach refill state");
    expect_true(saw_returning_to_breakpoint != 0U, "refill should return to breakpoint");
    expect_true(app_core_get_sim_output()->state == SIM_STATE_SPRAYING, "sim should auto resume spraying");
    expect_true(app_core_get_sim_output()->refill_count == 1U, "sim report should count refill events");
}

static void test_stop_then_start_resumes_from_paused_position(void)
{
    app_point_t paused_position;
    uint32_t paused_distance;
    unsigned int guard;

    setup_single_water_sim();
    for (guard = 0U; guard < 50U; guard++) {
        app_core_tick_10ms();
    }

    paused_position = app_core_get_sim_output()->current_position;
    paused_distance = app_core_get_sim_output()->route_distance_done_mm;
    expect_true(paused_distance > 0U, "sim should move before pause");
    expect_true(app_core_stop_sim() == APP_OK, "stop should pause sim");
    expect_true(app_core_get_state() == APP_STATE_SIM_RUNNING, "paused sim should remain resumable");
    expect_true(app_core_get_sim_output()->running == 0U, "paused sim should stop ticking");
    expect_true(app_core_get_sim_output()->state == SIM_STATE_PAUSED, "stop should expose paused state");

    for (guard = 0U; guard < 20U; guard++) {
        app_core_tick_10ms();
    }
    expect_true(app_core_get_sim_output()->current_position.x_mm == paused_position.x_mm &&
                app_core_get_sim_output()->current_position.y_mm == paused_position.y_mm,
                "paused sim should hold current position");
    expect_true(app_core_get_sim_output()->route_distance_done_mm == paused_distance,
                "paused sim should hold route progress");

    expect_true(app_core_start_sim() == APP_OK, "start should resume paused sim");
    expect_true(app_core_get_sim_output()->running != 0U, "resumed sim should be running");
    expect_true(app_core_get_sim_output()->state == SIM_STATE_SPRAYING, "resumed sim should continue spraying");

    for (guard = 0U; guard < 20U; guard++) {
        app_core_tick_10ms();
    }
    expect_true(app_core_get_sim_output()->route_distance_done_mm > paused_distance,
                "resumed sim should continue from paused progress");
}

static void test_manual_recall_from_pause_starts_return_home(void)
{
    app_point_t paused_position;

    setup_single_water_sim();
    app_core_tick_10ms();
    app_core_tick_10ms();
    paused_position = app_core_get_sim_output()->current_position;

    expect_true(app_core_stop_sim() == APP_OK, "stop before manual recall should pause sim");
    expect_true(app_core_trigger_recall() == APP_OK, "manual recall should work while paused");
    expect_true(app_core_get_state() == APP_STATE_SIM_RUNNING, "manual recall should keep sim active");
    expect_true(app_core_get_sim_output()->running != 0U, "manual recall should restart movement");
    expect_true(app_core_get_sim_output()->state == SIM_STATE_RETURNING_TO_HOME,
                "manual recall should enter return-to-home state");
    expect_true(app_core_get_sim_output()->rth_trigger_source == 2U,
                "manual recall should mark manual trigger source");
    expect_true(app_core_get_sim_output()->breakpoint_position.x_mm == paused_position.x_mm &&
                app_core_get_sim_output()->breakpoint_position.y_mm == paused_position.y_mm,
                "manual recall should use paused position as breakpoint");
}

static void test_manual_recall_moves_towards_service_point(void)
{
    app_point_t before_position;
    uint32_t before_distance;
    uint32_t after_distance;

    setup_single_water_sim();
    app_core_tick_10ms();
    app_core_tick_10ms();
    before_position = app_core_get_sim_output()->current_position;
    before_distance = app_point_distance_mm(&before_position, &app_core_get_plan_output()->summary.service_point);

    expect_true(app_core_trigger_recall() == APP_OK, "manual recall should start return-to-home");
    app_core_tick_10ms();

    after_distance = app_point_distance_mm(&app_core_get_sim_output()->current_position,
                                           &app_core_get_plan_output()->summary.service_point);
    expect_true(app_core_get_sim_output()->state == SIM_STATE_RETURNING_TO_HOME,
                "manual recall should keep return-to-home state after first tick");
    expect_true(after_distance < before_distance,
                "manual recall should move drone towards service point");
}

static void test_sim_battery_drops_while_flying(void)
{
    uint16_t initial_battery;
    uint16_t battery_after_ticks;
    unsigned int guard;

    setup_single_water_sim();
    initial_battery = app_core_get_sim_output()->battery_x100;
    expect_true(initial_battery == 10000U, "battery should start at 100%");

    for (guard = 0U; guard < 50U; guard++) {
        app_core_tick_10ms();
    }

    battery_after_ticks = app_core_get_sim_output()->battery_x100;
    expect_true(battery_after_ticks < initial_battery, "battery should drop while flying");
    expect_true(battery_after_ticks > 0U, "battery should stay nonzero in short demo");
}

static void test_time_scale_accelerates_battery_drain(void)
{
    uint16_t normal_drop;
    uint16_t high_speed_drop;
    unsigned int guard;

    setup_single_water_sim();
    for (guard = 0U; guard < 50U; guard++) {
        app_core_tick_10ms();
    }
    normal_drop = (uint16_t)(10000U - app_core_get_sim_output()->battery_x100);

    setup_single_water_sim();
    while (sim_module_get_time_scale() != 120U) {
        (void)sim_module_cycle_time_scale();
    }
    for (guard = 0U; guard < 50U; guard++) {
        app_core_tick_10ms();
    }
    high_speed_drop = (uint16_t)(10000U - app_core_get_sim_output()->battery_x100);

    expect_true(high_speed_drop > normal_drop, "higher time scale should drain battery faster");
}

static void test_low_battery_returns_to_charge(void)
{
    unsigned int guard;
    uint8_t saw_low_battery_return;
    uint8_t saw_charging;

    saw_low_battery_return = 0U;
    saw_charging = 0U;

    setup_single_water_sim();
    while (sim_module_get_time_scale() != 120U) {
        (void)sim_module_cycle_time_scale();
    }

    for (guard = 0U; guard < 20000U; guard++) {
        const sim_output_t *sim_output = app_core_get_sim_output();

        if ((sim_output->state == SIM_STATE_RETURNING_TO_HOME) &&
            (sim_output->rth_trigger_source == 3U)) {
            saw_low_battery_return = 1U;
        }
        if ((sim_output->state == SIM_STATE_REFILLING) &&
            (sim_output->rth_trigger_source == 3U)) {
            saw_charging = 1U;
        }
        if ((saw_charging != 0U) &&
            (sim_output->state == SIM_STATE_RETURNING_TO_BREAKPOINT)) {
            break;
        }
        app_core_tick_10ms();
    }

    expect_true(saw_low_battery_return != 0U, "low battery should trigger return-to-home");
    expect_true(saw_charging != 0U, "low battery return should enter charging service state");
    expect_true(app_core_get_sim_output()->charge_count > 0U, "sim should count battery charge events");
    expect_true(app_core_get_sim_output()->battery_x100 > 9000U, "battery should be restored after charging");
}

static void test_resume_button_is_tolerant_after_auto_service(void)
{
    unsigned int guard;

    setup_single_water_sim();
    expect_true(app_core_trigger_recall() == APP_OK, "manual recall should start return-to-home");

    for (guard = 0U; guard < 20000U; guard++) {
        if (app_core_get_sim_output()->state == SIM_STATE_RETURNING_TO_BREAKPOINT) {
            break;
        }
        app_core_tick_10ms();
    }

    expect_true(app_core_get_sim_output()->state == SIM_STATE_RETURNING_TO_BREAKPOINT,
                "auto service should start returning to breakpoint");
    expect_true(app_core_resume_from_refill() == APP_OK,
                "resume button should be accepted after auto service already resumed");
}

static void test_high_wind_blocks_takeoff(void)
{
    monitor_input_t monitor_input;
    mix_input_t mix_input;
    app_result_t result;

    app_core_init();
    monitor_module_fill_default_input(&monitor_input);
    monitor_input.grid.rows = 1U;
    monitor_input.grid.cols = 1U;
    monitor_input.mode = MONITOR_MODE_MANUAL_SET;
    monitor_input.environment = g_data_default_environment;
    monitor_input.environment.wind_speed_x10 = 100U;
    monitor_input.manual_plot_id = 1U;
    monitor_input.manual_state = PLOT_STATE_WATER_DEFICIT;
    monitor_input.manual_disease_type = DISEASE_TYPE_NONE;

    result = app_core_run_monitor(&monitor_input);
    expect_true(result == APP_OK, "monitor should finish even when wind is high");
    expect_true(app_core_get_monitor_output()->environment_status.wind_ok == 0U,
                "high wind should be marked unsafe");

    mix_input.tank_capacity_ml_x10 = g_data_default_drone.tank_capacity_ml_x10;
    expect_true(app_core_run_mix_with_input(&mix_input) == APP_OK, "mix should still be available");
    expect_true(app_core_run_plan() == APP_OK, "plan should still be available");
    expect_true(app_core_start_sim() == APP_ERR_STATE, "high wind should block takeoff");
}

static void test_sim_finishes_single_planned_bucket_without_auto_refill(void)
{
    monitor_input_t monitor_input;
    mix_input_t mix_input;
    app_result_t result;
    unsigned int guard;

    app_core_init();
    monitor_module_fill_default_input(&monitor_input);
    monitor_input.grid.rows = 1U;
    monitor_input.grid.cols = 1U;
    monitor_input.mode = MONITOR_MODE_MANUAL_SET;
    monitor_input.environment = g_data_default_environment;
    monitor_input.manual_plot_id = 1U;
    monitor_input.manual_state = PLOT_STATE_WATER_DEFICIT;
    monitor_input.manual_disease_type = DISEASE_TYPE_NONE;

    result = app_core_run_monitor(&monitor_input);
    expect_true(result == APP_OK, "monitor manual water plot should succeed");

    mix_input.tank_capacity_ml_x10 = g_data_default_drone.tank_capacity_ml_x10;
    result = app_core_run_mix_with_input(&mix_input);
    expect_true(result == APP_OK, "mix should fit one planned bucket");
    expect_true(app_core_get_mix_output()->sub_batch_count == 1U, "scenario should create one sub batch");

    result = app_core_run_plan();
    expect_true(result == APP_OK, "plan should succeed");

    result = app_core_start_sim();
    expect_true(result == APP_OK, "sim should start");

    for (guard = 0U; guard < 20000U; guard++) {
        const sim_output_t *sim_output = app_core_get_sim_output();

        if ((sim_output->running != 0U) &&
            ((sim_output->state == SIM_STATE_RETURNING_TO_HOME) ||
             (sim_output->state == SIM_STATE_REFILLING) ||
             (sim_output->state == SIM_STATE_RETURNING_TO_BREAKPOINT))) {
            expect_true(0, "single planned bucket should not auto-refill near completion");
            break;
        }

        if (app_core_get_state() == APP_STATE_SIM_DONE) {
            break;
        }

        app_core_tick_10ms();
    }

    expect_true(app_core_get_state() == APP_STATE_SIM_DONE, "sim should reach done state");
    expect_true(app_core_get_sim_output()->alarm_count > 0U &&
                app_core_get_sim_output()->alarm[0].code == APP_ALARM_ROUTE_FINISHED,
                "sim should finish route");
}

static void test_plot_id_parser_rejects_wraparound_values(void)
{
    uint16_t value;

    value = 0U;
    expect_true(app_parse_u16_bounded("1", 1U, APP_MAX_PLOT_COUNT, &value) != 0U,
                "valid plot id should parse");
    expect_true(value == 1U, "valid plot id should be returned");

    value = 77U;
    expect_true(app_parse_u16_bounded("65537", 1U, APP_MAX_PLOT_COUNT, &value) == 0U,
                "large plot id should be rejected instead of wrapping");
    expect_true(value == 77U, "failed parse should leave output unchanged");

    expect_true(app_parse_u16_bounded("", 1U, APP_MAX_PLOT_COUNT, &value) == 0U,
                "empty plot id should be rejected");
    expect_true(app_parse_u16_bounded("12x", 1U, APP_MAX_PLOT_COUNT, &value) == 0U,
                "non-digit plot id should be rejected");
}

int main(void)
{
    test_default_environment_is_suitable();
    test_crop_stage_changes_environment_limits();
    test_region_severity_tracks_ratio_and_environment();
    test_manual_recall_refills_and_auto_resumes();
    test_stop_then_start_resumes_from_paused_position();
    test_manual_recall_from_pause_starts_return_home();
    test_manual_recall_moves_towards_service_point();
    test_sim_battery_drops_while_flying();
    test_time_scale_accelerates_battery_drain();
    test_low_battery_returns_to_charge();
    test_resume_button_is_tolerant_after_auto_service();
    test_high_wind_blocks_takeoff();
    test_sim_finishes_single_planned_bucket_without_auto_refill();
    test_plot_id_parser_rejects_wraparound_values();

    if (g_failures != 0) {
        printf("%d test(s) failed\n", g_failures);
        return 1;
    }

    printf("all tests passed\n");
    return 0;
}
