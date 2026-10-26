// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Local dimming control for Uniwill notebooks with a mini-LED panel.
 *
 * Minimal out-of-tree module. It only exposes a "local_dimming" sysfs
 * attribute on the Uniwill WMI method device. It does not load any other
 * Uniwill feature (hotkeys, fans, battery, lightbar) and writes nothing at
 * load time.
 *
 * The EC register and WMI call come from the RFC patch "platform/x86:
 * uniwill-laptop: Offer support to activate local dimming" by Werner
 * Sembach (TUXEDO), posted 2026-04-21 for the Stellaris 16 Gen7:
 * https://lists.openwall.net/linux-kernel/2026/04/21/1627
 * The EC access and DMI gating follow the mainline uniwill-laptop driver by
 * Armin Wolf (drivers/platform/x86/uniwill/ in Linux v7.0). See README.md
 * for all sources.
 *
 * Copyright (C) 2026 Fabio Gagliardi
 *
 * Tested only on AiStone X6AR559Y, see README.md before use.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wmi.h>

#define EC_HANDLE_PATH			"\\_SB.INOU"
#define EC_ADDR_LOCAL_DIMMING_SUPPORT	0x0D4F
#define LOCAL_DIMMING_SUPPORT		BIT(0)
#define EC_DELAY_US			1000

#define WMI_GUID_BC			"ABBC0F6F-8EA1-11D1-00A0-C90629100000"
#define WMI_DEFAULT_INSTANCE		0x00
#define WMI_DEFAULT_METHOD		0x04
#define WMI_FUNC_SET_FEATURE		0x00000500
#define WMI_FEATURE_LOCAL_DIMMING_OFF	0x0d
#define WMI_FEATURE_LOCAL_DIMMING_ON	0x0e

struct __packed ld_wmi_input {
	u8 args[4];
	__le32 function;
	__le32 reserved[8];
};

struct ld_data {
	struct mutex lock;
	acpi_handle ec_handle;
	bool supported;
	bool user_set;
	bool last;
};

static bool force;
module_param_unsafe(force, bool, 0);
MODULE_PARM_DESC(force, "Skip the DMI check and load on any machine exposing the Uniwill WMI GUID");

static bool allow_unsupported;
module_param_unsafe(allow_unsupported, bool, 0);
MODULE_PARM_DESC(allow_unsupported, "Allow writes even if the EC does not report local dimming support");

static const struct dmi_system_id ld_dmi_table[] = {
	{
		.ident = "AiStone X6AR559Y",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "AiStone"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6AR559Y"),
		},
	},
	{ }
};

static int ld_ec_read(struct ld_data *data, unsigned int reg, unsigned int *val)
{
	union acpi_object params[1] = {
		{ .integer = { .type = ACPI_TYPE_INTEGER, .value = reg } },
	};
	struct acpi_object_list input = { .count = 1, .pointer = params };
	unsigned long long output;
	acpi_status status;

	status = acpi_evaluate_integer(data->ec_handle, "ECRR", &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (output > U8_MAX)
		return -ENXIO;

	usleep_range(EC_DELAY_US, EC_DELAY_US * 2);

	*val = output;

	return 0;
}

static int ld_wmi_set(struct wmi_device *wdev, bool enable)
{
	struct ld_wmi_input input = {
		.function = cpu_to_le32(WMI_FUNC_SET_FEATURE),
	};
	struct wmi_buffer in = { .length = sizeof(input), .data = &input };
	struct wmi_buffer out = { };
	int ret;

	input.args[0] = enable ? WMI_FEATURE_LOCAL_DIMMING_ON : WMI_FEATURE_LOCAL_DIMMING_OFF;

	ret = wmidev_invoke_method(wdev, WMI_DEFAULT_INSTANCE, WMI_DEFAULT_METHOD, &in, &out);
	kfree(out.data);
	if (ret < 0)
		return -EIO;

	return 0;
}

static ssize_t local_dimming_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ld_data *data = dev_get_drvdata(dev);

	guard(mutex)(&data->lock);

	/* The EC does not let us read the state back, only what we last wrote. */
	if (!data->user_set)
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%d\n", data->last);
}

static ssize_t local_dimming_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct ld_data *data = dev_get_drvdata(dev);
	bool enable;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret < 0)
		return ret;

	guard(mutex)(&data->lock);

	if (!data->supported && !allow_unsupported)
		return -EOPNOTSUPP;

	ret = ld_wmi_set(to_wmi_device(dev), enable);
	if (ret < 0)
		return ret;

	data->last = enable;
	data->user_set = true;

	return count;
}
static DEVICE_ATTR_RW(local_dimming);

static ssize_t local_dimming_supported_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct ld_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", data->supported);
}
static DEVICE_ATTR_RO(local_dimming_supported);

static struct attribute *ld_attrs[] = {
	&dev_attr_local_dimming.attr,
	&dev_attr_local_dimming_supported.attr,
	NULL
};
ATTRIBUTE_GROUPS(ld);

static int ld_probe(struct wmi_device *wdev, const void *context)
{
	struct ld_data *data;
	unsigned int value;
	acpi_status status;
	int ret;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = devm_mutex_init(&wdev->dev, &data->lock);
	if (ret < 0)
		return ret;

	status = acpi_get_handle(NULL, EC_HANDLE_PATH, &data->ec_handle);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "ACPI device %s not found\n", EC_HANDLE_PATH);
		return -ENODEV;
	}

	ret = ld_ec_read(data, EC_ADDR_LOCAL_DIMMING_SUPPORT, &value);
	if (ret < 0)
		return ret;

	data->supported = value != U8_MAX && (value & LOCAL_DIMMING_SUPPORT);
	dev_info(&wdev->dev, "EC register 0x%04x = 0x%02x, local dimming %ssupported\n",
		 EC_ADDR_LOCAL_DIMMING_SUPPORT, value, data->supported ? "" : "not ");

	dev_set_drvdata(&wdev->dev, data);

	return 0;
}

/* The firmware turns local dimming back on after suspend, so restore what the user chose. */
static int ld_resume(struct device *dev)
{
	struct ld_data *data = dev_get_drvdata(dev);
	int ret = 0;

	guard(mutex)(&data->lock);

	if (data->user_set)
		ret = ld_wmi_set(to_wmi_device(dev), data->last);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ld_pm_ops, NULL, ld_resume);

/*
 * No MODULE_DEVICE_TABLE(): this GUID is copied from the Windows driver
 * example by many vendors, so the module must never autoload and is only
 * usable after the DMI check (or "force").
 */
static const struct wmi_device_id ld_id_table[] = {
	{ WMI_GUID_BC, NULL },
	{ }
};

static struct wmi_driver ld_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.dev_groups = ld_groups,
		.pm = pm_sleep_ptr(&ld_pm_ops),
	},
	.id_table = ld_id_table,
	.probe = ld_probe,
	.no_notify_data = true,
	.no_singleton = true,
};

static int __init ld_init(void)
{
	if (!force && !dmi_check_system(ld_dmi_table)) {
		pr_info("unsupported device, refusing to load (see README, \"force\" skips this check)\n");
		return -ENODEV;
	}

	return wmi_driver_register(&ld_driver);
}
module_init(ld_init);

static void __exit ld_exit(void)
{
	wmi_driver_unregister(&ld_driver);
}
module_exit(ld_exit);

MODULE_AUTHOR("Fabio Gagliardi");
MODULE_DESCRIPTION("Local dimming control for Uniwill mini-LED notebooks");
MODULE_LICENSE("GPL");
