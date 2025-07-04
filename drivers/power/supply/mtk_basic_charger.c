// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_basic_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "mtk_charger.h"

static int mtbf_test = 0;
module_param_named(mtbf_test, mtbf_test, int, 0600);
extern int get_pd_usb_connected(void);
static int _uA_to_mA(int uA)
{
	if (uA == -1)
		return -1;
	else
		return uA / 1000;
}

static void select_cv(struct mtk_charger *info)
{
	u32 constant_voltage;

	if (info->enable_sw_jeita)
		if (info->sw_jeita.cv != 0) {
			info->setting.cv = info->sw_jeita.cv;
			return;
		}

	constant_voltage = info->data.battery_cv;
	chr_err("%s:cv=%d\n",
			__func__, info->data.battery_cv);
	info->setting.cv = constant_voltage;
}

static bool is_typec_adapter(struct mtk_charger *info)
{
	int rp;

	rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
	if (info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK &&
			rp != 500 &&
			info->chr_type != POWER_SUPPLY_TYPE_USB &&
			info->chr_type != POWER_SUPPLY_TYPE_USB_CDP)
		return true;

	return false;
}

static bool support_fast_charging(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int i = 0, state = 0;
	bool ret = false;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		if (info->enable_fast_charging_indicator &&
		    ((alg->alg_id & info->fast_charging_indicator) == 0))
			continue;

		chg_alg_set_current_limit(alg, &info->setting);
		state = chg_alg_is_algo_ready(alg);
		chr_debug("%s %s ret:%s\n", __func__, dev_name(&alg->dev),
			chg_alg_state_to_str(state));

		if (state == ALG_READY || state == ALG_RUNNING) {
			ret = true;
			break;
		}
	}
	return ret;
}

static bool select_charging_current_limit(struct mtk_charger *info,
	struct chg_limit_setting *setting)
{
	struct charger_data *pdata, *pdata2, *pdata_dvchg, *pdata_dvchg2;
	bool is_basic = false;
	u32 ichg1_min = 0, aicr1_min = 0;
	int ret, type_temp;
	struct adapter_power_cap cap;
	int i = 0, wait_count = 0;

	select_cv(info);

	pdata = &info->chg_data[CHG1_SETTING];
	pdata2 = &info->chg_data[CHG2_SETTING];
	pdata_dvchg = &info->chg_data[DVCHG1_SETTING];
	pdata_dvchg2 = &info->chg_data[DVCHG2_SETTING];
	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	chr_err("charging_current_limit(uA) %d %d %d %d\n", pdata->input_current_limit, pdata->charging_current_limit, ichg1_min, ichg1_min);
	if (info->usb_unlimited) {
		pdata->input_current_limit = 1800000;
		pdata->charging_current_limit = 1800000;
		is_basic = true;
		goto done;
	}

	if (info->water_detected) {
		pdata->input_current_limit = info->data.usb_charger_current;
		pdata->charging_current_limit = info->data.usb_charger_current;
		is_basic = true;
		goto done;
	}

	if (((info->bootmode == 1) ||
	    (info->bootmode == 5)) && info->enable_meta_current_limit != 0) {
		pdata->input_current_limit = 200000; // 200mA
		is_basic = true;
		goto done;
	}
	/* remove atm enabled set 100ma input current*/
#if 0
	if (info->atm_enabled == true
		&& (info->chr_type == POWER_SUPPLY_TYPE_USB ||
		info->chr_type == POWER_SUPPLY_TYPE_USB_CDP)
		) {
		pdata->input_current_limit = 100000;  
		is_basic = true;
		goto done;
	}
#endif

