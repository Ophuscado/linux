// SPDX-License-Identifier: GPL-2.0-only
/*
 * Thunderbolt driver - Out-of-band protocol adapter notification support
 *
 * On some host routers the controller behind a protocol adapter is not a
 * device on the Thunderbolt bus but a separate platform device, which has to
 * be told when a tunnel to its adapter comes up or goes away. Such a
 * controller registers a protocol adapter for its firmware node and the host
 * router driver looks it up from there.
 *
 * Copyright (C) The Asahi Linux Contributors
 * Author: Sven Peter <sven@svenpeter.dev>
 *
 * based on USB Role Switch Support (drivers/usb/roles/class.c)
 * Copyright (C) 2018 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *         Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/thunderbolt.h>

#include "tb.h"

static const struct class tb_protocol_adapter_class = {
	.name = "thunderbolt_protocol_adapter",
};

struct tb_protocol_adapter {
	struct device dev;
	struct mutex lock;

	tb_protocol_adapter_activate_t activate;
	tb_protocol_adapter_deactivate_t deactivate;
};

#define to_tb_protocol_adapter(d) \
	container_of(d, struct tb_protocol_adapter, dev)

static void tb_protocol_adapter_release(struct device *dev)
{
	struct tb_protocol_adapter *adapter = to_tb_protocol_adapter(dev);

	kfree(adapter);
}

static const struct device_type tb_protocol_adapter_dev_type = {
	.name = "tb_protocol_adapter",
	.release = tb_protocol_adapter_release,
};

struct tb_protocol_adapter *
tb_protocol_adapter_register(struct device *parent,
			     const struct tb_protocol_adapter_desc *desc)
{
	struct tb_protocol_adapter *adapter;
	int ret;

	if (!desc->fwnode)
		return ERR_PTR(-EINVAL);

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return ERR_PTR(-ENOMEM);

	mutex_init(&adapter->lock);

	adapter->activate = desc->activate;
	adapter->deactivate = desc->deactivate;
	adapter->dev.parent = parent;
	adapter->dev.fwnode = desc->fwnode;
	adapter->dev.class = &tb_protocol_adapter_class;
	adapter->dev.type = &tb_protocol_adapter_dev_type;
	dev_set_drvdata(&adapter->dev, desc->driver_data);
	dev_set_name(&adapter->dev, "%s-tbt-protocol-adapter",
		     dev_name(parent));

	ret = device_register(&adapter->dev);
	if (ret) {
		put_device(&adapter->dev);
		return ERR_PTR(ret);
	}

	return adapter;
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_register);

void tb_protocol_adapter_unregister(struct tb_protocol_adapter *adapter)
{
	device_unregister(&adapter->dev);
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_unregister);

struct tb_protocol_adapter *
tb_protocol_adapter_get(struct fwnode_handle *fwnode)
{
	struct tb_protocol_adapter *adapter;
	struct device *dev;

	dev = class_find_device_by_fwnode(&tb_protocol_adapter_class, fwnode);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	adapter = to_tb_protocol_adapter(dev);
	/* The parent is bound for as long as its adapter is registered. */
	if (!try_module_get(dev->parent->driver->owner)) {
		put_device(dev);
		return ERR_PTR(-ENODEV);
	}

	return adapter;
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_get);

void tb_protocol_adapter_put(struct tb_protocol_adapter *adapter)
{
	module_put(adapter->dev.parent->driver->owner);
	put_device(&adapter->dev);
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_put);

void *tb_protocol_adapter_get_drvdata(struct tb_protocol_adapter *adapter)
{
	return dev_get_drvdata(&adapter->dev);
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_get_drvdata);

int tb_protocol_adapter_activate(struct tb_protocol_adapter *adapter)
{
	int ret = 0;

	mutex_lock(&adapter->lock);
	if (adapter->activate)
		ret = adapter->activate(adapter);
	mutex_unlock(&adapter->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_activate);

void tb_protocol_adapter_deactivate(struct tb_protocol_adapter *adapter)
{
	mutex_lock(&adapter->lock);
	if (adapter->deactivate)
		adapter->deactivate(adapter);
	mutex_unlock(&adapter->lock);
}
EXPORT_SYMBOL_GPL(tb_protocol_adapter_deactivate);

int tb_protocol_adapter_init(void)
{
	return class_register(&tb_protocol_adapter_class);
}

void tb_protocol_adapter_exit(void)
{
	class_unregister(&tb_protocol_adapter_class);
}
