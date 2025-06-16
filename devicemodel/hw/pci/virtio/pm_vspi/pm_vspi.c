#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <protobuf-c/protobuf-c.h>

#define pr_prefix "pm_vspi: "

#include "log.h"
#include "monitor.h"
#include "virtio_spi.h"
#include "VehicleHalProto.pb-c.h"

#define INT32_VEC 4259840
#define VEHICLEPROPERTY_AP_state_REQ 0x11410a00
#define VEHICLEAPPOWERSTATEREQ_SHUTDOWN_PREPARE 0x1
#define VEHICLEAPPOWERSTATESHUTDOWNPARAM_SHUTDOWN_IMMEDIATELY 0x1
#define VEHICLEAPPOWERSTATESHUTDOWNPARAM_CAN_SLEEP 0x2
#define VSPI_PM_MSG_SIZE 128

enum vspi_pm_state {
	NORMAL = 0,
	S3 = 3,
	S5 = 5,
};

static struct vspidev_pm {
	bool enabled;
	struct vspidev *vspidev;
	enum vspi_pm_state state;
} vspi_pm = { 0 };

static int spidev_init_pm(struct vspidev *vspidev, char *opts)
{
	int ret = 0;

	if (vspi_pm.vspidev != NULL) {
		pr_err("vm can only have 1 vspi_pm device\n");
		ret = -EINVAL;
	} else {
		if (!vspi_pm.enabled) {
			pr_warn("device created but pm notify is not enabled\n");
			pr_warn("add '--pm_notify_channel vspi' in launch script to enable it\n");
		}

		vspidev->priv = &vspi_pm;
		vspi_pm.vspidev = vspidev;
		vspi_pm.state = 0;
	}

	return ret;
}

static void spidev_deinit_pm(struct vspidev *vspidev)
{
	vspi_pm.vspidev = NULL;
	vspidev->priv = NULL;
	vspi_pm.state = 0;
}

static int vspi_pm_encode_response(void *buffer, uint32_t buffer_size)
{
	int ret = 0;
	uint32_t message_size;
	int32_t power_state_values[2] = { 0, 0 };
	VhalProto__EmulatorMessage message = VHAL_PROTO__EMULATOR_MESSAGE__INIT;
	VhalProto__VehiclePropValue prop_value = VHAL_PROTO__VEHICLE_PROP_VALUE__INIT;
	VhalProto__VehiclePropValue *prop_value_ptrs[1] = { &prop_value };

	// Set up the message
	message.msg_type = VHAL_PROTO__MSG_TYPE__SET_PROPERTY_CMD;
	message.value = prop_value_ptrs;
	message.n_value = sizeof(prop_value_ptrs) / sizeof(prop_value_ptrs[0]);

	// Set up the property value
	prop_value.prop = VEHICLEPROPERTY_AP_state_REQ;
	prop_value.value_type = INT32_VEC;
	if (vspi_pm.state == S3) {
		power_state_values[0] = VEHICLEAPPOWERSTATEREQ_SHUTDOWN_PREPARE;
		power_state_values[1] = VEHICLEAPPOWERSTATESHUTDOWNPARAM_CAN_SLEEP;
	} else if (vspi_pm.state == S5) {
		power_state_values[0] = VEHICLEAPPOWERSTATEREQ_SHUTDOWN_PREPARE;
		power_state_values[1] = VEHICLEAPPOWERSTATESHUTDOWNPARAM_SHUTDOWN_IMMEDIATELY;
	}
	prop_value.int32_values = power_state_values;
	prop_value.n_int32_values = sizeof(power_state_values) / sizeof(power_state_values[0]);

	// Pack the message
	message_size = vhal_proto__emulator_message__get_packed_size(&message);
	if (message_size + sizeof(message_size) > buffer_size) {
		pr_err("message too large to fit in buffer\n");
		ret = -E2BIG;
	} else {
		// Store the message size at the head of the buffer
		memcpy(buffer, &message_size, sizeof(message_size));
		// Pack the message into the buffer after the message size
		vhal_proto__emulator_message__pack(&message, buffer + sizeof(message_size));
	}

	return ret;
}

static uint8_t spidev_transfer_pm(struct vspidev *vspidev, struct virtio_spi_transfer_req *req)
{
	int ret = -EINVAL;
	uint32_t len = *(uint32_t *)req->tx_buf;

	memset(req->rx_buf, 0, req->rx_buf_size);

	if (req->tx_buf_size != VSPI_PM_MSG_SIZE) {
		pr_warn("request length is not 128B\n");
	} else if (len != 0) {
		pr_dbg("write request ignored\n");
	} else {
		ret = vspi_pm_encode_response(req->rx_buf, req->rx_buf_size);
	}

	return ret ? VIRTIO_SPI_TRANS_ERR : VIRTIO_SPI_TRANS_OK;
}

struct vspidev_be vspidev_pm_be = {
	.type = VSPIDEV_TYPE_VMCU,
	.name = "pm",
	.init = spidev_init_pm,
	.deinit = spidev_deinit_pm,
	.transfer = spidev_transfer_pm,
};

static inline int vspi_pm_handler(void *arg, enum vspi_pm_state state)
{
	int ret = 0;

	if (!arg || !vspi_pm.vspidev) {
		ret = -EINVAL;
	} else {
		vspi_pm.state = state;
		vspidev_inject_irq(vspi_pm.vspidev, VIRTIO_SPI_IRQ_STATUS_VALID);
	}

	return ret;
}

static int vspi_pm_stop_handler(void *arg)
{
	pr_info("Got S5 request\n");
	return vspi_pm_handler(arg, S5);
}

static int vspi_pm_suspend_handler(void *arg)
{
	pr_info("Got S3 request\n");
	return vspi_pm_handler(arg, S3);
}

static int vspi_pm_resume_handler(void *arg)
{
	int ret = 0;

	if (!arg) {
		ret = -EINVAL;
	} else {
		vspi_pm.state = NORMAL;
	}

	return ret;
}

static struct monitor_vm_ops vspi_pm_ops = {
	.stop = vspi_pm_stop_handler,
	.suspend = vspi_pm_suspend_handler,
	.resume = vspi_pm_resume_handler,
};

int pm_vspi_init(struct vmctx *ctx)
{
	int ret = 0;

	if (monitor_register_vm_ops(&vspi_pm_ops, ctx, "pm-vspi") == 0) {
		vspi_pm.enabled = true;
	} else {
		pr_err("failed to register vspi_pm_ops\n");
		ret = -EFAULT;
	}

	return ret;
}

void pm_vspi_deinit(struct vmctx *ctx __attribute__((unused)))
{
	vspi_pm.enabled = false;
}