	if (info->product_name == MATISSE) {//L11
		if (info->real_type == XMUSB350_TYPE_FLOAT && !info->pd_type && !info->usb350_dev) {
			if (info->usb_type == POWER_SUPPLY_USB_TYPE_SDP)
				info->real_type = XMUSB350_TYPE_SDP;
			else if (info->usb_type == POWER_SUPPLY_USB_TYPE_CDP)
				info->real_type = XMUSB350_TYPE_CDP;
			else if (info->usb_type == POWER_SUPPLY_USB_TYPE_DCP && info->chr_type == POWER_SUPPLY_TYPE_USB_DCP)
				info->real_type = XMUSB350_TYPE_DCP;
		}
	} else {
		if (info->real_type == XMUSB350_TYPE_FLOAT && (info->pd_type != MTK_PD_CONNECT_PE_READY_SNK_PD30) && (info->pd_type != MTK_PD_CONNECT_PE_READY_SNK_APDO) && !info->usb350_dev) {
			if (info->usb_type == POWER_SUPPLY_USB_TYPE_SDP && info->chr_type == POWER_SUPPLY_TYPE_USB)
				info->real_type = XMUSB350_TYPE_SDP;
			else if (info->usb_type == POWER_SUPPLY_USB_TYPE_CDP)
				info->real_type = XMUSB350_TYPE_CDP;
			else if (info->usb_type == POWER_SUPPLY_USB_TYPE_DCP && info->chr_type == POWER_SUPPLY_TYPE_USB_DCP)
				info->real_type = XMUSB350_TYPE_DCP;
		}
	}

	if (info->real_type == XMUSB350_TYPE_FLOAT) {
	        chr_err("float type set input current 1A charging\n");
	        pdata->input_current_limit =  1000000;
	        pdata->charging_current_limit = 1000000;
	        is_basic = true;
	        goto done;
	} else if ((info->pd_type == MTK_PD_CONNECT_PE_READY_SNK || info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO || info->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) && info->usb_type != POWER_SUPPLY_USB_TYPE_SDP) {
		if (info->switch_pd_wa > 0 && info->pd_type == MTK_PD_CONNECT_PE_READY_SNK) {
retry:
			if (!info->pd_verify_done) {
				if (wait_count < 5) {
					wait_count++;
					msleep(100);
					goto retry;
				}
			}
			info->switch_pd_wa--;
			ret = adapter_dev_get_cap(info->pd_adapter, MTK_PD, &cap);
			if (!ret) {
				for (i = 0; i < cap.nr; i++) {
					if (cap.type[i] == MTK_PD_APDO || cap.max_mv[i] == 9000)
						break;

					if (i == cap.nr - 1)
						info->switch_pd_wa = -1;
				}
			} else {

			}
		}

		if (info->switch_pd_wa == -1)
			 pdata->input_current_limit = 1400000;
		else if(get_vbus(info) < 6000)
 			pdata->input_current_limit = 3000000;
		else
			pdata->input_current_limit = info->data.pd2_input_current;
	        pdata->charging_current_limit = info->thermal_current * 1000;
	        chr_err("pd use vote current charging=%d\n", info->thermal_current);
	        is_basic = true;
	        goto done;
	} else if (info->real_type == XMUSB350_TYPE_HVDCP_2 || info->real_type == XMUSB350_TYPE_HVDCP_3) {
			chr_err("hvdcp2 set input current 1400ma charging\n");
			pdata->input_current_limit =  1400000;
			pdata->charging_current_limit = 2500000;
			is_basic = true;
			goto done;
	}

