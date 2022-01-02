#include <linux/err.h>
#include <linux/module.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/cpu.h>

#include "virtio.h"


void
virtperiscope_clear_request(struct virtio_periscope_request *vc_req)
{
	if (vc_req) {
		kzfree(vc_req->req_data);
		kfree(vc_req->sgs);
	}
}

static void virtperiscope_dataq_callback(struct virtqueue *vq)
{
	struct virtio_periscope *vperiscope = vq->vdev->priv;
	struct virtio_periscope_request *vc_req;
	unsigned long flags;
	unsigned int len;
	unsigned int qid = vq->index;

	spin_lock_irqsave(&vperiscope->data_vq[qid].lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((vc_req = virtqueue_get_buf(vq, &len)) != NULL) {
			spin_unlock_irqrestore(
				&vperiscope->data_vq[qid].lock, flags);
			if (vc_req->cb)
				vc_req->cb(vc_req, len);
			spin_lock_irqsave(
				&vperiscope->data_vq[qid].lock, flags);
		}
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&vperiscope->data_vq[qid].lock, flags);
}

static int virtperiscope_find_vqs(struct virtio_periscope *vi)
{
	vq_callback_t **callbacks;
	struct virtqueue **vqs;
	int ret = -ENOMEM;
	int i, total_vqs;
	const char **names;
#if 0
	struct device *dev = &vi->vdev->dev;
#endif

	/*
	 * We expect 1 virtqueue for KCOV
	 */
	total_vqs = 1;

	/* Allocate space for find_vqs parameters */
	vqs = kcalloc(total_vqs, sizeof(*vqs), GFP_KERNEL);
	if (!vqs)
		goto err_vq;
	callbacks = kcalloc(total_vqs, sizeof(*callbacks), GFP_KERNEL);
	if (!callbacks)
		goto err_callback;
	names = kcalloc(total_vqs, sizeof(*names), GFP_KERNEL);
	if (!names)
		goto err_names;

	for (i = 0; i < 1; i++) {
		callbacks[i] = virtperiscope_dataq_callback;
		snprintf(vi->data_vq[i].name, sizeof(vi->data_vq[i].name),
				"dataq.%d", i);
		names[i] = vi->data_vq[i].name;
	}

	ret = virtio_find_vqs(vi->vdev, total_vqs, vqs, callbacks, names, NULL);
	if (ret)
		goto err_find;

	for (i = 0; i < 1; i++) {
		spin_lock_init(&vi->data_vq[i].lock);
		vi->data_vq[i].vq = vqs[i];
	}

	kfree(names);
	kfree(callbacks);
	kfree(vqs);

	return 0;

err_find:
	kfree(names);
err_names:
	kfree(callbacks);
err_callback:
	kfree(vqs);
err_vq:
	return ret;
}

static int virtperiscope_alloc_queues(struct virtio_periscope *vi)
{
	return 0;
#if 0
	vi->data_vq = kcalloc(vi->max_data_queues, sizeof(*vi->data_vq),
				GFP_KERNEL);
	if (!vi->data_vq)
		return -ENOMEM;

	return 0;
#endif
}

static void virtperiscope_free_queues(struct virtio_periscope *vi)
{
	kfree(vi->data_vq);
}

static int virtperiscope_init_vqs(struct virtio_periscope *vi)
{
	int ret;

	/* Allocate send & receive queues */
	ret = virtperiscope_alloc_queues(vi);
	if (ret)
		goto err;

	ret = virtperiscope_find_vqs(vi);
	if (ret)
		goto err_free;

	get_online_cpus();
	put_online_cpus();

	return 0;

err_free:
	virtperiscope_free_queues(vi);
err:
	return ret;
}

static void virtperiscope_del_vqs(struct virtio_periscope *vperiscope)
{
	struct virtio_device *vdev = vperiscope->vdev;

	vdev->config->del_vqs(vdev);

	virtperiscope_free_queues(vperiscope);
}

static int virtperiscope_probe(struct virtio_device *vdev)
{
	int err = -EFAULT;
	struct virtio_periscope *vperiscope;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	if (!vdev->config->get) {
		dev_err(&vdev->dev, "%s failure: config access disabled\n",
			__func__);
		return -EINVAL;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&vdev->dev) < 0) {
		/*
		 * If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&vdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	vperiscope = kzalloc_node(sizeof(*vperiscope), GFP_KERNEL,
					dev_to_node(&vdev->dev));
	if (!vperiscope)
		return -ENOMEM;

	vperiscope->owner = THIS_MODULE;
	vperiscope = vdev->priv = vperiscope;
	vperiscope->vdev = vdev;

	err = virtperiscope_init_vqs(vperiscope);
	if (err) {
		dev_err(&vdev->dev, "Failed to initialize vqs.\n");
		goto free;
	}

	virtio_device_ready(vdev);

	return 0;

#if 0
free_vqs:
	vperiscope->vdev->config->reset(vdev);
	virtperiscope_del_vqs(vperiscope);
#endif
free:
	kfree(vperiscope);
	return err;
}

static void virtperiscope_free_unused_reqs(struct virtio_periscope *vperiscope)
{
	struct virtio_periscope_request *vc_req;
	int i;
	struct virtqueue *vq;

	for (i = 0; i < 1; i++) {
		vq = vperiscope->data_vq[i].vq;
		while ((vc_req = virtqueue_detach_unused_buf(vq)) != NULL) {
			kfree(vc_req->req_data);
			kfree(vc_req->sgs);
		}
	}
}

static void virtperiscope_remove(struct virtio_device *vdev)
{
	struct virtio_periscope *vperiscope = vdev->priv;

	dev_info(&vdev->dev, "Start virtperiscope_remove.\n");

	vdev->config->reset(vdev);
	virtperiscope_free_unused_reqs(vperiscope);
	virtperiscope_del_vqs(vperiscope);
	kfree(vperiscope);
}

static void virtperiscope_config_changed(struct virtio_device *vdev)
{
	struct virtio_periscope *vperiscope = vdev->priv;

	(void)vperiscope;
}

static unsigned int features[] = {
	/* none */
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_PERISCOPE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_periscope_driver = {
	.driver.name         = KBUILD_MODNAME,
	.driver.owner        = THIS_MODULE,
	.feature_table       = features,
	.feature_table_size  = ARRAY_SIZE(features),
	.id_table            = id_table,
	.probe               = virtperiscope_probe,
	.remove              = virtperiscope_remove,
	.config_changed      = virtperiscope_config_changed,
};

module_virtio_driver(virtio_periscope_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("virtio periscope device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dokyung Song <dokyungs@uci.edu>");
