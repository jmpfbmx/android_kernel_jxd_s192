/*
 * rtc-palmas.c -- Palmas Real Time Clock driver.

 * RTC driver for TI Palma series devices like TPS65913,
 * TPS65914 power management IC.
 *
 * Copyright (c) 2012 - 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Kasoju Mallikarjun <mkasoju@nvidia.com>
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/bcd.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/palmas.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

struct palmas_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	unsigned int		irq;
};

/* Total number of RTC registers needed to set time*/
#define PALMAS_NUM_TIME_REGS	(PALMAS_YEARS_REG - PALMAS_SECONDS_REG + 1)

static int palmas_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	/* Copy RTC counting registers to static registers or latches */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_GET_TIME, PALMAS_RTC_CTRL_REG_GET_TIME);
	if (ret < 0) {
		dev_err(dev, "RTC CTRL reg update failed, err: %d\n", ret);
		return ret;
	}

	ret = palmas_bulk_read(palmas, PALMAS_RTC_BASE, PALMAS_SECONDS_REG,
			rtc_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_SECONDS reg read failed, err = %d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	dev_dbg(dev, "%s() %d %d %d %d %d %d\n",
		__func__, tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	return ret;
}

static int palmas_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);

	dev_dbg(dev, "%s() %d %d %d %d %d %d\n",
		__func__, tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	/* Stop RTC while updating the RTC time registers */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_STOP_RTC, 0);
	if (ret < 0) {
		dev_err(dev, "RTC stop failed, err = %d\n", ret);
		return ret;
	}

	ret = palmas_bulk_write(palmas, PALMAS_RTC_BASE, PALMAS_SECONDS_REG,
		rtc_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_SECONDS reg write failed, err = %d\n", ret);
		return ret;
	}

	/* Start back RTC */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_STOP_RTC, PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0)
		dev_err(dev, "RTC start failed, err = %d\n", ret);
	return ret;
}

static int palmas_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	u8 val;

	val = enabled ? PALMAS_RTC_INTERRUPTS_REG_IT_ALARM : 0;
	return palmas_write(palmas, PALMAS_RTC_BASE,
		PALMAS_RTC_INTERRUPTS_REG, val);
}

static int palmas_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[PALMAS_NUM_TIME_REGS];
	u32 int_val;
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	ret = palmas_bulk_read(palmas, PALMAS_RTC_BASE,
			PALMAS_ALARM_SECONDS_REG,
			alarm_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_ALARM_SECONDS read failed, err = %d\n", ret);
		return ret;
	}

	alm->time.tm_sec = bcd2bin(alarm_data[0]);
	alm->time.tm_min = bcd2bin(alarm_data[1]);
	alm->time.tm_hour = bcd2bin(alarm_data[2]);
	alm->time.tm_mday = bcd2bin(alarm_data[3]);
	alm->time.tm_mon = bcd2bin(alarm_data[4]) - 1;
	alm->time.tm_year = bcd2bin(alarm_data[5]) + 100;

	dev_dbg(dev, "%s() %d %d %d %d %d %d\n", __func__,
		alm->time.tm_year, alm->time.tm_mon, alm->time.tm_mday,
		alm->time.tm_hour, alm->time.tm_min, alm->time.tm_sec);

	ret = palmas_read(palmas, PALMAS_RTC_BASE, PALMAS_RTC_INTERRUPTS_REG,
			&int_val);
	if (ret < 0) {
		dev_err(dev, "RTC_INTERRUPTS reg read failed, err = %d\n", ret);
		return ret;
	}

	if (int_val & PALMAS_RTC_INTERRUPTS_REG_IT_ALARM)
		alm->enabled = 1;
	return ret;
}

static int palmas_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	ret = palmas_rtc_alarm_irq_enable(dev, 0);
	if (ret < 0) {
		dev_err(dev, "Disable RTC alarm failed\n");
		return ret;
	}

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = bin2bcd(alm->time.tm_year - 100);

	dev_dbg(dev, "%s() %d %d %d %d %d %d\n", __func__,
		alm->time.tm_year, alm->time.tm_mon, alm->time.tm_mday,
		alm->time.tm_hour, alm->time.tm_min, alm->time.tm_sec);

	ret = palmas_bulk_write(palmas, PALMAS_RTC_BASE,
		PALMAS_ALARM_SECONDS_REG, alarm_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "ALARM_SECONDS_REG write failed, err = %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = palmas_rtc_alarm_irq_enable(dev, 1);
	return ret;
}

