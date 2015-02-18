/*
 *  Force feedback support for Logitech Gaming Wheels
 *
 *  Including G27, G25, DFP, DFGT, FFEX, Momo, Momo2 &
 *  Speed Force Wireless (WiiWheel)
 *
 *  Copyright (c) 2010 Simon Wood <simon@mungewell.org>
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include <linux/input.h>
#include <linux/usb.h>
#include <linux/hid.h>

#include "usbhid/usbhid.h"
#include "hid-lg.h"
#include "hid-lg4ff.h"
#include "hid-ids.h"

#define to_hid_device(pdev) container_of(pdev, struct hid_device, dev)

#define LG4FF_MMODE_IS_MULTIMODE 0
#define LG4FF_MMODE_SWITCHED 1
#define LG4FF_MMODE_NOT_MULTIMODE 2

#define LG4FF_MODE_NATIVE_IDX 0
#define LG4FF_MODE_DFEX_IDX 1
#define LG4FF_MODE_DFP_IDX 2
#define LG4FF_MODE_G25_IDX 3
#define LG4FF_MODE_DFGT_IDX 4
#define LG4FF_MODE_G27_IDX 5
#define LG4FF_MODE_MAX_IDX 6

#define LG4FF_MODE_NATIVE BIT(LG4FF_MODE_NATIVE_IDX)
#define LG4FF_MODE_DFEX BIT(LG4FF_MODE_DFEX_IDX)
#define LG4FF_MODE_DFP BIT(LG4FF_MODE_DFP_IDX)
#define LG4FF_MODE_G25 BIT(LG4FF_MODE_G25_IDX)
#define LG4FF_MODE_DFGT BIT(LG4FF_MODE_DFGT_IDX)
#define LG4FF_MODE_G27 BIT(LG4FF_MODE_G27_IDX)

#define LG4FF_DFEX_TAG "DF-EX"
#define LG4FF_DFEX_NAME "Driving Force / Formula EX"
#define LG4FF_DFP_TAG "DFP"
#define LG4FF_DFP_NAME "Driving Force Pro"
#define LG4FF_G25_TAG "G25"
#define LG4FF_G25_NAME "G25 Racing Wheel"
#define LG4FF_G27_TAG "G27"
#define LG4FF_G27_NAME "G27 Racing Wheel"
#define LG4FF_DFGT_TAG "DFGT"
#define LG4FF_DFGT_NAME "Driving Force GT"

#define LG4FF_FFEX_REV_MAJ 0x21
#define LG4FF_FFEX_REV_MIN 0x00

static void hid_lg4ff_set_range_dfp(struct hid_device *hid, u16 range);
static void hid_lg4ff_set_range_g25(struct hid_device *hid, u16 range);

struct lg4ff_device_entry {
	__u32 product_id;
	__u16 range;
	__u16 min_range;
	__u16 max_range;
#ifdef CONFIG_LEDS_CLASS
	__u8  led_state;
	struct led_classdev *led[5];
#endif
	u32 alternate_modes;
	const char *real_tag;
	const char *real_name;
	u16 real_product_id;
	struct list_head list;
	void (*set_range)(struct hid_device *hid, u16 range);
};

static const signed short lg4ff_wheel_effects[] = {
	FF_CONSTANT,
	FF_AUTOCENTER,
	-1
};

struct lg4ff_wheel {
	const __u32 product_id;
	const signed short *ff_effects;
	const __u16 min_range;
	const __u16 max_range;
	void (*set_range)(struct hid_device *hid, u16 range);
};

struct lg4ff_compat_mode_switch {
	const __u8 cmd_count;	/* Number of commands to send */
	const __u8 cmd[];
};

struct lg4ff_wheel_ident_info {
	const u16 mask;
	const u16 result;
	const u16 real_product_id;
};

struct lg4ff_wheel_ident_checklist {
	const u32 count;
	const struct lg4ff_wheel_ident_info *models[];
};

struct lg4ff_multimode_wheel {
	const u16 product_id;
	const u32 alternate_modes;
	const char *real_tag;
	const char *real_name;
};

struct lg4ff_alternate_mode {
	const u16 product_id;
	const char *tag;
	const char *name;
};