	if (info->chr_type == POWER_SUPPLY_TYPE_USB &&
	    info->usb_type == POWER_SUPPLY_USB_TYPE_SDP && (!get_pd_usb_connected())) {
		chr_err("USB set input current 500mA charging\n");
		pdata->input_current_limit =
				info->data.usb_charger_current;
		/* it can be larger */
		pdata->charging_current_limit =
				info->data.usb_charger_current;
		is_basic = true;
		if (!info->usb350_dev)
			info->real_type = XMUSB350_TYPE_SDP;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_CDP && info->usb_type != POWER_SUPPLY_USB_TYPE_DCP) {
		chr_err("CDP set input current 1500mA charging\n");
		pdata->input_current_limit =
			info->data.charging_host_charger_current;
		pdata->charging_current_limit =
			info->data.charging_host_charger_current;
		is_basic = true;
		if (!info->usb350_dev)
			info->real_type = XMUSB350_TYPE_CDP;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_DCP) {
		chr_err("DCP set input current 1600mA charging\n");
		pdata->input_current_limit =
			info->data.ac_charger_input_current;
		pdata->charging_current_limit =
			info->data.ac_charger_current;
		if (info->product_name == MATISSE) {//L11
			if (!info->usb350_dev && info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK)
				info->real_type = XMUSB350_TYPE_DCP;
		} else {
			if (!info->usb350_dev && (info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK || info->usb_type == POWER_SUPPLY_USB_TYPE_DCP))
				info->real_type = XMUSB350_TYPE_DCP;
		}
		if (info->config == DUAL_CHARGERS_IN_SERIES) {
			pdata2->input_current_limit =
				pdata->input_current_limit;
			pdata2->charging_current_limit = 2000000;
		}
		is_basic = true;
		goto done;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_ACA && info->real_type != XMUSB350_TYPE_PD) {
		chr_err("HVCHG set input current 2A charging\n");
		type_temp = info->real_type;
		if (!info->usb350_dev)
			info->real_type = XMUSB350_TYPE_HVCHG;
		pdata->input_current_limit =  1600000;
		pdata->charging_current_limit = 3000000;
		is_basic = true;
		if (type_temp != info->real_type)
			update_quick_chg_type(info);
		goto done;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_CDP &&
	    info->usb_type == POWER_SUPPLY_USB_TYPE_DCP) {
		chr_err("NONSTANDARD_CHARGER set input current 500mA charging\n");
		/* NONSTANDARD_CHARGER */
		pdata->input_current_limit =
			info->data.usb_charger_current;
		pdata->charging_current_limit =
			info->data.usb_charger_current;
		info->real_type = XMUSB350_TYPE_FLOAT;
		is_basic = true;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB &&
	    info->usb_type == POWER_SUPPLY_USB_TYPE_SDP && (info->real_type == XMUSB350_TYPE_PD||get_pd_usb_connected())) {
		chr_err("C to C set input current 1500mA charging\n");
		pdata->input_current_limit = 1500000;
		pdata->charging_current_limit = 1500000;
		is_basic = true;
		if (!info->usb350_dev)
			info->real_type = XMUSB350_TYPE_PD;
	} else {
		/*chr_type && usb_type cannot match above, set 500mA*/
		chr_err("others set input current 500mA charging\n");
		pdata->input_current_limit =
				info->data.usb_charger_current;
		pdata->charging_current_limit =
				info->data.usb_charger_current;
		is_basic = true;
	}

	if (support_fast_charging(info))
		is_basic = false;
	else {
		is_basic = true;
		/* AICL */
		if (mtbf_test == 0) {
			charger_dev_run_aicl(info->chg1_dev,
				&pdata->input_current_limit_by_aicl);
			if (info->enable_dynamic_mivr) {
				if (pdata->input_current_limit_by_aicl >
					info->data.max_dmivr_charger_current)
					pdata->input_current_limit_by_aicl =
						info->data.max_dmivr_charger_current;
			}
		} else {
			pdata->input_current_limit_by_aicl = -1;
		}
		if (is_typec_adapter(info)) {
			if (adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL)
				== 3000) {
				pdata->input_current_limit = 3000000;
				pdata->charging_current_limit = 3000000;
			} else if (adapter_dev_get_property(info->pd_adapter,
				TYPEC_RP_LEVEL) == 1500) {
				pdata->input_current_limit = 1500000;
				pdata->charging_current_limit = 2000000;
			} else {
				chr_err("type-C: inquire rp error\n");
				pdata->input_current_limit = 500000;
				pdata->charging_current_limit = 500000;
			}

			chr_err("type-C:%d current:%d\n",
				info->pd_type,
				adapter_dev_get_property(info->pd_adapter,
					TYPEC_RP_LEVEL));
		}
	}

	if (info->enable_sw_jeita) {
		if (IS_ENABLED(CONFIG_USBIF_COMPLIANCE)
			&& info->chr_type == POWER_SUPPLY_TYPE_USB)
			chr_debug("USBIF & STAND_HOST skip current check\n");
		else {
			if (info->sw_jeita.sm == TEMP_T0_TO_T1) {
				pdata->input_current_limit = 500000;
				pdata->charging_current_limit = 350000;
			}
		}
	}

	sc_select_charging_current(info, pdata);

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
			pdata->charging_current_limit) {
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
			info->setting.charging_current_limit1 =
					pdata->thermal_charging_current_limit;
		}
	} else
		info->setting.charging_current_limit1 = -1;

	if (pdata->thermal_input_current_limit != -1) {
		if (pdata->thermal_input_current_limit <
			pdata->input_current_limit) {
			pdata->input_current_limit =
					pdata->thermal_input_current_limit;
			info->setting.input_current_limit1 =
					pdata->input_current_limit;
		}
	} else
		info->setting.input_current_limit1 = -1;

	if (pdata2->thermal_charging_current_limit != -1) {
		if (pdata2->thermal_charging_current_limit <
			pdata2->charging_current_limit) {
			pdata2->charging_current_limit =
					pdata2->thermal_charging_current_limit;
			info->setting.charging_current_limit2 =
					pdata2->charging_current_limit;
		}
	} else
		info->setting.charging_current_limit2 = -1;

	if (pdata2->thermal_input_current_limit != -1) {
		if (pdata2->thermal_input_current_limit <
			pdata2->input_current_limit) {
			pdata2->input_current_limit =
					pdata2->thermal_input_current_limit;
			info->setting.input_current_limit2 =
					pdata2->input_current_limit;
		}
	} else
		info->setting.input_current_limit2 = -1;

	if (is_basic == true && pdata->input_current_limit_by_aicl != -1 && mtbf_test == 0) {
		if (pdata->input_current_limit_by_aicl <
		    pdata->input_current_limit)
			pdata->input_current_limit =
					pdata->input_current_limit_by_aicl;
	}
	info->setting.input_current_limit_dvchg1 =
		pdata_dvchg->thermal_input_current_limit;

