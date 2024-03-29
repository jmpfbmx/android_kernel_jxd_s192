/*
 * rt5639_ioctl.h  --  RT5639 ALSA SoC audio driver IO control
 *
 * Copyright (c) 2011-2013 REALTEK SEMICONDUCTOR CORP. All rights reserved.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5639_IOCTL_H__
#define __RT5639_IOCTL_H__

#include <sound/hwdep.h>
#include <linux/ioctl.h>

enum {
	NORMAL = 0,
	SPK,
	HP,
	MODE_NUM,
};

#define EQ_REG_NUM 21
typedef struct  hweq_s {
	unsigned int reg[EQ_REG_NUM];
	unsigned int value[EQ_REG_NUM];
	unsigned int ctrl;
} hweq_t;

int rt5639_ioctl_common(struct snd_hwdep *hw, struct file *file,
			unsigned int cmd, unsigned long arg);
int rt5639_update_eqmode(
	struct snd_soc_codec *codec, int mode);

#endif /* __RT5639_IOCTL_H__ */