static const struct lg4ff_wheel lg4ff_devices[] = {
	{USB_DEVICE_ID_LOGITECH_WHEEL,       lg4ff_wheel_effects, 40, 270, NULL},
	{USB_DEVICE_ID_LOGITECH_MOMO_WHEEL,  lg4ff_wheel_effects, 40, 270, NULL},
	{USB_DEVICE_ID_LOGITECH_DFP_WHEEL,   lg4ff_wheel_effects, 40, 900, hid_lg4ff_set_range_dfp},
	{USB_DEVICE_ID_LOGITECH_G25_WHEEL,   lg4ff_wheel_effects, 40, 900, hid_lg4ff_set_range_g25},
	{USB_DEVICE_ID_LOGITECH_DFGT_WHEEL,  lg4ff_wheel_effects, 40, 900, hid_lg4ff_set_range_g25},
	{USB_DEVICE_ID_LOGITECH_G27_WHEEL,   lg4ff_wheel_effects, 40, 900, hid_lg4ff_set_range_g25},
	{USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2, lg4ff_wheel_effects, 40, 270, NULL},
	{USB_DEVICE_ID_LOGITECH_WII_WHEEL,   lg4ff_wheel_effects, 40, 270, NULL}
};

static const struct lg4ff_multimode_wheel lg4ff_multimode_wheels[] = {
	{USB_DEVICE_ID_LOGITECH_DFP_WHEEL,
	 LG4FF_MODE_NATIVE | LG4FF_MODE_DFP | LG4FF_MODE_DFEX,
	 LG4FF_DFP_TAG, LG4FF_DFP_NAME},
	{USB_DEVICE_ID_LOGITECH_G25_WHEEL,
	 LG4FF_MODE_NATIVE | LG4FF_MODE_G25 | LG4FF_MODE_DFP | LG4FF_MODE_DFEX,
	 LG4FF_G25_TAG, LG4FF_G25_NAME},
	{USB_DEVICE_ID_LOGITECH_DFGT_WHEEL,
	 LG4FF_MODE_NATIVE | LG4FF_MODE_DFGT | LG4FF_MODE_DFP | LG4FF_MODE_DFEX,
	 LG4FF_DFGT_TAG, LG4FF_DFGT_NAME},
	{USB_DEVICE_ID_LOGITECH_G27_WHEEL,
	 LG4FF_MODE_NATIVE | LG4FF_MODE_G27 | LG4FF_MODE_G25 | LG4FF_MODE_DFP | LG4FF_MODE_DFEX,
	 LG4FF_G27_TAG, LG4FF_G27_NAME},
};

static const struct lg4ff_alternate_mode lg4ff_alternate_modes[] = {
	[LG4FF_MODE_NATIVE_IDX] = {0, "native", ""},
	[LG4FF_MODE_DFEX_IDX] = {USB_DEVICE_ID_LOGITECH_WHEEL, LG4FF_DFEX_TAG, LG4FF_DFEX_NAME},
	[LG4FF_MODE_DFP_IDX] = {USB_DEVICE_ID_LOGITECH_DFP_WHEEL, LG4FF_DFP_TAG, LG4FF_DFP_NAME},
	[LG4FF_MODE_G25_IDX] = {USB_DEVICE_ID_LOGITECH_G25_WHEEL, LG4FF_G25_TAG, LG4FF_G25_NAME},
	[LG4FF_MODE_DFGT_IDX] = {USB_DEVICE_ID_LOGITECH_DFGT_WHEEL, LG4FF_DFGT_TAG, LG4FF_DFGT_NAME},
	[LG4FF_MODE_G27_IDX] = {USB_DEVICE_ID_LOGITECH_G27_WHEEL, LG4FF_G27_TAG, LG4FF_G27_NAME}
};

/* Multimode wheel identificators */
static const struct lg4ff_wheel_ident_info lg4ff_dfp_ident_info = {
	0xf000,
	0x1000,
	USB_DEVICE_ID_LOGITECH_DFP_WHEEL
};

static const struct lg4ff_wheel_ident_info lg4ff_g25_ident_info = {
	0xff00,
	0x1200,
	USB_DEVICE_ID_LOGITECH_G25_WHEEL
};

static const struct lg4ff_wheel_ident_info lg4ff_g27_ident_info = {
	0xfff0,
	0x1230,
	USB_DEVICE_ID_LOGITECH_G27_WHEEL
};

static const struct lg4ff_wheel_ident_info lg4ff_dfgt_ident_info = {
	0xff00,
	0x1300,
	USB_DEVICE_ID_LOGITECH_DFGT_WHEEL
};

