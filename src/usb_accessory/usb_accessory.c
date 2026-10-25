#include <errno.h>
#include <stdio.h>
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "usb_accessory/usb_accessory.h"
#include "logging/logging.h"

#pragma mark - Defines

#define USB_ACCESSORY_KERNEL_CONFIG_PATH			"/sys/kernel/config"
#define USB_ACCESSORY_CONFIGURATION					"ACCESSORY"
#define USB_ACCESSORY_GADGET_NAME					"g1"
#define USB_ACCESSORY_FUNCTION_INSTANCE_NAME		"usb0"
#define USB_ACCESSORY_CONFIG_LABEL					"Accessory Config"
#define USB_ACCESSORY_CONFIG_NAME					"accessory.1"
// Time for the host to notice the disconnect before the device comes back
#define USB_ACCESSORY_RECONFIGURE_DELAY_US			(250000)

#pragma mark - Static properties

static const usb_accessory_data_t usb_accessory_identities[] = {
	[USB_ACCESSORY_MODE_INITIAL] = {
		.vid = 0x04E8,
		.pid = 0x6860,
		.serial = "R5CR103RSQE",
		.manufacturer = "Samsung Electronics Co., Ltd.",
		.product = "SAMSUNG_Android"
	},
	[USB_ACCESSORY_MODE_ACCESSORY] = {
		.vid = 0x18D1,
		.pid = 0x2D00,
		.serial = "R5CR103RSQE",
		.manufacturer = "Google Inc.",
		.product = "SAMSUNG_Android"
	}
};

static pthread_mutex_t usb_accessory_mutex = PTHREAD_MUTEX_INITIALIZER;
static usbg_state* usb_accessory_state = NULL;
static usbg_gadget* usb_accessory_gadget = NULL;
static const gchar* usb_accessory_log_tag = "Gadget";

#pragma mark - Private methods

// usleep() is cut short by the wakeup signal of the session thread, sleep the whole time
static void usb_accessory_sleep_us(long us) {
	struct timespec request = { .tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000 };
	struct timespec remaining;

	while (nanosleep(&request, &remaining) < 0 && errno == EINTR) {
		request = remaining;
	}
}

static void usb_accessory_log_error(const char* what, int ret) {
	LOG_E(usb_accessory_log_tag, "%s: %s (%d)", what, usbg_strerror(ret), ret);
}

static int usb_accessory_disable_locked(void) {
	int ret = 0;

	if (usb_accessory_gadget == NULL) {
		return -ENODEV;
	}

	// Disabling an already detached gadget is not an error for us

	if (usbg_get_gadget_udc(usb_accessory_gadget) != NULL) {
		ret = usbg_disable_gadget(usb_accessory_gadget);

		if (ret != USBG_SUCCESS) {
			usb_accessory_log_error("Can`t disable gadget", ret);
		}

		usb_accessory_sleep_us(USB_ACCESSORY_RECONFIGURE_DELAY_US);
	}

	return ret;
}

static int usb_accessory_create_locked(void) {
	usbg_gadget *g = NULL;
	usbg_config *c = NULL;
	usbg_function *f_accessory = NULL;
	usbg_gadget *existing_gadget = NULL;
	const usb_accessory_data_t *identity = &usb_accessory_identities[USB_ACCESSORY_MODE_INITIAL];
	int ret;

	struct usbg_gadget_attrs g_attrs = {
		.bcdUSB = 0x0200,
		.bDeviceClass = USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass = 0x00,
		.bDeviceProtocol = 0x00,
		.bMaxPacketSize0 = 64,
		.idVendor = identity->vid,
		.idProduct = identity->pid,
		.bcdDevice = 0x0001,
	};

	struct usbg_gadget_strs g_strs = {
		.serial = (char*)identity->serial,
		.manufacturer = (char*)identity->manufacturer,
		.product = (char*)identity->product
	};

	struct usbg_config_strs c_strs = {
		.configuration = USB_ACCESSORY_CONFIGURATION
	};

	ret = usbg_init(USB_ACCESSORY_KERNEL_CONFIG_PATH, &usb_accessory_state);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t open configfs", ret);

		return ret;
	}

	// A gadget left by a previous run (crash, SIGKILL) may be bound or half configured:
	// always start from a clean one

	existing_gadget = usbg_get_gadget(usb_accessory_state, USB_ACCESSORY_GADGET_NAME);

	if (existing_gadget) {
		LOG_I(usb_accessory_log_tag, "Removing stale gadget");

		if (usbg_get_gadget_udc(existing_gadget)) {
			usbg_disable_gadget(existing_gadget);
		}

		ret = usbg_rm_gadget(existing_gadget, USBG_RM_RECURSE);

		if (ret != USBG_SUCCESS) {
			usb_accessory_log_error("Can`t remove stale gadget", ret);

			goto cleanup;
		}
	}

	ret = usbg_create_gadget(usb_accessory_state, USB_ACCESSORY_GADGET_NAME, &g_attrs, &g_strs, &g);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t create gadget", ret);

		goto cleanup;
	}

	ret = usbg_create_function(g, USBG_F_ACCESSORY, USB_ACCESSORY_FUNCTION_INSTANCE_NAME, NULL, &f_accessory);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t create accessory function (is usb_f_accessory loaded?)", ret);

		goto cleanup;
	}

	ret = usbg_create_config(g, 1, USB_ACCESSORY_CONFIG_LABEL, NULL, &c_strs, &c);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t create config", ret);

		goto cleanup;
	}

	ret = usbg_add_config_function(c, USB_ACCESSORY_CONFIG_NAME, f_accessory);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t add function to config", ret);

		goto cleanup;
	}

	usb_accessory_gadget = g;

	LOG_I(usb_accessory_log_tag, "Gadget created");

	return 0;

