#include "slider/behavior_slider.h"
#include <logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct slider_gpio_spec {
    const struct device *port;
    gpio_pin_t pin;
    gpio_dt_flags_t dt_flags;
};

struct slider_config {
    struct slider_gpio_spec gpios[4];  // 最大4ボタン
    uint8_t num_buttons;              // 実際に使用するボタン数
    uint32_t *sequence_up;           // 上方向のシーケンス
    uint32_t *sequence_down;         // 下方向のシーケンス
    struct zmk_behavior_keys key_items;
};

struct slider_data {
    struct k_timer reset_timer;
    int slider_sum;
    int button_count;
};

static struct k_timer reset_timer; // 状態をリセットするためのタイマー
static int slider_sum = 0;         // 合計値
static int button_count = 0;       // 押されたボタンの数

static struct gpio_callback button_cb_data[4];

// タイマーコールバック: 状態をリセットする
void reset_slider_state(struct k_timer *timer_id) {
    slider_sum = 0;
    button_count = 0;
}

// シーケンス評価を動的に行う
static int evaluate_slider_sequence(const struct slider_config *config, int sequence) {
    // 現在のシーケンスを数値配列に変換
    int digits[4] = {0};
    int temp = sequence;
    for (int i = config->num_buttons - 1; i >= 0; i--) {
        digits[i] = temp % 10;
        temp /= 10;
    }

    // 上方向のシーケンスと比較
    bool matches_up = true;
    for (int i = 0; i < config->num_buttons; i++) {
        if (digits[i] != config->sequence_up[i]) {
            matches_up = false;
            break;
        }
    }
    if (matches_up) return 0;

    // 下方向のシーケンスと比較
    bool matches_down = true;
    for (int i = 0; i < config->num_buttons; i++) {
        if (digits[i] != config->sequence_down[i]) {
            matches_down = false;
            break;
        }
    }
    if (matches_down) return 1;

    return -1;
}

// スライダーのイベントを処理
void slider_process(uint32_t key) {
    k_timer_stop(&reset_timer);

    if (key < 1 || key > 4) {
        LOG_WRN("Slider : Invalid key value: %d", key);
        return;
    }

    // ボタン番号を数値として処理
    switch (key) {
        case 1:
            slider_sum = slider_sum * 10 + 1;
            button_count++;
            break;
        case 2:
            slider_sum = slider_sum * 10 + 2;
            button_count++;
            break;
        case 3:
            slider_sum = slider_sum * 10 + 3;
            button_count++;
            break;
        case 4:
            slider_sum = slider_sum * 10 + 4;
            button_count++;
            break;
        default:
            slider_sum = 0;
            button_count = 0;
            return;
    }

    // ボタンが4つ押された場合に評価
    if (button_count == config->num_buttons) {  // 設定されたボタン数に基づいてチェック
        const struct slider_config *config = CONTAINER_OF(
            dev->config, struct slider_config, config);
        
        if (config->key_items.size < 2) {
            LOG_ERR("Slider : Insufficient key bindings configured");
            return;
        }

        int sequence_type = evaluate_slider_sequence(config, slider_sum);
        if (sequence_type >= 0) {
            const zmk_key_t key = config->key_items.keys[sequence_type];
            int err = zmk_keymap_press(key);
            if (err) {
                LOG_ERR("Slider : Failed to press key: %d", err);
            }
        } else {
            LOG_WRN("Slider : Invalid sequence: %d", slider_sum);
        }

        slider_sum = 0;
        button_count = 0;
    }
    // タイマー再起動 (例: 1秒後にリセット)
    k_timer_start(&reset_timer, K_SECONDS(1), K_NO_WAIT);
}

static void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // GPIOピンからボタン番号を特定
    const struct slider_config *config = dev->config;
    for (int i = 0; i < config->num_buttons; i++) {
        if (pins & BIT(config->gpios[i].pin)) {
            slider_process(i + 1);
            break;
        }
    }
}