/* Multimode wheel identification checklists */
static const struct lg4ff_wheel_ident_checklist lg4ff_main_checklist = {
	4,
	{&lg4ff_dfgt_ident_info,
	 &lg4ff_g27_ident_info,
	 &lg4ff_g25_ident_info,
	 &lg4ff_dfp_ident_info}
};

/* Compatibility mode switching commands */
static const struct lg4ff_compat_mode_switch lg4ff_mode_switch_dfp = {
	1,
	{0xf8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static const struct lg4ff_compat_mode_switch lg4ff_mode_switch_dfgt = {
	2,
	{0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 1st command */
	 0xf8, 0x09, 0x03, 0x01, 0x00, 0x00, 0x00}	/* 2nd command */
};

static const struct lg4ff_compat_mode_switch lg4ff_mode_switch_g25 = {
	1,
	{0xf8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}
};

static const struct lg4ff_compat_mode_switch lg4ff_mode_switch_g27 = {
	2,
	{0xf8, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00,	/* 1st command */
	 0xf8, 0x09, 0x04, 0x01, 0x00, 0x00, 0x00}	/* 2nd command */
};

/* Recalculates X axis value accordingly to currently selected range */
static __s32 lg4ff_adjust_dfp_x_axis(__s32 value, __u16 range)
{
	__u16 max_range;
	__s32 new_value;

	if (range == 900)
		return value;
	else if (range == 200)
		return value;
	else if (range < 200)
		max_range = 200;
	else
		max_range = 900;

	new_value = 8192 + mult_frac(value - 8192, max_range, range);
	if (new_value < 0)
		return 0;
	else if (new_value > 16383)
		return 16383;
	else
		return new_value;
}

int lg4ff_adjust_input_event(struct hid_device *hid, struct hid_field *field,
			     struct hid_usage *usage, __s32 value, struct lg_drv_data *drv_data)
{
	struct lg4ff_device_entry *entry = drv_data->device_props;
	__s32 new_value = 0;

	if (!entry) {
		hid_err(hid, "Device properties not found");
		return 0;
	}

	switch (entry->product_id) {
	case USB_DEVICE_ID_LOGITECH_DFP_WHEEL:
		switch (usage->code) {
		case ABS_X:
			new_value = lg4ff_adjust_dfp_x_axis(value, entry->range);
			input_event(field->hidinput->input, usage->type, usage->code, new_value);
			return 1;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int hid_lg4ff_play(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;
	int x;

#define CLAMP(x) do { if (x < 0) x = 0; else if (x > 0xff) x = 0xff; } while (0)

	switch (effect->type) {
	case FF_CONSTANT:
		x = effect->u.ramp.start_level + 0x80;	/* 0x80 is no force */
		CLAMP(x);

		if (x == 0x80) {
			/* De-activate force in slot-1*/
			value[0] = 0x13;
			value[1] = 0x00;
			value[2] = 0x00;
			value[3] = 0x00;
			value[4] = 0x00;
			value[5] = 0x00;
			value[6] = 0x00;

			hid_hw_request(hid, report, HID_REQ_SET_REPORT);
			return 0;
		}

		value[0] = 0x11;	/* Slot 1 */
		value[1] = 0x08;
		value[2] = x;
		value[3] = 0x80;
		value[4] = 0x00;
		value[5] = 0x00;
		value[6] = 0x00;

		hid_hw_request(hid, report, HID_REQ_SET_REPORT);
		break;
	}
	return 0;
}

/* Sends default autocentering command compatible with
 * all wheels except Formula Force EX */
static void hid_lg4ff_set_autocenter_default(struct input_dev *dev, u16 magnitude)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;
	__u32 expand_a, expand_b;
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Private driver data not found!\n");
		return;
	}

	entry = drv_data->device_props;
	if (!entry) {
		hid_err(hid, "Device properties not found!\n");
		return;
	}

	/* De-activate Auto-Center */
	if (magnitude == 0) {
		value[0] = 0xf5;
		value[1] = 0x00;
		value[2] = 0x00;
		value[3] = 0x00;
		value[4] = 0x00;
		value[5] = 0x00;
		value[6] = 0x00;

		hid_hw_request(hid, report, HID_REQ_SET_REPORT);
		return;
	}

	if (magnitude <= 0xaaaa) {
		expand_a = 0x0c * magnitude;
		expand_b = 0x80 * magnitude;
	} else {
		expand_a = (0x0c * 0xaaaa) + 0x06 * (magnitude - 0xaaaa);
		expand_b = (0x80 * 0xaaaa) + 0xff * (magnitude - 0xaaaa);
	}

	/* Adjust for non-MOMO wheels */
	switch (entry->product_id) {
	case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL:
	case USB_DEVICE_ID_LOGITECH_MOMO_WHEEL2:
		break;
	default:
		expand_a = expand_a >> 1;
		break;
	}

	value[0] = 0xfe;
	value[1] = 0x0d;
	value[2] = expand_a / 0xaaaa;
	value[3] = expand_a / 0xaaaa;
	value[4] = expand_b / 0xaaaa;
	value[5] = 0x00;
	value[6] = 0x00;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);

	/* Activate Auto-Center */
	value[0] = 0x14;
	value[1] = 0x00;
	value[2] = 0x00;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);
}

/* Sends autocentering command compatible with Formula Force EX */
static void hid_lg4ff_set_autocenter_ffex(struct input_dev *dev, u16 magnitude)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;
	magnitude = magnitude * 90 / 65535;

	value[0] = 0xfe;
	value[1] = 0x03;
	value[2] = magnitude >> 14;
	value[3] = magnitude >> 14;
	value[4] = magnitude;
	value[5] = 0x00;
	value[6] = 0x00;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);
}