done:
	if (info->jeita_chg_fcc > 0)
		pdata->charging_current_limit = min(info->jeita_chg_fcc * 1000, pdata->charging_current_limit);
	if (info->thermal_current > 0)
		pdata->charging_current_limit = min(info->thermal_current * 1000, pdata->charging_current_limit);
	ret = charger_dev_get_min_charging_current(info->chg1_dev, &ichg1_min);
	if (ret != -EOPNOTSUPP && pdata->charging_current_limit < ichg1_min) {
		pdata->charging_current_limit = 0;
		/* For TC_018, pleasae don't modify the format */
		chr_err("min_charging_current is too low %d %d\n",
			pdata->charging_current_limit, ichg1_min);
		is_basic = true;
	}

	ret = charger_dev_get_min_input_current(info->chg1_dev, &aicr1_min);
	if (ret != -EOPNOTSUPP && pdata->input_current_limit < aicr1_min) {
		pdata->input_current_limit = 0;
		/* For TC_018, pleasae don't modify the format */
		chr_err("min_input_current is too low %d %d\n",
			pdata->input_current_limit, aicr1_min);
		is_basic = true;
	}
	/* For TC_018, pleasae don't modify the format */
	chr_err("m:%d chg1:%d,%d,%d,%d chg2:%d,%d,%d,%d dvchg1:%d sc:%d %d %d type:%d:%d usb_unlimited:%d usbif:%d usbsm:%d aicl:%d atm:%d bm:%d b:%d mtbf:%d\n",
		info->config,
		_uA_to_mA(pdata->thermal_input_current_limit),
		_uA_to_mA(pdata->thermal_charging_current_limit),
		_uA_to_mA(pdata->input_current_limit),
		_uA_to_mA(pdata->charging_current_limit),
		_uA_to_mA(pdata2->thermal_input_current_limit),
		_uA_to_mA(pdata2->thermal_charging_current_limit),
		_uA_to_mA(pdata2->input_current_limit),
		_uA_to_mA(pdata2->charging_current_limit),
		_uA_to_mA(pdata_dvchg->thermal_input_current_limit),
		info->sc.pre_ibat,
		info->sc.sc_ibat,
		info->sc.solution,
		info->chr_type, info->pd_type,
		info->usb_unlimited,
		IS_ENABLED(CONFIG_USBIF_COMPLIANCE), info->usb_state,
		pdata->input_current_limit_by_aicl, info->atm_enabled,
		info->bootmode, is_basic, mtbf_test);

	return is_basic;
}