static int palmas_clear_interrupts(struct device *dev)
{
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	unsigned int rtc_reg;
	int ret;

	ret = palmas_read(palmas, PALMAS_RTC_BASE, PALMAS_RTC_STATUS_REG,
				&rtc_reg);
	if (ret < 0) {
		dev_err(dev, "RTC_STATUS read failed, err = %d\n", ret);
		return ret;
	}

	ret = palmas_write(palmas, PALMAS_RTC_BASE, PALMAS_RTC_STATUS_REG,
			rtc_reg);
	if (ret < 0) {
		dev_err(dev, "RTC_STATUS write failed, err = %d\n", ret);
		return ret;
	}
	return 0;
}

static irqreturn_t palmas_rtc_interrupt(int irq, void *context)
{
	struct palmas_rtc *palmas_rtc = context;
	struct device *dev = palmas_rtc->dev;
	int ret;

	ret = palmas_clear_interrupts(dev);
	if (ret < 0) {
		dev_err(dev, "RTC interrupt clear failed, err = %d\n", ret);
		return IRQ_NONE;
	}

	rtc_update_irq(palmas_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

struct palmas_rtc *globle_palmas;

int load_battery_capacity_by_read_reg(u32 *int_val)
{
	struct palmas *palmas;
	int ret=-1;
	struct device *dev;

	if(globle_palmas)
	{
		dev = globle_palmas->dev;
		palmas = dev_get_drvdata(dev->parent);
		ret = palmas_read(palmas, PALMAS_RTC_BASE, PALMAS_RTC_COMP_LSB_REG, int_val);
		if (ret < 0) {
			printk("RTC_COMP_LSB_REG read failed: %d\n",
					ret);
			return ret;
		}
		printk("load_battery_capacity_by_read_reg value=%d,ret=%d\n",*int_val,ret);
	}

	return ret;
}

int put_battery_capacity_by_write_reg(u32 int_val)
{
	struct palmas *palmas;
	int ret=-1;
	struct device *dev;

	if(globle_palmas)
	{
		dev = globle_palmas->dev;
		palmas = dev_get_drvdata(dev->parent);
		ret = palmas_write(palmas, PALMAS_RTC_BASE, PALMAS_RTC_COMP_LSB_REG, int_val);
		if (ret < 0) {
			printk("RTC_COMP_LSB_REG write failed: value=%d,%d\n",int_val,ret);
			return ret;
		}
		printk("put_battery_capacity_by_write_reg value=%d,ret=%d\n",int_val,ret);
	}

	return ret;
}


static struct rtc_class_ops palmas_rtc_ops = {
	.read_time	= palmas_rtc_read_time,
	.set_time	= palmas_rtc_set_time,
	.read_alarm	= palmas_rtc_read_alarm,
	.set_alarm	= palmas_rtc_set_alarm,
	.alarm_irq_enable = palmas_rtc_alarm_irq_enable,
};

static int palmas_rtc_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_rtc *palmas_rtc = NULL;
	struct palmas_platform_data *palmas_pdata;
	struct palmas_rtc_platform_data *rtc_pdata = NULL;
	int ret;
	bool enable_bb_charging = false;
	bool high_bb_charging;


	palmas_pdata = dev_get_platdata(pdev->dev.parent);
	if (palmas_pdata)
		rtc_pdata = palmas_pdata->rtc_pdata;

	if (rtc_pdata) {
		enable_bb_charging = rtc_pdata->backup_battery_chargeable;
		high_bb_charging = rtc_pdata->backup_battery_charge_high_current;
	} else if (pdev->dev.of_node) {
		enable_bb_charging = of_property_read_bool(pdev->dev.of_node,
					"ti,backup-battery-chargeable");
		high_bb_charging = of_property_read_bool(pdev->dev.of_node,
					"ti,backup-battery-charge-high-current");
	}

	palmas_rtc = devm_kzalloc(&pdev->dev, sizeof(struct palmas_rtc),
			GFP_KERNEL);
	if (!palmas_rtc) {
		dev_err(&pdev->dev, "Memory allocation failed.\n");
		return -ENOMEM;
	}

	/* Clear pending interrupts */
	ret = palmas_clear_interrupts(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "clear RTC int failed, err = %d\n", ret);
		return ret;
	}

	palmas->rtc = palmas_rtc;
	palmas_rtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, palmas_rtc);

	if (enable_bb_charging) {
		unsigned reg = PALMAS_BACKUP_BATTERY_CTRL_BBS_BBC_LOW_ICHRG;

		if (high_bb_charging)
			reg = 0;

		ret = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
			PALMAS_BACKUP_BATTERY_CTRL,
			PALMAS_BACKUP_BATTERY_CTRL_BBS_BBC_LOW_ICHRG, reg);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"BACKUP_BATTERY_CTRL update failed, %d\n", ret);
			return ret;
		}

		ret = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
			PALMAS_BACKUP_BATTERY_CTRL,
			PALMAS_BACKUP_BATTERY_CTRL_BB_CHG_EN,
			PALMAS_BACKUP_BATTERY_CTRL_BB_CHG_EN);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"BACKUP_BATTERY_CTRL update failed, %d\n", ret);
			return ret;
		}
	}

	ret = palmas_write(palmas, PALMAS_RTC_BASE,
				PALMAS_RTC_INTERRUPTS_REG, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "RTC_INTERRUPTS_REG write failed: %d\n",
				ret);
		return ret;
	}

	/* Start RTC */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
			PALMAS_RTC_CTRL_REG_STOP_RTC,
			PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0) {
		dev_err(&pdev->dev, "RTC_CTRL write failed, err = %d\n", ret);
		return ret;
	}

	palmas_rtc->irq = palmas_irq_get_virq(palmas, PALMAS_RTC_ALARM_IRQ);
	dev_dbg(&pdev->dev, "RTC interrupt %d\n", palmas_rtc->irq);

	device_init_wakeup(&pdev->dev, 1);
	palmas_rtc->rtc = rtc_device_register(pdev->name, &pdev->dev,
		&palmas_rtc_ops, THIS_MODULE);
	if (IS_ERR(palmas_rtc->rtc)) {
		ret = PTR_ERR(palmas_rtc->rtc);
		dev_err(&pdev->dev, "RTC register failed, err = %d\n", ret);
		return ret;
	}

	ret = request_threaded_irq(palmas_rtc->irq, NULL,
			palmas_rtc_interrupt,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT |
			IRQF_EARLY_RESUME,
			dev_name(&pdev->dev), palmas_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ request failed, err = %d\n", ret);
		rtc_device_unregister(palmas_rtc->rtc);
		return ret;
	}

	globle_palmas = palmas_rtc;

	return 0;
}