static int slider_init_gpio(const struct device *dev)
{
    const struct slider_config *config = dev->config;
    int err;

    for (int i = 0; i < config->num_buttons; i++) {
        err = gpio_pin_configure(config->gpios[i].port, 
                               config->gpios[i].pin,
                               GPIO_INPUT | config->gpios[i].dt_flags);
        if (err) {
            LOG_ERR("Slider : Failed to configure GPIO pin %d: %d", i, err);
            return err;
        }

        err = gpio_pin_interrupt_configure(config->gpios[i].port,
                                         config->gpios[i].pin,
                                         GPIO_INT_EDGE_TO_ACTIVE);
        if (err) {
            LOG_ERR("Slider : Failed to configure GPIO interrupt %d: %d", i, err);
            return err;
        }

        gpio_init_callback(&button_cb_data[i],
                          button_pressed_callback,
                          BIT(config->gpios[i].pin));
        gpio_add_callback(config->gpios[i].port, &button_cb_data[i]);
    }
    return 0;
}

// モジュールの初期化
static int slider_module_init(const struct device *dev)
{
    struct slider_data *data = dev->data;
    const struct slider_config *config = dev->config;
    int err;

    k_timer_init(&data->reset_timer, reset_slider_state, NULL);

    err = slider_init_gpio(dev);
    if (err) {
        LOG_ERR("Slider : Failed to initialize GPIO: %d", err);
        return err;
    }

    LOG_INF("Slider : Slider module initialized successfully");
    return 0;
}

#define SEQUENCE_GPIO(i, n, prop) \
    { \
        .port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(n, prop, i)), \
        .pin = DT_GPIO_PIN_BY_IDX(n, prop, i), \
        .flags = DT_GPIO_FLAGS_BY_IDX(n, prop, i) \
    }

#define PROP_SEQUENCES(n, prop) \
    COND_CODE_1(DT_NODE_HAS_PROP(DT_DRV_INST(n), prop), \
                ({ \
                    .size = DT_INST_PROP_LEN(n, prop), \
                    .gpios = { LISTIFY(DT_INST_PROP_LEN(n, prop), SEQUENCE_GPIO, (, ), n, prop) } \
                }), \
                ({.size = 0}))

#define KEY_LIST_ITEM(i, n, prop) ZMK_KEY_PARAM_DECODE(DT_INST_PROP_BY_IDX(n, prop, i))

#define PROP_KEY_LIST(n, prop)                                                                     \
    COND_CODE_1(DT_NODE_HAS_PROP(DT_DRV_INST(n), prop),                                            \
                ({                                                                                 \
                    .size = DT_INST_PROP_LEN(n, prop),                                             \
                    .keys = {LISTIFY(DT_INST_PROP_LEN(n, prop), KEY_LIST_ITEM, (, ), n, prop)},    \
                }),                                                                                \
                ({.size = 0}))


#define SLIDER_GPIO_SPEC(node_id, prop, idx) \
{ \
    .port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(node_id, prop, idx)), \
    .pin = DT_GPIO_PIN_BY_IDX(node_id, prop, idx), \
    .dt_flags = DT_GPIO_FLAGS_BY_IDX(node_id, prop, idx), \
}

#define SLIDER_CONFIG(n) \
{ \
    .gpios = { \
        SLIDER_GPIO_SPEC(DT_DRV_INST(n), gpio_keys_buttons, 0), \
        SLIDER_GPIO_SPEC(DT_DRV_INST(n), gpio_keys_buttons, 1), \
        SLIDER_GPIO_SPEC(DT_DRV_INST(n), gpio_keys_buttons, 2), \
        SLIDER_GPIO_SPEC(DT_DRV_INST(n), gpio_keys_buttons, 3), \
    }, \
    .num_buttons = DT_PROP(DT_INST(n, slider_config), num-buttons), \
    .sequence_up = DT_PROP(DT_INST(n, slider_config), sequence-up), \
    .sequence_down = DT_PROP(DT_INST(n, slider_config), sequence-down), \
    .key_items = PROP_KEY_LIST(n, bindings) \
}

#define SLIDER_DEFINE(n) \
    static struct slider_data slider_data_##n = {0}; \
    static const struct slider_config slider_cfg_##n = SLIDER_CONFIG(n); \
    static const struct zmk_behavior_keys key_items_##n = PROP_KEY_LIST(n, bindings); \
    DEVICE_DT_INST_DEFINE(n, slider_module_init, device_pm_control_nop, \
                         &slider_data_##n, &slider_cfg_##n, POST_KERNEL, \
                         CONFIG_APPLICATION_INIT_PRIORITY, NULL);
            

// デバイスツリーノードと連携
SYS_INIT(slider_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

DT_INST_FOREACH_STATUS_OKAY(SLIDER_DEFINE)