// SPDX-License-Identifier: GPL-2.0
/*
 * AgentOS — HVC Block Storage Driver
 *
 * Exposes the iPhone NAND storage as a Linux block device /dev/hvcblk0
 * by routing all I/O through HAC_HVC_STORAGE_READ/WRITE hypercalls.
 * The HAC directly controls the ANS2 NVMe coprocessor at EL2.
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include "hac_hypercalls.h"

#define DRIVER_NAME     "hvc-storage"
#define DISK_NAME       "hvcblk"
#define SECTOR_SIZE     512
#define QUEUE_DEPTH     16

struct hvc_storage_priv {
    struct gendisk       *disk;
    struct blk_mq_tag_set tag_set;
    u64                   total_sectors;
    /* Bounce buffer for DMA-safe transfers */
    void                 *bounce_buf;
    dma_addr_t            bounce_dma;
};

static blk_status_t hvc_storage_queue_rq(struct blk_mq_hw_ctx *hctx,
                                           const struct blk_mq_queue_data *bd)
{
    struct request           *rq   = bd->rq;
    struct hvc_storage_priv  *priv = hctx->queue->queuedata;
    struct bio_vec            bvec;
    struct req_iterator       iter;
    u64  lba = blk_rq_pos(rq);
    bool write = (req_op(rq) == REQ_OP_WRITE);

    blk_mq_start_request(rq);

    rq_for_each_segment(bvec, rq, iter) {
        u64  n_sectors = bvec.bv_len / SECTOR_SIZE;
        void *buf      = page_address(bvec.bv_page) + bvec.bv_offset;
        phys_addr_t pa = virt_to_phys(buf);

        long ret;
        if (write)
            ret = hac_hvc3(HAC_HVC_STORAGE_WRITE, lba, n_sectors, pa);
        else
            ret = hac_hvc3(HAC_HVC_STORAGE_READ,  lba, n_sectors, pa);

        if (ret != HAC_OK) {
            blk_mq_end_request(rq, BLK_STS_IOERR);
            return BLK_STS_IOERR;
        }
        lba += n_sectors;
    }

    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
}

static const struct blk_mq_ops hvc_storage_mq_ops = {
    .queue_rq = hvc_storage_queue_rq,
};

static int hvc_storage_probe(struct platform_device *pdev)
{
    struct hvc_storage_priv *priv;
    int ret;

    priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) return -ENOMEM;

    /* Query storage size from HAC */
    long info = hac_hvc0(HAC_HVC_STORAGE_INFO);
    priv->total_sectors = (info > 0) ? (u64)info : (256ULL * 1024 * 1024 * 2); /* 256 GB fallback */

    /* Set up blk-mq tag set */
    priv->tag_set.ops      = &hvc_storage_mq_ops;
    priv->tag_set.nr_hw_queues = 1;
    priv->tag_set.queue_depth  = QUEUE_DEPTH;
    priv->tag_set.numa_node    = NUMA_NO_NODE;
    priv->tag_set.cmd_size     = 0;
    priv->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;

    ret = blk_mq_alloc_tag_set(&priv->tag_set);
    if (ret) return ret;

    priv->disk = blk_mq_alloc_disk(&priv->tag_set, priv);
    if (IS_ERR(priv->disk)) {
        ret = PTR_ERR(priv->disk);
        goto err_tag;
    }

    strscpy(priv->disk->disk_name, DISK_NAME, DISK_NAMELEN);
    priv->disk->major       = 0;  /* dynamic */
    priv->disk->first_minor = 0;
    priv->disk->minors      = 8;
    priv->disk->fops        = NULL;  /* minimal — add ioctl later */
    set_capacity(priv->disk, priv->total_sectors);
    blk_queue_logical_block_size(priv->disk->queue, SECTOR_SIZE);
    blk_queue_physical_block_size(priv->disk->queue, 4096);
    blk_queue_max_hw_sectors(priv->disk->queue, 256);  /* 128 KB at a time */

    ret = add_disk(priv->disk);
    if (ret) goto err_disk;

    platform_set_drvdata(pdev, priv);
    dev_info(&pdev->dev, "HVC storage: %llu MB\n",
             priv->total_sectors / (1024 * 1024 / SECTOR_SIZE));
    return 0;

err_disk:  put_disk(priv->disk);
err_tag:   blk_mq_free_tag_set(&priv->tag_set);
    return ret;
}

static int hvc_storage_remove(struct platform_device *pdev)
{
    struct hvc_storage_priv *priv = platform_get_drvdata(pdev);
    hac_hvc0(HAC_HVC_STORAGE_FLUSH);
    del_gendisk(priv->disk);
    put_disk(priv->disk);
    blk_mq_free_tag_set(&priv->tag_set);
    return 0;
}

static const struct of_device_id hvc_storage_of_match[] = {
    { .compatible = "agentos,hvc-storage" }, { }
};
MODULE_DEVICE_TABLE(of, hvc_storage_of_match);

static struct platform_driver hvc_storage_driver = {
    .probe  = hvc_storage_probe,
    .remove = hvc_storage_remove,
    .driver = { .name = DRIVER_NAME, .of_match_table = hvc_storage_of_match },
};
module_platform_driver(hvc_storage_driver);
MODULE_DESCRIPTION("AgentOS HVC Storage Driver");
MODULE_LICENSE("GPL v2");