static int do_algorithm(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	struct charger_data *pdata;
	struct chg_alg_notify notify;
	bool is_basic = true;
	bool chg_done = false;
	int i;
	int ret;
	int val = 0;

	pdata = &info->chg_data[CHG1_SETTING];
	charger_dev_is_charging_done(info->chg1_dev, &chg_done);
	is_basic = select_charging_current_limit(info, &info->setting);

	if (info->is_chg_done != chg_done) {
		if (chg_done) {
			charger_dev_do_event(info->chg1_dev, EVENT_FULL, 0);
			info->polling_interval = CHARGING_FULL_INTERVAL;
			chr_err("%s battery full\n", __func__);
		} else {
			charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
			info->polling_interval = CHARGING_INTERVAL;
			chr_err("%s battery recharge\n", __func__);
		}
	}

	chr_err("%s is_basic:%d\n", __func__, is_basic);
	if (is_basic != true) {
		is_basic = true;
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;

			if (info->enable_fast_charging_indicator &&
			    ((alg->alg_id & info->fast_charging_indicator) == 0))
				continue;

			if (!info->enable_hv_charging ||
			    pdata->charging_current_limit == 0 ||
			    pdata->input_current_limit == 0) {
				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000)
					chg_alg_stop_algo(alg);
				chr_err("%s: alg:%s alg_vbus:%d\n", __func__,
					dev_name(&alg->dev), val);
				continue;
			}

			if (chg_done != info->is_chg_done) {
				if (chg_done) {
					notify.evt = EVT_FULL;
					notify.value = 0;
				} else {
					notify.evt = EVT_RECHARGE;
					notify.value = 0;
				}
				chg_alg_notifier_call(alg, &notify);
				chr_err("%s notify:%d\n", __func__, notify.evt);
			}

			chg_alg_set_current_limit(alg, &info->setting);
			ret = chg_alg_is_algo_ready(alg);

			chr_err("%s %s ret:%s\n", __func__,
				dev_name(&alg->dev),
				chg_alg_state_to_str(ret));

			if (ret == ALG_INIT_FAIL || ret == ALG_TA_NOT_SUPPORT) {
				/* try next algorithm */
				continue;
			} else if (ret == ALG_TA_CHECKING || ret == ALG_DONE ||
						ret == ALG_NOT_READY) {
				/* wait checking , use basic first */
				is_basic = true;
				break;
			} else if (ret == ALG_READY || ret == ALG_RUNNING) {
				is_basic = false;
				//chg_alg_set_setting(alg, &info->setting);
				chg_alg_start_algo(alg);
				break;
			} else {
				chr_err("algorithm ret is error");
				is_basic = true;
			}
		}
	} else {
		if (info->enable_hv_charging != true ||
		    pdata->charging_current_limit == 0 ||
		    pdata->input_current_limit == 0) {
			for (i = 0; i < MAX_ALG_NO; i++) {
				alg = info->alg[i];
				if (alg == NULL)
					continue;

				chg_alg_get_prop(alg, ALG_MAX_VBUS, &val);
				if (val > 5000 && chg_alg_is_algo_running(alg))
					chg_alg_stop_algo(alg);

				chr_err("%s: Stop hv charging. en_hv:%d alg:%s alg_vbus:%d\n",
					__func__, info->enable_hv_charging,
					dev_name(&alg->dev), val);
			}
		}
	}
	info->is_chg_done = chg_done;

	if (is_basic == true) {
		chr_err("ICL_VOTER set aicr=%d , not final set\n",pdata->input_current_limit);
		if((pdata->input_current_limit / 1000) < 1000){
			vote(info->icl_votable, ICL_VOTER, true, 1000);
		}else{
			vote(info->icl_votable, ICL_VOTER, true, pdata->input_current_limit / 1000);
		}

		charger_dev_set_charging_current(info->chg1_dev,
			pdata->charging_current_limit);

		chr_err("%s:old_cv=%d,cv=%d, vbat_mon_en=%d\n",
			__func__,
			info->old_cv,
			info->setting.cv,
			info->setting.vbat_mon_en);
		/* not use mtk default code modify cv voltage*/
#if 0
		if (info->old_cv == 0 || (info->old_cv != info->setting.cv)
		    || info->setting.vbat_mon_en == 0) {
			charger_dev_enable_6pin_battery_charging(
				info->chg1_dev, false);
			charger_dev_set_constant_voltage(info->chg1_dev,
				info->setting.cv);
			if (info->setting.vbat_mon_en && info->stop_6pin_re_en != 1)
				charger_dev_enable_6pin_battery_charging(
					info->chg1_dev, true);
			info->old_cv = info->setting.cv;
		} else {
			if (info->setting.vbat_mon_en && info->stop_6pin_re_en != 1) {
				info->stop_6pin_re_en = 1;
				charger_dev_enable_6pin_battery_charging(
					info->chg1_dev, true);
			}
		}
#endif
	}

	if (pdata->input_current_limit == 0 ||
	    pdata->charging_current_limit == 0)
		charger_dev_enable(info->chg1_dev, false);
	else {
		alg = get_chg_alg_by_name("pe5");
		ret = chg_alg_is_algo_ready(alg);
		if (!(ret == ALG_READY || ret == ALG_RUNNING) && (info->night_charge_enable == false)) {
			if(info->charge_full == 1)
          			charger_dev_enable(info->chg1_dev, !info->charge_full);
		}
	}

	if (info->chg1_dev != NULL)
		charger_dev_dump_registers(info->chg1_dev);

	if (info->chg2_dev != NULL)
		charger_dev_dump_registers(info->chg2_dev);

	return 0;
}