/* Sends command to set range compatible with G25/G27/Driving Force GT */
static void hid_lg4ff_set_range_g25(struct hid_device *hid, u16 range)
{
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;

	dbg_hid("G25/G27/DFGT: setting range to %u\n", range);

	value[0] = 0xf8;
	value[1] = 0x81;
	value[2] = range & 0x00ff;
	value[3] = (range & 0xff00) >> 8;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);
}

/* Sends commands to set range compatible with Driving Force Pro wheel */
static void hid_lg4ff_set_range_dfp(struct hid_device *hid, __u16 range)
{
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	int start_left, start_right, full_range;
	__s32 *value = report->field[0]->value;

	dbg_hid("Driving Force Pro: setting range to %u\n", range);

	/* Prepare "coarse" limit command */
	value[0] = 0xf8;
	value[1] = 0x00;	/* Set later */
	value[2] = 0x00;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;

	if (range > 200) {
		report->field[0]->value[1] = 0x03;
		full_range = 900;
	} else {
		report->field[0]->value[1] = 0x02;
		full_range = 200;
	}
	hid_hw_request(hid, report, HID_REQ_SET_REPORT);

	/* Prepare "fine" limit command */
	value[0] = 0x81;
	value[1] = 0x0b;
	value[2] = 0x00;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;

	if (range == 200 || range == 900) {	/* Do not apply any fine limit */
		hid_hw_request(hid, report, HID_REQ_SET_REPORT);
		return;
	}

	/* Construct fine limit command */
	start_left = (((full_range - range + 1) * 2047) / full_range);
	start_right = 0xfff - start_left;

	value[2] = start_left >> 4;
	value[3] = start_right >> 4;
	value[4] = 0xff;
	value[5] = (start_right & 0xe) << 4 | (start_left & 0xe);
	value[6] = 0xff;

	hid_hw_request(hid, report, HID_REQ_SET_REPORT);
}

static int lg4ff_switch_compatibility_mode(struct hid_device *hid, const struct lg4ff_compat_mode_switch *s)
{
	struct usb_device *usbdev = hid_to_usb_dev(hid);
	struct usbhid_device *usbhid = hid->driver_data;
	u8 i;

	for (i = 0; i < s->cmd_count; i++) {
		int xferd, ret;
		u8 data[7];

		memcpy(data, s->cmd + (7*i), 7);
		ret = usb_interrupt_msg(usbdev, usbhid->urbout->pipe, data, 7, &xferd, USB_CTRL_SET_TIMEOUT);
		if (ret)
			return ret;
	}
	return 0;
}