cleanup:
	usbg_cleanup(usb_accessory_state);
	usb_accessory_state = NULL;

	return ret;
}

#pragma mark - Internal methods implementations

int usb_accessory_init(void) {
	int ret = 0;

	pthread_mutex_lock(&usb_accessory_mutex);

	if (usb_accessory_gadget == NULL) {
		ret = usb_accessory_create_locked();
	}

	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}

int usb_accessory_set_mode(usb_accessory_mode_t mode) {
	const usb_accessory_data_t *identity = &usb_accessory_identities[mode];
	usbg_udc *udc = NULL;
	int ret;

	pthread_mutex_lock(&usb_accessory_mutex);

	// Creation may have failed at startup (configfs not mounted, module still loading)
	if (usb_accessory_gadget == NULL && (ret = usb_accessory_create_locked()) != 0) {
		pthread_mutex_unlock(&usb_accessory_mutex);

		return ret < 0 ? ret : -ENODEV;
	}

	usb_accessory_disable_locked();

	// The UDC driver (musb) may still be loading during boot. libusbgx reads the UDC list
	// only in usbg_init(), so re-create the state to see a controller that appeared later.
	udc = usbg_get_first_udc(usb_accessory_state);

	if (udc == NULL) {
		usbg_cleanup(usb_accessory_state);
		usb_accessory_state = NULL;
		usb_accessory_gadget = NULL;

		if (usb_accessory_create_locked() == 0) {
			udc = usbg_get_first_udc(usb_accessory_state);
		}
	}

	if (udc == NULL || usb_accessory_gadget == NULL) {
		LOG_W(usb_accessory_log_tag, "No USB device controller yet");

		pthread_mutex_unlock(&usb_accessory_mutex);

		return -EAGAIN;
	}

	if ((ret = usbg_set_gadget_vendor_id(usb_accessory_gadget, identity->vid)) != USBG_SUCCESS ||
		(ret = usbg_set_gadget_product_id(usb_accessory_gadget, identity->pid)) != USBG_SUCCESS ||
		(ret = usbg_set_gadget_manufacturer(usb_accessory_gadget, LANG_US_ENG, identity->manufacturer)) != USBG_SUCCESS ||
		(ret = usbg_set_gadget_product(usb_accessory_gadget, LANG_US_ENG, identity->product)) != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t set gadget identity", ret);

		pthread_mutex_unlock(&usb_accessory_mutex);

		return ret;
	}

	ret = usbg_enable_gadget(usb_accessory_gadget, udc);

	if (ret != USBG_SUCCESS) {
		usb_accessory_log_error("Can`t enable gadget", ret);
	} else {
		LOG_I(usb_accessory_log_tag, "Enumerating as %04x:%04x (%s)", identity->vid, identity->pid,
			mode == USB_ACCESSORY_MODE_ACCESSORY ? "accessory" : "initial");
	}

	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}

int usb_accessory_disable(void) {
	int ret;

	pthread_mutex_lock(&usb_accessory_mutex);

	ret = usb_accessory_disable_locked();

	pthread_mutex_unlock(&usb_accessory_mutex);

	return ret;
}
