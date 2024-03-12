#include <errno.h>
#include <stdio.h>
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "usb_accessory/usb_accessory.h"
#include "logging/logging.h"

#pragma mark - Defines

#define USB_ACCESSORY_KERNEL_CONFIG_PATH			"/sys/kernel/config"
#define USB_ACCESSORY_CONFIGURATION					"ACCESSORY"
#define USB_ACCESSORY_GADGET_NAME					"g1"
#define USB_ACCESSORY_FUNCTION_INSTANCE_NAME		"usb0"
#define USB_ACCESSORY_CONFIG_LABEL					"Accessory Config"
#define USB_ACCESSORY_CONFIG_NAME					"accessory.1"
#define USB_ACCESSORY_RECONFIGURE_DELAY_US			(250000)

#pragma mark - Static properties

usb_accessory_data_t usb_accessory_initial = {
    .vid = 0x04E8,
    .pid = 0x6860,
    .serial = "R5CR103RSQE",
    .manufacturer = "Samsung Electronics Co., Ltd.",
    .product = "SAMSUNG_Android"
};

usb_accessory_data_t usb_accessory_configured = {
    .vid = 0x18D1,
    .pid = 0x2D00,
    .serial = "R5CR103RSQE",
    .manufacturer = "Google Inc.",
    .product = "SAMSUNG_Android"
};

pthread_mutex_t usb_accessory_mutex = PTHREAD_MUTEX_INITIALIZER;
usbg_gadget* usb_accessory_gadget = NULL;
const gchar* usb_accessory_log_tag = "Gadget";

#pragma mark - Private methods definition

int usb_accessory_create(usb_accessory_data_t accessory_data);
int usb_accessory_reconfigure(usb_accessory_data_t accessory_data);

#pragma mark - Internal methods implementations

int usb_accessory_create_initial_gadget(void) {
	int ret = -EALREADY;
	
	pthread_mutex_lock(&usb_accessory_mutex);

	if (usb_accessory_gadget) {
		goto exit;
	}

	ret = usb_accessory_create(usb_accessory_initial);

exit:
	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}

int usb_accessory_reset(void) {
	int ret = 0;

	pthread_mutex_lock(&usb_accessory_mutex);

	ret = usb_accessory_reconfigure(usb_accessory_initial);

	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}

int usb_accessory_configure(void) {
	int ret = 0;

	pthread_mutex_lock(&usb_accessory_mutex);

	ret = usb_accessory_reconfigure(usb_accessory_configured);

	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}

#pragma mark - Private methods implementations

int usb_accessory_reconfigure(usb_accessory_data_t accessory_data) {
	usbg_udc *active_udc = NULL;
	int ret = 0;

	if (usb_accessory_gadget == NULL) {
		return -1;
	}

	active_udc = usbg_get_gadget_udc(usb_accessory_gadget);

	if (active_udc) {
		ret = usbg_disable_gadget(usb_accessory_gadget);

		usleep(USB_ACCESSORY_RECONFIGURE_DELAY_US);
	}

	if (ret < 0) { goto error; }

	ret = usbg_set_gadget_vendor_id(usb_accessory_gadget, accessory_data.vid);

	if (ret < 0) { goto error; }

	ret = usbg_set_gadget_product_id(usb_accessory_gadget, accessory_data.pid);

	if (ret < 0) { goto error; }

	ret = usbg_set_gadget_manufacturer(usb_accessory_gadget, LANG_US_ENG, accessory_data.manufacturer);

	if (ret < 0) { goto error; }

	ret =  usbg_enable_gadget(usb_accessory_gadget, DEFAULT_UDC);

	if (ret < 0) { goto error; }

	return ret;

error:
	LOG_E(usb_accessory_log_tag, usbg_strerror(ret));
	return ret;
}

int usb_accessory_create(usb_accessory_data_t accessory_data) {
    usbg_state *s = NULL;
	usbg_gadget *g = NULL;
	usbg_config *c = NULL;
	usbg_function *f_accessory = NULL;
	usbg_gadget *existing_gadget = NULL;
	int ret = -EINVAL;

	struct usbg_gadget_attrs g_attrs = {
		.bcdUSB = 0x0200,
		.bDeviceClass = USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass = 0x00,
		.bDeviceProtocol = 0x00,
		.bMaxPacketSize0 = 64,
		.idVendor = accessory_data.vid,
		.idProduct = accessory_data.pid,
		.bcdDevice = 0x0001,
	};

	struct usbg_gadget_strs g_strs = {
		.serial = accessory_data.serial,
		.manufacturer = accessory_data.manufacturer,
		.product = accessory_data.product
	};

	struct usbg_config_strs c_strs = {
		.configuration = USB_ACCESSORY_CONFIGURATION
	};

	ret = usbg_init(USB_ACCESSORY_KERNEL_CONFIG_PATH, &s);

	if (ret != USBG_SUCCESS) {
		LOG_E(usb_accessory_log_tag, usbg_strerror(ret));

		goto exit;
	}
	
	existing_gadget = usbg_get_gadget(s, USB_ACCESSORY_GADGET_NAME);

	if (existing_gadget) {
		usb_accessory_gadget = existing_gadget;
		ret = 0;

		LOG_I(usb_accessory_log_tag, "Active gadget found!");

		goto exit;
	}

	ret = usbg_create_gadget(s, USB_ACCESSORY_GADGET_NAME, &g_attrs, &g_strs, &g);
	if (ret != USBG_SUCCESS) {
		LOG_E(usb_accessory_log_tag, usbg_strerror(ret));

		goto cleanup;
	}

	ret = usbg_create_function(g, USBG_F_ACCESSORY, USB_ACCESSORY_FUNCTION_INSTANCE_NAME, NULL, &f_accessory);
	if (ret != USBG_SUCCESS) {
		LOG_E(usb_accessory_log_tag, usbg_strerror(ret));

		goto cleanup;
	}

	/* NULL can be passed to use kernel defaults */
	ret = usbg_create_config(g, 1, USB_ACCESSORY_CONFIG_LABEL, NULL, &c_strs, &c);
	if (ret != USBG_SUCCESS) {
		LOG_E(usb_accessory_log_tag, usbg_strerror(ret));

		goto cleanup;
	}

	ret = usbg_add_config_function(c, USB_ACCESSORY_CONFIG_NAME, f_accessory);
	if (ret != USBG_SUCCESS) {
		LOG_E(usb_accessory_log_tag, usbg_strerror(ret));

		goto cleanup;
	}

	usb_accessory_gadget = g;

	ret = 0;

	LOG_I(usb_accessory_log_tag, "Created!");

	goto exit;

cleanup:
	usbg_cleanup(s);

exit:
	return ret;
}