static ssize_t lg4ff_alternate_modes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hid_device *hid = to_hid_device(dev);
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;
	ssize_t count = 0;
	int i;

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Private driver data not found!\n");
		return 0;
	}

	entry = drv_data->device_props;
	if (!entry) {
		hid_err(hid, "Device properties not found!\n");
		return 0;
	}

	if (!entry->real_name) {
		hid_err(hid, "NULL pointer to string\n");
		return 0;
	}

	for (i = 0; i < LG4FF_MODE_MAX_IDX; i++) {
		if (entry->alternate_modes & BIT(i)) {
			/* Print tag and full name */
			count += scnprintf(buf + count, PAGE_SIZE - count, "%s: %s",
					   lg4ff_alternate_modes[i].tag,
					   !lg4ff_alternate_modes[i].product_id ? entry->real_name : lg4ff_alternate_modes[i].name);
			if (count >= PAGE_SIZE - 1)
				return count;

			/* Mark the currently active mode with an asterisk */
			if (lg4ff_alternate_modes[i].product_id == entry->product_id ||
			    (lg4ff_alternate_modes[i].product_id == 0 && entry->product_id == entry->real_product_id))
				count += scnprintf(buf + count, PAGE_SIZE - count, " *\n");
			else
				count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

			if (count >= PAGE_SIZE - 1)
				return count;
		}
	}

	return count;
}

static ssize_t lg4ff_alternate_modes_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return -ENOSYS;
}
static DEVICE_ATTR(alternate_modes, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH, lg4ff_alternate_modes_show, lg4ff_alternate_modes_store);

/* Read current range and display it in terminal */
static ssize_t range_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct hid_device *hid = to_hid_device(dev);
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;
	size_t count;

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Private driver data not found!\n");
		return 0;
	}

	entry = drv_data->device_props;
	if (!entry) {
		hid_err(hid, "Device properties not found!\n");
		return 0;
	}

	count = scnprintf(buf, PAGE_SIZE, "%u\n", entry->range);
	return count;
}

/* Set range to user specified value, call appropriate function
 * according to the type of the wheel */
static ssize_t range_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct hid_device *hid = to_hid_device(dev);
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;
	__u16 range = simple_strtoul(buf, NULL, 10);

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Private driver data not found!\n");
		return -EINVAL;
	}

	entry = drv_data->device_props;
	if (!entry) {
		hid_err(hid, "Device properties not found!\n");
		return -EINVAL;
	}

	if (range == 0)
		range = entry->max_range;

	/* Check if the wheel supports range setting
	 * and that the range is within limits for the wheel */
	if (entry->set_range != NULL && range >= entry->min_range && range <= entry->max_range) {
		entry->set_range(hid, range);
		entry->range = range;
	}

	return count;
}
static DEVICE_ATTR_RW(range);

static ssize_t lg4ff_real_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hid_device *hid = to_hid_device(dev);
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;
	size_t count;

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Private driver data not found!\n");
		return 0;
	}

	entry = drv_data->device_props;
	if (!entry) {
		hid_err(hid, "Device properties not found!\n");
		return 0;
	}

	if (!entry->real_tag || !entry->real_name) {
		hid_err(hid, "NULL pointer to string\n");
		return 0;
	}

	count = scnprintf(buf, PAGE_SIZE, "%s: %s\n", entry->real_tag, entry->real_name);
	return count;
}

static ssize_t lg4ff_real_id_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	/* Real ID is a read-only value */
	return -EPERM;
}
static DEVICE_ATTR(real_id, S_IRUGO, lg4ff_real_id_show, lg4ff_real_id_store);

#ifdef CONFIG_LEDS_CLASS
static void lg4ff_set_leds(struct hid_device *hid, __u8 leds)
{
	struct list_head *report_list = &hid->report_enum[HID_OUTPUT_REPORT].report_list;
	struct hid_report *report = list_entry(report_list->next, struct hid_report, list);
	__s32 *value = report->field[0]->value;

	value[0] = 0xf8;
	value[1] = 0x12;
	value[2] = leds;
	value[3] = 0x00;
	value[4] = 0x00;
	value[5] = 0x00;
	value[6] = 0x00;
	hid_hw_request(hid, report, HID_REQ_SET_REPORT);
}

static void lg4ff_led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	struct lg_drv_data *drv_data = hid_get_drvdata(hid);
	struct lg4ff_device_entry *entry;
	int i, state = 0;

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return;
	}

	entry = (struct lg4ff_device_entry *)drv_data->device_props;

	if (!entry) {
		hid_err(hid, "Device properties not found.");
		return;
	}

	for (i = 0; i < 5; i++) {
		if (led_cdev != entry->led[i])
			continue;
		state = (entry->led_state >> i) & 1;
		if (value == LED_OFF && state) {
			entry->led_state &= ~(1 << i);
			lg4ff_set_leds(hid, entry->led_state);
		} else if (value != LED_OFF && !state) {
			entry->led_state |= 1 << i;
			lg4ff_set_leds(hid, entry->led_state);
		}
		break;
	}
}