static int palmas_rtc_remove(struct platform_device *pdev)
{
	struct palmas_rtc *palmas_rtc = platform_get_drvdata(pdev);

	palmas_rtc_alarm_irq_enable(&pdev->dev, 0);
	free_irq(palmas_rtc->irq, palmas_rtc);
	rtc_device_unregister(palmas_rtc->rtc);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int palmas_rtc_suspend(struct device *dev)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		int ret;
		struct rtc_wkalrm alm;

		enable_irq_wake(palmas_rtc->irq);
		ret = palmas_rtc_read_alarm(dev, &alm);
		if (!ret)
			dev_info(dev, "%s() alrm %d time %d %d %d %d %d %d\n",
				__func__, alm.enabled,
				alm.time.tm_year, alm.time.tm_mon,
				alm.time.tm_mday, alm.time.tm_hour,
				alm.time.tm_min, alm.time.tm_sec);
	}

	return 0;
}

static int palmas_rtc_resume(struct device *dev)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		struct rtc_time tm;
		int ret;

		disable_irq_wake(palmas_rtc->irq);
		ret = palmas_rtc_read_time(dev, &tm);
		if (!ret)
			dev_info(dev, "%s() %d %d %d %d %d %d\n",
				__func__, tm.tm_year, tm.tm_mon, tm.tm_mday,
				tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	return 0;
}
#endif

static const struct dev_pm_ops palmas_rtc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(palmas_rtc_suspend, palmas_rtc_resume)
};

#ifdef CONFIG_OF
static struct of_device_id of_palmas_rtc_match[] = {
	{ .compatible = "ti,palmas-rtc"},
	{ },
};
MODULE_DEVICE_TABLE(of, of_palmas_rtc_match);
#endif

static struct platform_driver palmas_rtc_driver = {
	.probe		= palmas_rtc_probe,
	.remove		= palmas_rtc_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "palmas-rtc",
		.pm	= &palmas_rtc_pm_ops,
		.of_match_table = of_match_ptr(of_palmas_rtc_match),
	},
};

module_platform_driver(palmas_rtc_driver);

MODULE_ALIAS("platform:palmas_rtc");
MODULE_DESCRIPTION("TI PALMAS series RTC driver");
MODULE_AUTHOR("Kasoju Mallikarjun <mkasoju@nvidia.com>");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