static int enable_charging(struct mtk_charger *info,
						bool en)
{
	int i;
	struct chg_alg_device *alg;


	chr_err("%s %d\n", __func__, en);

	if (en == false) {
		for (i = 0; i < MAX_ALG_NO; i++) {
			alg = info->alg[i];
			if (alg == NULL)
				continue;
			chg_alg_stop_algo(alg);
		}
		charger_dev_enable(info->chg1_dev, false);
		charger_dev_do_event(info->chg1_dev, EVENT_DISCHARGE, 0);
	} else {
		if (!info->night_charge_enable)
			charger_dev_enable(info->chg1_dev, !info->charge_full);
		charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
	}

	return 0;
}

static int charger_dev_event(struct notifier_block *nb, unsigned long event,
				void *v)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	struct mtk_charger *info =
			container_of(nb, struct mtk_charger, chg1_nb);
	struct chgdev_notify *data = v;
	int i;

	chr_err("%s %d\n", __func__, event);

	switch (event) {
	case CHARGER_DEV_NOTIFY_EOC:
		info->stop_6pin_re_en = 1;
		notify.evt = EVT_FULL;
		notify.value = 0;
		for (i = 0; i < 10; i++) {
			alg = info->alg[i];
			chg_alg_notifier_call(alg, &notify);
		}

		break;
	case CHARGER_DEV_NOTIFY_RECHG:
		pr_info("%s: recharge\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT:
		info->safety_timeout = true;
		pr_info("%s: safety timer timeout\n", __func__);
		break;
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		info->vbusov_stat = data->vbusov_stat;
		pr_info("%s: vbus ovp = %d\n", __func__, info->vbusov_stat);
		break;
	case CHARGER_DEV_NOTIFY_BATPRO_DONE:
		info->batpro_done = true;
		info->setting.vbat_mon_en = 0;
		notify.evt = EVT_BATPRO_DONE;
		notify.value = 0;
		for (i = 0; i < 10; i++) {
			alg = info->alg[i];
			chg_alg_notifier_call(alg, &notify);
		}
		pr_info("%s: batpro_done = %d\n", __func__, info->batpro_done);
		break;
	default:
		return NOTIFY_DONE;
	}

	if (info->chg1_dev->is_polling_mode == false)
		_wake_up_charger(info);

	return NOTIFY_DONE;
}

static int to_alg_notify_evt(unsigned long evt)
{
	switch (evt) {
	case CHARGER_DEV_NOTIFY_VBUS_OVP:
		return EVT_VBUSOVP;
	case CHARGER_DEV_NOTIFY_IBUSOCP:
		return EVT_IBUSOCP;
	case CHARGER_DEV_NOTIFY_IBUSUCP_FALL:
		return EVT_IBUSUCP_FALL;
	case CHARGER_DEV_NOTIFY_BAT_OVP:
		return EVT_VBATOVP;
	case CHARGER_DEV_NOTIFY_IBATOCP:
		return EVT_IBATOCP;
	case CHARGER_DEV_NOTIFY_VBATOVP_ALARM:
		return EVT_VBATOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VBUSOVP_ALARM:
		return EVT_VBUSOVP_ALARM;
	case CHARGER_DEV_NOTIFY_VOUTOVP:
		return EVT_VOUTOVP;
	case CHARGER_DEV_NOTIFY_VDROVP:
		return EVT_VDROVP;
	default:
		return -EINVAL;
	}
}

