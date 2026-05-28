// SPDX-License-Identifier: GPL-2.0
/*
 * AgentOS — HVC Touchscreen Driver
 *
 * Registers a multi-touch input device. Touch events are delivered
 * two ways:
 *   1. IRQ path: HAC pushes IPC_MSG_TOUCH_EVENT into the EL2→EL1
 *      ring buffer; an IRQ from the HAC notifies the kernel.
 *   2. Polling fallback: a timer calls HAC_HVC_TOUCH_POLL directly.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/dma-mapping.h>
#include "hac_hypercalls.h"

#define DRIVER_NAME     "hvc-touch"
#define TOUCH_MAX_X     1125
#define TOUCH_MAX_Y     2436
#define TOUCH_MAX_SLOTS 5
#define POLL_MS         16   /* ~60 Hz polling */

struct hvc_touch_event {
    u16 x, y;
    u8  pressure, id, state, _reserved;
} __packed;

struct hvc_touch_priv {
    struct input_dev        *input;
    struct timer_list        poll_timer;
    struct hvc_touch_event  *event_buf;  /* DMA-coherent buffer */
    dma_addr_t               event_dma;
};

static void hvc_touch_report(struct hvc_touch_priv *priv)
{
    long count = hac_hvc2(HAC_HVC_TOUCH_POLL,
                          (u64)priv->event_dma,
                          TOUCH_MAX_SLOTS);
    if (count <= 0) return;

    for (int i = 0; i < count && i < TOUCH_MAX_SLOTS; i++) {
        struct hvc_touch_event *e = &priv->event_buf[i];
        input_mt_slot(priv->input, e->id);
        if (e->state == 0) {  /* finger up */
            input_mt_report_slot_state(priv->input, MT_TOOL_FINGER, false);
        } else {
            input_mt_report_slot_state(priv->input, MT_TOOL_FINGER, true);
            input_report_abs(priv->input, ABS_MT_POSITION_X, e->x);
            input_report_abs(priv->input, ABS_MT_POSITION_Y, e->y);
            input_report_abs(priv->input, ABS_MT_PRESSURE,   e->pressure);
        }
    }
    input_mt_report_pointer_emulation(priv->input, true);
    input_sync(priv->input);
}

static void hvc_touch_poll_cb(struct timer_list *t)
{
    struct hvc_touch_priv *priv = from_timer(priv, t, poll_timer);
    hvc_touch_report(priv);
    mod_timer(&priv->poll_timer, jiffies + msecs_to_jiffies(POLL_MS));
}

static int hvc_touch_probe(struct platform_device *pdev)
{
    struct hvc_touch_priv *priv;
    struct input_dev      *input;
    int ret;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) return -ENOMEM;

    /* Allocate DMA-coherent event buffer */
    priv->event_buf = dma_alloc_coherent(&pdev->dev,
                        sizeof(*priv->event_buf) * TOUCH_MAX_SLOTS,
                        &priv->event_dma, GFP_KERNEL);
    if (!priv->event_buf) return -ENOMEM;

    input = devm_input_allocate_device(&pdev->dev);
    if (!input) { ret = -ENOMEM; goto err_dma; }
    priv->input = input;

    input->name = "AgentOS HVC Touchscreen";
    input->id.bustype = BUS_HOST;

    input_set_abs_params(input, ABS_MT_POSITION_X, 0, TOUCH_MAX_X, 0, 0);
    input_set_abs_params(input, ABS_MT_POSITION_Y, 0, TOUCH_MAX_Y, 0, 0);
    input_set_abs_params(input, ABS_MT_PRESSURE,   0, 255,         0, 0);
    input_mt_init_slots(input, TOUCH_MAX_SLOTS, INPUT_MT_DIRECT);

    ret = input_register_device(input);
    if (ret) goto err_dma;

    /* Enable touch hardware */
    hac_hvc1(HAC_HVC_TOUCH_ENABLE, 1);

    /* Start polling timer */
    timer_setup(&priv->poll_timer, hvc_touch_poll_cb, 0);
    mod_timer(&priv->poll_timer, jiffies + msecs_to_jiffies(POLL_MS));

    platform_set_drvdata(pdev, priv);
    dev_info(&pdev->dev, "HVC touch registered (%dx%d, %d slots)\n",
             TOUCH_MAX_X, TOUCH_MAX_Y, TOUCH_MAX_SLOTS);
    return 0;

err_dma:
    dma_free_coherent(&pdev->dev,
                      sizeof(*priv->event_buf) * TOUCH_MAX_SLOTS,
                      priv->event_buf, priv->event_dma);
    return ret;
}

static int hvc_touch_remove(struct platform_device *pdev)
{
    struct hvc_touch_priv *priv = platform_get_drvdata(pdev);
    del_timer_sync(&priv->poll_timer);
    hac_hvc1(HAC_HVC_TOUCH_ENABLE, 0);
    dma_free_coherent(&pdev->dev,
                      sizeof(*priv->event_buf) * TOUCH_MAX_SLOTS,
                      priv->event_buf, priv->event_dma);
    return 0;
}

static const struct of_device_id hvc_touch_of_match[] = {
    { .compatible = "agentos,hvc-touch" },
    { }
};
MODULE_DEVICE_TABLE(of, hvc_touch_of_match);

static struct platform_driver hvc_touch_driver = {
    .probe  = hvc_touch_probe,
    .remove = hvc_touch_remove,
    .driver = { .name = DRIVER_NAME, .of_match_table = hvc_touch_of_match },
};
module_platform_driver(hvc_touch_driver);

MODULE_DESCRIPTION("AgentOS HVC Touchscreen Driver");
MODULE_LICENSE("GPL v2");