static enum led_brightness lg4ff_led_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hid = container_of(dev, struct hid_device, dev);
	struct lg_drv_data *drv_data = hid_get_drvdata(hid);
	struct lg4ff_device_entry *entry;
	int i, value = 0;

	if (!drv_data) {
		hid_err(hid, "Device data not found.");
		return LED_OFF;
	}

	entry = (struct lg4ff_device_entry *)drv_data->device_props;

	if (!entry) {
		hid_err(hid, "Device properties not found.");
		return LED_OFF;
	}

	for (i = 0; i < 5; i++)
		if (led_cdev == entry->led[i]) {
			value = (entry->led_state >> i) & 1;
			break;
		}

	return value ? LED_FULL : LED_OFF;
}
#endif

static u16 lg4ff_identify_multimode_wheel(struct hid_device *hid, const u16 reported_product_id, const u16 bcdDevice)
{
	const struct lg4ff_wheel_ident_checklist *checklist;
	int i, from_idx, to_idx;

	switch (reported_product_id) {
	case USB_DEVICE_ID_LOGITECH_WHEEL:
	case USB_DEVICE_ID_LOGITECH_DFP_WHEEL:
		checklist = &lg4ff_main_checklist;
		from_idx = 0;
		to_idx = checklist->count - 1;
		break;
	case USB_DEVICE_ID_LOGITECH_G25_WHEEL:
		checklist = &lg4ff_main_checklist;
		from_idx = 0;
		to_idx = checklist->count - 2; /* End identity check at G25 */
		break;
	case USB_DEVICE_ID_LOGITECH_G27_WHEEL:
		checklist = &lg4ff_main_checklist;
		from_idx = 1; /* Start identity check at G27 */
		to_idx = checklist->count - 3; /* End identity check at G27 */
		break;
	case USB_DEVICE_ID_LOGITECH_DFGT_WHEEL:
		checklist = &lg4ff_main_checklist;
		from_idx = 0;
		to_idx = checklist->count - 4; /* End identity check at DFGT */
		break;
	default:
		return 0;
	}

	for (i = from_idx; i <= to_idx; i++) {
		const u16 mask = checklist->models[i]->mask;
		const u16 result = checklist->models[i]->result;
		const u16 real_product_id = checklist->models[i]->real_product_id;

		if ((bcdDevice & mask) == result) {
			dbg_hid("Found wheel with real PID %X whose reported PID is %X\n", real_product_id, reported_product_id);
			return real_product_id;
		}
	}

	/* No match found. This is an unknown wheel model, do not touch it */
	dbg_hid("Wheel with bcdDevice %X was not recognized as multimode wheel, leaving in its current mode\n", bcdDevice);
	return 0;
}

static int lg4ff_handle_multimode_wheel(struct hid_device *hid, u16 *real_product_id, const u16 bcdDevice)
{
	const u16 reported_product_id = hid->product;
	int ret;

	*real_product_id = lg4ff_identify_multimode_wheel(hid, reported_product_id, bcdDevice);
	/* Probed wheel is not a multimode wheel */
	if (!*real_product_id) {
		*real_product_id = reported_product_id;
		dbg_hid("Wheel is not a multimode wheel\n");
		return LG4FF_MMODE_NOT_MULTIMODE;
	}

	/* Switch from "Driving Force" mode to native mode automatically.
	 * Otherwise keep the wheel in its current mode */
	if (reported_product_id == USB_DEVICE_ID_LOGITECH_WHEEL &&
	    reported_product_id != *real_product_id &&
	    !lg4ff_no_autoswitch) {
		const struct lg4ff_compat_mode_switch *s;

		switch (*real_product_id) {
		case USB_DEVICE_ID_LOGITECH_DFP_WHEEL:
			s = &lg4ff_mode_switch_dfp;
			break;
		case USB_DEVICE_ID_LOGITECH_G25_WHEEL:
			s = &lg4ff_mode_switch_g25;
			break;
		case USB_DEVICE_ID_LOGITECH_G27_WHEEL:
			s = &lg4ff_mode_switch_g27;
			break;
		case USB_DEVICE_ID_LOGITECH_DFGT_WHEEL:
			s = &lg4ff_mode_switch_dfgt;
			break;
		default:
			hid_err(hid, "Invalid product id %X\n", *real_product_id);
			return LG4FF_MMODE_NOT_MULTIMODE;
		}

		ret = lg4ff_switch_compatibility_mode(hid, s);
		if (ret) {
			/* Wheel could not have been switched to native mode,
			 * leave it in "Driving Force" mode and continue */
			hid_err(hid, "Unable to switch wheel mode, errno %d\n", ret);
			return LG4FF_MMODE_IS_MULTIMODE;
		}
		return LG4FF_MMODE_SWITCHED;
	}

	return LG4FF_MMODE_IS_MULTIMODE;
}


