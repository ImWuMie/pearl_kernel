# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_POWER_RESET)	+= reset/
ifeq ($(CONFIG_CHARGE_PROJECT),"PLATO")
obj-$(CONFIG_POWER_SUPPLY)	+= supply_new/
obj-m				+= xm_charge/
else
obj-$(CONFIG_POWER_SUPPLY)	+= supply/
endif
obj-$(CONFIG_XM_POWER_DEBUG)	+= xm_power_debug/
obj-$(CONFIG_XM_POWER)		+= xm_power/
