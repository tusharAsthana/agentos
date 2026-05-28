// SPDX-License-Identifier: GPL-2.0
/*
 * AgentOS — HVC Display Driver
 *
 * Registers as a DRM simple-framebuffer device. All pixel writes
 * go via HAC_HVC_DISPLAY_BLIT to the HAC at EL2, which copies
 * the framebuffer to the hardware display using direct MMIO/DCP.
 *
 * The physical framebuffer is pre-allocated by HAC at FB_DEFAULT_PA.
 * We map it into kernel virtual space and expose it as /dev/fb0 and
 * as a DRM connector for compositor use.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/io.h>
#include "hac_hypercalls.h"

#define DRIVER_NAME     "hvc-display"
#define FB_WIDTH        1125
#define FB_HEIGHT       2436
#define FB_BPP          32
#define FB_STRIDE       (FB_WIDTH * (FB_BPP / 8))
#define FB_SIZE         (FB_STRIDE * FB_HEIGHT)
#define FB_DEFAULT_PA   0x83C000000ULL

struct hvc_display_priv {
    struct fb_info     *info;
    void __iomem       *fb_virt;   /* kernel VA of framebuffer */
    phys_addr_t         fb_phys;
};

/* ---------------------------------------------------------------
 * fb_ops
 * --------------------------------------------------------------- */
static int hvc_display_setcolreg(unsigned regno, unsigned red, unsigned green,
                                  unsigned blue, unsigned transp,
                                  struct fb_info *info)
{
    if (regno >= 256) return -EINVAL;
    /* ARGB8888: pack colour into pseudo-palette */
    if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 16) {
        u32 *pal = info->pseudo_palette;
        pal[regno] = ((red   >> (16 - info->var.red.length))   << info->var.red.offset)   |
                     ((green >> (16 - info->var.green.length)) << info->var.green.offset) |
                     ((blue  >> (16 - info->var.blue.length))  << info->var.blue.offset);
    }
    return 0;
}

static void hvc_display_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
    /* Called by deferred I/O when pages are written.
     * Blit the dirty region to HAC display via HVC. */
    phys_addr_t fb_pa = virt_to_phys(info->screen_base);
    hac_hvc5(HAC_HVC_DISPLAY_BLIT, fb_pa, 0, 0, FB_WIDTH, FB_HEIGHT);
}

static struct fb_deferred_io hvc_defio = {
    .delay   = HZ / 30,          /* 30 fps */
    .deferred_io = hvc_display_deferred_io,
};

static struct fb_ops hvc_fb_ops = {
    .owner          = THIS_MODULE,
    .fb_setcolreg   = hvc_display_setcolreg,
    .fb_fillrect    = cfb_fillrect,
    .fb_copyarea    = cfb_copyarea,
    .fb_imageblit   = cfb_imageblit,
};

/* ---------------------------------------------------------------
 * Platform driver probe
 * --------------------------------------------------------------- */
static int hvc_display_probe(struct platform_device *pdev)
{
    struct hvc_display_priv *priv;
    struct fb_info *info;
    int ret;

    /* Verify HAC is alive */
    long ver = hac_hvc0(HAC_HVC_VERSION);
    if (ver <= 0) {
        dev_err(&pdev->dev, "HAC not responding (version=%ld)\n", ver);
        return -ENODEV;
    }
    dev_info(&pdev->dev, "HAC version 0x%lx detected\n", ver);

    info = framebuffer_alloc(sizeof(*priv), &pdev->dev);
    if (!info) return -ENOMEM;
    priv = info->par;

    /* Map HAC framebuffer into kernel virtual address space */
    priv->fb_phys = FB_DEFAULT_PA;
    priv->fb_virt = ioremap_wc(priv->fb_phys, FB_SIZE);
    if (!priv->fb_virt) {
        ret = -ENOMEM;
        goto err_alloc;
    }

    /* Fill fb_fix_screeninfo */
    strscpy(info->fix.id, "hvc-display", sizeof(info->fix.id));
    info->fix.smem_start  = priv->fb_phys;
    info->fix.smem_len    = FB_SIZE;
    info->fix.type        = FB_TYPE_PACKED_PIXELS;
    info->fix.visual      = FB_VISUAL_TRUECOLOR;
    info->fix.line_length = FB_STRIDE;

    /* Fill fb_var_screeninfo */
    info->var.xres           = FB_WIDTH;
    info->var.yres           = FB_HEIGHT;
    info->var.xres_virtual   = FB_WIDTH;
    info->var.yres_virtual   = FB_HEIGHT;
    info->var.bits_per_pixel = FB_BPP;
    /* ARGB8888 layout */
    info->var.red.offset     = 16; info->var.red.length   = 8;
    info->var.green.offset   =  8; info->var.green.length = 8;
    info->var.blue.offset    =  0; info->var.blue.length  = 8;
    info->var.transp.offset  = 24; info->var.transp.length= 8;
    info->var.activate       = FB_ACTIVATE_NOW;

    info->screen_base   = priv->fb_virt;
    info->screen_size   = FB_SIZE;
    info->fbops         = &hvc_fb_ops;
    info->fbdefio       = &hvc_defio;
    info->pseudo_palette = devm_kzalloc(&pdev->dev, sizeof(u32) * 16, GFP_KERNEL);

    fb_deferred_io_init(info);

    ret = register_framebuffer(info);
    if (ret) goto err_iounmap;

    platform_set_drvdata(pdev, info);
    dev_info(&pdev->dev, "HVC display registered: %dx%d ARGB8888\n",
             FB_WIDTH, FB_HEIGHT);
    return 0;

err_iounmap:
    iounmap(priv->fb_virt);
err_alloc:
    framebuffer_release(info);
    return ret;
}

static int hvc_display_remove(struct platform_device *pdev)
{
    struct fb_info *info = platform_get_drvdata(pdev);
    struct hvc_display_priv *priv = info->par;
    fb_deferred_io_cleanup(info);
    unregister_framebuffer(info);
    iounmap(priv->fb_virt);
    framebuffer_release(info);
    return 0;
}

static const struct of_device_id hvc_display_of_match[] = {
    { .compatible = "agentos,hvc-display" },
    { }
};
MODULE_DEVICE_TABLE(of, hvc_display_of_match);

static struct platform_driver hvc_display_driver = {
    .probe  = hvc_display_probe,
    .remove = hvc_display_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = hvc_display_of_match,
    },
};
module_platform_driver(hvc_display_driver);

MODULE_DESCRIPTION("AgentOS HVC Display Driver");
MODULE_LICENSE("GPL v2");