int lg4ff_init(struct hid_device *hid)
{
	struct hid_input *hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	struct input_dev *dev = hidinput->input;
	const struct usb_device_descriptor *udesc = &(hid_to_usb_dev(hid)->descriptor);
	const u16 bcdDevice = le16_to_cpu(udesc->bcdDevice);
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;
	int error, i, j;
	int mmode_ret, mmode_idx = -1;
	u16 real_product_id;

	if (list_empty(&hid->inputs)) {
		hid_err(hid, "no inputs found\n");
		return -ENODEV;
	}
	hidinput = list_entry(hid->inputs.next, struct hid_input, list);
	dev = hidinput->input;

	/* Check that the report looks ok */
	if (!hid_validate_values(hid, HID_OUTPUT_REPORT, 0, 0, 7))
		return -1;

	/* Check if a multimode wheel has been connected and
	 * handle it appropriately */
	mmode_ret = lg4ff_handle_multimode_wheel(hid, &real_product_id, bcdDevice);

	/* Wheel has been told to switch to native mode. There is no point in going on
	 * with the initialization as the wheel will do a USB reset when it switches mode
	 */
	if (mmode_ret == LG4FF_MMODE_SWITCHED)
		return 0;

	/* Check what wheel has been connected */
	for (i = 0; i < ARRAY_SIZE(lg4ff_devices); i++) {
		if (hid->product == lg4ff_devices[i].product_id) {
			dbg_hid("Found compatible device, product ID %04X\n", lg4ff_devices[i].product_id);
			break;
		}
	}

	if (i == ARRAY_SIZE(lg4ff_devices)) {
		hid_err(hid, "Device is not supported by lg4ff driver. If you think it should be, consider reporting a bug to"
			     "LKML, Simon Wood <simon@mungewell.org> or Michal Maly <madcatxster@gmail.com>\n");
		return -1;
	}

	if (mmode_ret == LG4FF_MMODE_IS_MULTIMODE) {
		for (mmode_idx = 0; mmode_idx < ARRAY_SIZE(lg4ff_multimode_wheels); mmode_idx++) {
			if (real_product_id == lg4ff_multimode_wheels[mmode_idx].product_id)
				break;
		}

		if (mmode_idx == ARRAY_SIZE(lg4ff_multimode_wheels)) {
			hid_err(hid, "Device product ID %X is not listed as a multimode wheel", real_product_id);
			return -1;
		}
	}

	/* Set supported force feedback capabilities */
	for (j = 0; lg4ff_devices[i].ff_effects[j] >= 0; j++)
		set_bit(lg4ff_devices[i].ff_effects[j], dev->ffbit);

	error = input_ff_create_memless(dev, NULL, hid_lg4ff_play);

	if (error)
		return error;

	/* Get private driver data */
	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Cannot add device, private driver data not allocated\n");
		return -1;
	}

	/* Initialize device properties */
	entry = kzalloc(sizeof(struct lg4ff_device_entry), GFP_KERNEL);
	if (!entry) {
		hid_err(hid, "Cannot add device, insufficient memory to allocate device properties.\n");
		return -ENOMEM;
	}
	drv_data->device_props = entry;

	entry->product_id = lg4ff_devices[i].product_id;
	entry->real_product_id = real_product_id;
	entry->min_range = lg4ff_devices[i].min_range;
	entry->max_range = lg4ff_devices[i].max_range;
	entry->set_range = lg4ff_devices[i].set_range;
	if (mmode_ret == LG4FF_MMODE_IS_MULTIMODE) {
		BUG_ON(mmode_idx == -1);
		entry->alternate_modes = lg4ff_multimode_wheels[mmode_idx].alternate_modes;
		entry->real_tag = lg4ff_multimode_wheels[mmode_idx].real_tag;
		entry->real_name = lg4ff_multimode_wheels[mmode_idx].real_name;
	}

	/* Check if autocentering is available and
	 * set the centering force to zero by default */
	if (test_bit(FF_AUTOCENTER, dev->ffbit)) {
		/* Formula Force EX expects different autocentering command */
		if ((bcdDevice >> 8) == LG4FF_FFEX_REV_MAJ &&
		    (bcdDevice & 0xff) == LG4FF_FFEX_REV_MIN)
			dev->ff->set_autocenter = hid_lg4ff_set_autocenter_ffex;
		else
			dev->ff->set_autocenter = hid_lg4ff_set_autocenter_default;

		dev->ff->set_autocenter(dev, 0);
	}

	/* Create sysfs interface */
	error = device_create_file(&hid->dev, &dev_attr_range);
	if (error)
		return error;
	if (mmode_ret == LG4FF_MMODE_IS_MULTIMODE) {
		error = device_create_file(&hid->dev, &dev_attr_real_id);
		if (error)
			return error;
		error = device_create_file(&hid->dev, &dev_attr_alternate_modes);
		if (error)
			return error;
	}
	dbg_hid("sysfs interface created\n");

	/* Set the maximum range to start with */
	entry->range = entry->max_range;
	if (entry->set_range != NULL)
		entry->set_range(hid, entry->range);

