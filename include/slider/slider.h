#ifndef GPIOSLIDER_MODULE_H
#define GPIOSLIDER_MODULE_H
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/keys.h>
// モジュールの初期化関数
int slider_module_init(const struct device *dev);

// スライダーイベントを処理する関数
void slider_process(uint32_t key);
#endif // GPIOSLIDER_MODULE_H