static int dvchg1_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, dvchg1_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int dvchg2_dev_event(struct notifier_block *nb, unsigned long event,
			    void *data)
{
	struct mtk_charger *info =
		container_of(nb, struct mtk_charger, dvchg1_nb);
	int alg_evt = to_alg_notify_evt(event);

	chr_info("%s %ld", __func__, event);
	if (alg_evt < 0)
		return NOTIFY_DONE;
	mtk_chg_alg_notify_call(info, alg_evt, 0);
	return NOTIFY_OK;
}

static int mt_charger_fcc_vote_callback(struct votable *votable, void *data, int value, const char *client)
{
	struct mtk_charger *mpci = data;
	int ret = 0;

	chr_info("vote FCC = %d\n", value);
	if (value > 3000)
		value = 3000 * 1000;
	else
		value = value * 1000;

	ret = charger_dev_set_charging_current(mpci->chg1_dev, value);
	if (ret) {
		chr_err("failed to set FCC\n");
		return ret;
	}

	return ret;
}

static int mt_charger_fv_vote_callback(struct votable *votable, void *data, int value, const char *client)
{
	struct mtk_charger *mpci = data;
	int ret = 0;

	chr_info("vote FV = %d\n", value);

	mpci->data.battery_cv = value;
	ret = charger_dev_set_constant_voltage(mpci->chg1_dev, value * 1000);
	if (ret) {
		chr_err("failed to set FV\n");
		return ret;
	}

	return ret;
}

static int mt_charger_icl_vote_callback(struct votable *votable, void *data, int value, const char *client)
{
	struct mtk_charger *mpci = data;
	int ret = 0;

	chr_info("vote ICL = %d\n", value);

	ret = charger_dev_set_input_current(mpci->chg1_dev, value * 1000);
	if (ret) {
		chr_err("failed to set IINLIM0\n");
		return ret;
	}
	return ret;
}

static int mt_charger_iterm_vote_callback(struct votable *votable, void *data, int value, const char *client)
{
	struct mtk_charger *mpci = data;
	int ret = 0;

	chr_info("vote ITERM = %d\n", value);

	ret = charger_dev_set_eoc_current(mpci->chg1_dev, value * 1000);
	if (ret) {
		chr_err("failed to set ITERM\n");
		return ret;
	}

	return ret;
}

static int mtk_charger_create_votable(struct mtk_charger *mpci)
{
	int rc = 0;

	mpci->fcc_votable = create_votable("CHARGER_FCC", VOTE_MIN, mt_charger_fcc_vote_callback, mpci);
	if (IS_ERR(mpci->fcc_votable)) {
		chr_err("failed to create voter CHARGER_FCC\n");
		return -1;
	}

	mpci->fv_votable = create_votable("CHARGER_FV", VOTE_MIN, mt_charger_fv_vote_callback, mpci);
	if (IS_ERR(mpci->fv_votable)) {
		chr_err("failed to create voter CHARGER_FV\n");
		return -1;
	}

	mpci->icl_votable = create_votable("CHARGER_ICL", VOTE_MIN, mt_charger_icl_vote_callback, mpci);
	if (IS_ERR(mpci->icl_votable)) {
		chr_err("failed to create voter CHARGER_ICL\n");
		return -1;
	}

	mpci->iterm_votable = create_votable("CHARGER_ITERM", VOTE_MIN, mt_charger_iterm_vote_callback, mpci);
	if (IS_ERR(mpci->iterm_votable)) {
		chr_err("failed to create voter CHARGER_ITERM\n");
		return -1;
	}
	return rc;
}

int mtk_basic_charger_init(struct mtk_charger *info)
{
    int ret = 0;

	ret = mtk_charger_create_votable(info);
	if (ret)
		chr_err("failed to create charger voter\n");

	info->algo.do_algorithm = do_algorithm;
	info->algo.enable_charging = enable_charging;
	info->algo.do_event = charger_dev_event;
	info->algo.do_dvchg1_event = dvchg1_dev_event;
	info->algo.do_dvchg2_event = dvchg2_dev_event;
	//info->change_current_setting = mtk_basic_charging_current;
	return 0;
}