#ifdef CONFIG_LEDS_CLASS
	/* register led subsystem - G27 only */
	entry->led_state = 0;
	for (j = 0; j < 5; j++)
		entry->led[j] = NULL;

	if (lg4ff_devices[i].product_id == USB_DEVICE_ID_LOGITECH_G27_WHEEL) {
		struct led_classdev *led;
		size_t name_sz;
		char *name;

		lg4ff_set_leds(hid, 0);

		name_sz = strlen(dev_name(&hid->dev)) + 8;

		for (j = 0; j < 5; j++) {
			led = kzalloc(sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
			if (!led) {
				hid_err(hid, "can't allocate memory for LED %d\n", j);
				goto err;
			}

			name = (void *)(&led[1]);
			snprintf(name, name_sz, "%s::RPM%d", dev_name(&hid->dev), j+1);
			led->name = name;
			led->brightness = 0;
			led->max_brightness = 1;
			led->brightness_get = lg4ff_led_get_brightness;
			led->brightness_set = lg4ff_led_set_brightness;

			entry->led[j] = led;
			error = led_classdev_register(&hid->dev, led);

			if (error) {
				hid_err(hid, "failed to register LED %d. Aborting.\n", j);
err:
				/* Deregister LEDs (if any) */
				for (j = 0; j < 5; j++) {
					led = entry->led[j];
					entry->led[j] = NULL;
					if (!led)
						continue;
					led_classdev_unregister(led);
					kfree(led);
				}
				goto out;	/* Let the driver continue without LEDs */
			}
		}
	}
out:
#endif
	hid_info(hid, "Force feedback support for Logitech Gaming Wheels\n");
	return 0;
}

int lg4ff_deinit(struct hid_device *hid)
{
	struct lg4ff_device_entry *entry;
	struct lg_drv_data *drv_data;

	drv_data = hid_get_drvdata(hid);
	if (!drv_data) {
		hid_err(hid, "Error while deinitializing device, no private driver data.\n");
		return -1;
	}
	entry = drv_data->device_props;
	if (!entry)
		goto out; /* Nothing more to do */

	device_remove_file(&hid->dev, &dev_attr_range);

	/* Multimode devices will have at least the "MODE_NATIVE" bit set */
	if (entry->alternate_modes) {
		device_remove_file(&hid->dev, &dev_attr_real_id);
		device_remove_file(&hid->dev, &dev_attr_alternate_modes);
	}

#ifdef CONFIG_LEDS_CLASS
	{
		int j;
		struct led_classdev *led;

		/* Deregister LEDs (if any) */
		for (j = 0; j < 5; j++) {

			led = entry->led[j];
			entry->led[j] = NULL;
			if (!led)
				continue;
			led_classdev_unregister(led);
			kfree(led);
		}
	}
#endif

	/* Deallocate memory */
	kfree(entry);

out:
	dbg_hid("Device successfully unregistered\n");
	return 0;
}
