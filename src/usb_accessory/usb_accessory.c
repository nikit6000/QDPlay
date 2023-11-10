#include <errno.h>
#include <stdio.h>
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <stdlib.h>
#include <string.h>
#include "usb_accessory/usb_accessory.h"

#pragma mark - Defines

#define USB_ACCESSORY_KERNEL_CONFIG_PATH       "/sys/kernel/config"
#define USB_ACCESSORY_CONFIGURATION            "ACCESSORY"
#define USB_ACCESSORY_GADGET_NAME              "g1"
#define USB_ACCESSORY_FUNCTION_INSTANCE_NAME   "usb0"
#define USB_ACCESSORY_CONFIG_LABEL             "Accessory Config"
#define USB_ACCESSORY_CONFIG_NAME              "accessory.1"

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

#pragma mark - Private methods definition

int usb_accessory_remove(usbg_gadget *g);

#pragma mark - Internal methods implementations

int usb_accessory_create(usb_accessory_data_t accessory_data) {
    usbg_state *s;
	usbg_gadget *g;
	usbg_config *c;
	usbg_function *f_accessory;
	int ret = -EINVAL;
	int usbg_ret;

    // Remove all devices
    ret = usb_accessory_remove_all();

    // Register new
	struct usbg_gadget_attrs g_attrs = {
		.bcdUSB = 0x0200,
		.bDeviceClass = USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass = 0x00,
		.bDeviceProtocol = 0x00,
		.bMaxPacketSize0 = 512,
		.idVendor = accessory_data.vid,
		.idProduct = accessory_data.pid,
		.bcdDevice = 0x0001,
	};

	struct usbg_gadget_strs g_strs = {
		.serial = accessory_data.serial,		            /* Serial number */
		.manufacturer = accessory_data.manufacturer,        /* Manufacturer */
		.product = accessory_data.product		            /* Product string */
	};

	struct usbg_config_strs c_strs = {
		.configuration = USB_ACCESSORY_CONFIGURATION
	};

	usbg_ret = usbg_init(USB_ACCESSORY_KERNEL_CONFIG_PATH, &s);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on USB gadget init\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out1;
	}

	usbg_ret = usbg_create_gadget(s, USB_ACCESSORY_GADGET_NAME, &g_attrs, &g_strs, &g);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on create gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out2;
	}

	usbg_ret = usbg_create_function(g, USBG_F_ACCESSORY, USB_ACCESSORY_FUNCTION_INSTANCE_NAME, NULL, &f_accessory);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating accessory function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out2;
	}

	/* NULL can be passed to use kernel defaults */
	usbg_ret = usbg_create_config(g, 1, USB_ACCESSORY_CONFIG_LABEL, NULL, &c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating config\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out2;
	}

	usbg_ret = usbg_add_config_function(c, USB_ACCESSORY_CONFIG_NAME, f_accessory);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out2;
	}

	usbg_ret = usbg_enable_gadget(g, DEFAULT_UDC);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error enabling gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out2;
	}

	ret = 0;

out2:
	usbg_cleanup(s);

out1:
	return ret;
}

#pragma mark - Private methods implementations

int usb_accessory_remove(usbg_gadget *g) {

	int usbg_ret;
	usbg_udc *u;

	/* Check if gadget is enabled */
	u = usbg_get_gadget_udc(g);

	/* If gadget is enable we have to disable it first */
	if (u) {
		usbg_ret = usbg_disable_gadget(g);
		if (usbg_ret != USBG_SUCCESS) {
			fprintf(stderr, "Error on USB disable gadget udc\n");
			fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));
			goto out;
		}
	}

	/* Remove gadget with USBG_RM_RECURSE flag to remove
	 * also its configurations, functions and strings */
	usbg_ret = usbg_rm_gadget(g, USBG_RM_RECURSE);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on USB gadget remove\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));
	}

out:
	return usbg_ret;
}

int usb_accessory_remove_all(void) {
    int usbg_ret;
	int ret = -EINVAL;
	usbg_state *s;
	usbg_gadget *g;
	struct usbg_gadget_attrs g_attrs;
	char *cp;
    
    usbg_ret = usbg_init("/sys/kernel/config", &s);
	
    if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on USB state init\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

		goto out1;
	}

	g = usbg_get_first_gadget(s);
	while (g != NULL) {
		/* Get current gadget attrs to be compared */
		usbg_ret = usbg_get_gadget_attrs(g, &g_attrs);
		if (usbg_ret != USBG_SUCCESS) {
			fprintf(stderr, "Error on USB get gadget attrs\n");
			fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret), usbg_strerror(usbg_ret));

			goto out2;
		}

		usbg_gadget *g_next = usbg_get_next_gadget(g);

        usbg_ret = usb_accessory_remove(g);

        if (usbg_ret != USBG_SUCCESS)
            goto out2;

        g = g_next;
	}

	ret = 0;

out2:
	usbg_cleanup(s);
out1:
	return ret;
}