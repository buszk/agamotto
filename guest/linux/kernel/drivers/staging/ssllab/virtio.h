#ifndef _VIRTIO_PERISCOPE_H
#define _VIRTIO_PERISCOPE_H

#include <linux/virtio.h>
#include <linux/spinlock.h>

struct data_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* To protect the vq operations for the dataq */
	spinlock_t lock;

	/* Name of the queue: dataq.$index */
	char name[32];
};

struct virtio_periscope {
	struct virtio_device *vdev;
	struct data_queue data_vq[1];
	struct module *owner;
};

struct virtio_periscope_request;
typedef void (*virtio_periscope_data_callback)
		(struct virtio_periscope_request *vc_req, int len);

struct virtio_periscope_request {
	uint8_t status;
	struct virtio_periscope_op_data_req *req_data;
	struct scatterlist **sgs;
	struct data_queue *dataq;
	virtio_periscope_data_callback cb;
};

#endif
