/*
** =============================================================================
** Copyright (c) 2016  Texas Instruments Inc.
**
** This program is free software; you can redistribute it and/or modify it under
** the terms of the GNU General Public License as published by the Free Software
** Foundation; version 2.
**
** This program is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** File:
**     tas2560-core.c
**
** Description:
**     TAS2560 common functions for Android Linux
**
** =============================================================================
*/
//#define DEBUG
//#define TAS2560_HAPTICS
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "tas2560.h"

#define TAS2560_MDELAY 0xFFFFFFFE
#define TAS2560_MSLEEP 0xFFFFFFFD

#define MAX_CLIENTS 8
extern uint8_t gDongleType;
extern bool suspend_by_hall;
extern int hid_to_gpio_set(u8 gpio, u8 value);
extern int asus_wait4hid (void);
extern int check_i2c(struct tas2560_priv *pTAS2560);
extern void tas2560_sec_mi2s_enable(bool enable);
extern bool tas2560_sec_mi2s_status(void);
static unsigned int p_tas2560_default_data[] = {
	/* reg address			size	values	*/
	TAS2560_AUDIO_ENHANCER,	0x01,	0x03,
	TAS2560_SOFT_MUTE,	0x01,	0x01,
	//TAS2560_BOOST_CTRL,	0x01,	0x03,
	TAS2560_DEV_MODE_REG,	0x01,	0x02,
	TAS2560_SPK_CTRL_REG,	0x01,	0x5d,
	TAS2560_CLK_ERR_CTRL2,	0x01,	0x21,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_irq_config[] = {
	TAS2560_IRQ_PIN_REG,	0x01,	0x41,
	TAS2560_INT_MODE_REG,	0x01,	0x80,/* active high until INT_STICKY_1 and INT_STICKY_2 are read to be cleared. */
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_48kSR_BCLK_1P536_data[] = {
	TAS2560_CLK_SEL,		0x01,	0x01,	/* BCLK in, P = 1 */
	TAS2560_SET_FREQ,		0x01,	0x20,	/* J = 32 */
	TAS2560_PLL_D_MSB,		0x01,	0x00,	/* D = 0 */
	TAS2560_PLL_D_LSB,		0x01,	0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_48kSR_BCLK_3P072_data[] = {
	TAS2560_CLK_SEL,		0x01,	0x01,	/* BCLK in, P = 1 */
	TAS2560_SET_FREQ,		0x01,	0x10,	/* J = 16 */
	TAS2560_PLL_D_MSB,		0x01,	0x00,	/* D = 0 */
	TAS2560_PLL_D_LSB,		0x01,	0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_startup_data[] = {
	/* reg address			size	values	*/
	TAS2560_MUTE_REG,		0x01,	0x41,
	TAS2560_MDELAY,			0x01,	0x01,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_haptics_postpwrup[] = {
	TAS2560_MUTE_REG,		0x01,	0x40, /* Unmute */
	0xFFFFFFFF, 0xFFFFFFFF
};
#if 1
static unsigned int p_tas2560_disable_voltage_limiter[] = {
	/* reg address				size        values */
	TAS2560_VLIMIT_THRESHOLD,	0x04,	0x78, 0x00, 0x00, 0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};
#endif
static unsigned int p_tas2560_shutdown_data[] = {
	/* reg address			size	values	*/
	TAS2560_INT_GEN_REG,	0x01,	0x00,
	TAS2560_CLK_ERR_CTRL,	0x01,	0x00,
	TAS2560_MUTE_REG,		0x01,	0x01,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_idle_chnl_detect[] = {
	TAS2560_IDLE_CHNL_DETECT,	0x04,	0, 0, 0, 0,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_HPF_data[] = {
	/* reg address			size	values */
	/*all pass*/
	TAS2560_HPF_CUTOFF_CTL1,	0x04,	0x7F, 0xFF, 0xFF, 0xFF,
	TAS2560_HPF_CUTOFF_CTL2,	0x04,	0x00, 0x00, 0x00, 0x00,
	TAS2560_HPF_CUTOFF_CTL3,	0x04,	0x00, 0x00, 0x00, 0x00,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_48khz_data[] = {
	/* reg address			size	values */
	TAS2560_SR_CTRL1,		0x01,	0x01,
	TAS2560_SR_CTRL2,		0x01,	0x08,
	TAS2560_SR_CTRL3,		0x01,	0x10,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_16khz_data[] = {
	/* reg address			size	values */
	TAS2560_SR_CTRL1,		0x01,	0x01,
	TAS2560_SR_CTRL2,		0x01,	0x18,
	TAS2560_SR_CTRL3,		0x01,	0x20,
	0xFFFFFFFF, 0xFFFFFFFF
};

static unsigned int p_tas2560_8khz_data[] = {
	/* reg address			size	values */
	TAS2560_SR_CTRL1,		0x01,	0x01,
	TAS2560_SR_CTRL2,		0x01,	0x30,
	TAS2560_SR_CTRL3,		0x01,	0x20,
	0xFFFFFFFF, 0xFFFFFFFF
};

static int tas2560_i2c_load_data(struct tas2560_priv *pTAS2560, unsigned int *pData)
{
	unsigned int nRegister;
	unsigned int *nData;
	unsigned char Buf[128];
	unsigned int nLength = 0;
	unsigned int i = 0;
	unsigned int nSize = 0;
	int nResult = 0;

	do {
		nRegister = pData[nLength];
		nSize = pData[nLength + 1];
		nData = &pData[nLength + 2];
		if (nRegister == TAS2560_MSLEEP) {
			msleep(nData[0]);
			dev_dbg(pTAS2560->dev, "%s, msleep = %d\n",
				__func__, nData[0]);
		} else if (nRegister == TAS2560_MDELAY) {
			mdelay(nData[0]);
			dev_dbg(pTAS2560->dev, "%s, mdelay = %d\n",
				__func__, nData[0]);
		} else {
			if (nRegister != 0xFFFFFFFF) {
				if (nSize > 128) {
					dev_err(pTAS2560->dev,
						"%s, Line=%d, invalid size, maximum is 128 bytes!\n",
						__func__, __LINE__);
					break;
				}

				if (nSize > 1) {
					for (i = 0; i < nSize; i++)
						Buf[i] = (unsigned char)nData[i];
					nResult = pTAS2560->bulk_write(pTAS2560, nRegister, Buf, nSize);
					if (nResult < 0)
						break;
				} else if (nSize == 1) {
					nResult = pTAS2560->write(pTAS2560, nRegister, nData[0]);
					if (nResult < 0)
						break;
				} else {
					dev_err(pTAS2560->dev,
						"%s, Line=%d,invalid size, minimum is 1 bytes!\n",
						__func__, __LINE__);
				}
			}
		}
		nLength = nLength + 2 + pData[nLength + 1];
	} while (nRegister != 0xFFFFFFFF);

	return nResult;
}

void tas2560_sw_shutdown(struct tas2560_priv *pTAS2560, int sw_shutdown)
{
	dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, sw_shutdown);

	if (sw_shutdown)
		pTAS2560->update_bits(pTAS2560, TAS2560_PWR_REG,
			TAS2560_PWR_BIT_MASK, 0);
	else
		pTAS2560->update_bits(pTAS2560, TAS2560_PWR_REG,
			TAS2560_PWR_BIT_MASK, TAS2560_PWR_BIT_MASK);
}

int tas2560_set_SampleRate(struct tas2560_priv *pTAS2560, unsigned int nSamplingRate)
{
	int ret = 0;

	//dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, nSamplingRate);
	printk("tas2560-core.c:%s, %d\n", __func__, nSamplingRate);
	switch (nSamplingRate) {
	case 48000:
		tas2560_i2c_load_data(pTAS2560, p_tas2560_48khz_data);
		break;
	case 44100:
		pTAS2560->write(pTAS2560, TAS2560_SR_CTRL1, 0x11);
		break;
	case 16000:
		tas2560_i2c_load_data(pTAS2560, p_tas2560_16khz_data);
		break;
	case 8000:
		tas2560_i2c_load_data(pTAS2560, p_tas2560_8khz_data);
		break;
	default:
		dev_err(pTAS2560->dev, "Invalid Sampling rate, %d\n", nSamplingRate);
		ret = -1;
		break;
	}

	if (ret >= 0)
		pTAS2560->mnSamplingRate = nSamplingRate;

	return ret;
}

int tas2560_set_bit_rate(struct tas2560_priv *pTAS2560, unsigned int nBitRate)
{
	int ret = 0, n = -1;

	dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, nBitRate);
	switch (nBitRate) {
	case 16:
		n = 0;
	break;
	case 20:
		n = 1;
	break;
	case 24:
		n = 2;
	break;
	case 32:
		n = 3;
	break;
	}

	if (n >= 0)
		ret = pTAS2560->update_bits(pTAS2560,
			TAS2560_DAI_FMT, 0x03, n);

	return ret;
}

int tas2560_get_bit_rate(struct tas2560_priv *pTAS2560)
{
	int nBitRate = -1, value = -1, ret = 0;

	ret = pTAS2560->read(pTAS2560, TAS2560_DAI_FMT, &value);
	value &= 0x03;

	switch (value) {
	case 0:
		nBitRate = 16;
	break;
	case 1:
		nBitRate = 20;
	break;
	case 2:
		nBitRate = 24;
	break;
	case 3:
		nBitRate = 32;
	break;
	default:
	break;
	}

	return nBitRate;
}

int tas2560_set_ASI_fmt(struct tas2560_priv *pTAS2560, unsigned int fmt)
{
	u8 serial_format = 0, asi_cfg_1 = 0;
	int ret = 0;

	dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		asi_cfg_1 = 0x00;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		asi_cfg_1 = TAS2560_WCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		asi_cfg_1 = TAS2560_BCLKDIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		asi_cfg_1 = (TAS2560_BCLKDIR | TAS2560_WCLKDIR);
		break;
	default:
		dev_err(pTAS2560->dev, "ASI format master is not found\n");
		ret = -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		asi_cfg_1 |= 0x00;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		asi_cfg_1 |= TAS2560_WCLKINV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		asi_cfg_1 |= TAS2560_BCLKINV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		asi_cfg_1 = (TAS2560_WCLKINV | TAS2560_BCLKINV);
		break;
	default:
		dev_err(pTAS2560->dev, "ASI format Inverse is not found\n");
		ret = -EINVAL;
	}

	pTAS2560->update_bits(pTAS2560, TAS2560_ASI_CFG_1, TAS2560_DIRINV_MASK | 0x02,
			asi_cfg_1 | 0x02);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case (SND_SOC_DAIFMT_I2S):
		serial_format |= (TAS2560_DATAFORMAT_I2S << TAS2560_DATAFORMAT_SHIFT);
		break;
	case (SND_SOC_DAIFMT_DSP_A):
	case (SND_SOC_DAIFMT_DSP_B):
		serial_format |= (TAS2560_DATAFORMAT_DSP << TAS2560_DATAFORMAT_SHIFT);
		break;
	case (SND_SOC_DAIFMT_RIGHT_J):
		serial_format |= (TAS2560_DATAFORMAT_RIGHT_J << TAS2560_DATAFORMAT_SHIFT);
		break;
	case (SND_SOC_DAIFMT_LEFT_J):
		serial_format |= (TAS2560_DATAFORMAT_LEFT_J << TAS2560_DATAFORMAT_SHIFT);
		break;
	default:
		dev_err(pTAS2560->dev, "DAI Format is not found, fmt=0x%x\n", fmt);
		ret = -EINVAL;
		break;
	}

	pTAS2560->update_bits(pTAS2560, TAS2560_DAI_FMT, TAS2560_DAI_FMT_MASK,
			serial_format);

	return ret;
}

int tas2560_set_pll_clkin(struct tas2560_priv *pTAS2560, int clk_id,
		unsigned int freq)
{
	int ret = 0;
	unsigned char pll_in = 0;

	dev_dbg(pTAS2560->dev, "%s, clkid=%d, freq=%d\n", __func__, clk_id, freq);

	switch (clk_id) {
	case TAS2560_PLL_CLKIN_BCLK:
		pll_in = 0;
		break;
	case TAS2560_PLL_CLKIN_MCLK:
		pll_in = 1;
		break;
	case TAS2560_PLL_CLKIN_PDMCLK:
		pll_in = 2;
		break;
	default:
		dev_err(pTAS2560->dev, "Invalid clk id: %d\n", clk_id);
		ret = -EINVAL;
		break;
	}

	if (ret >= 0) {
		pTAS2560->update_bits(pTAS2560, TAS2560_CLK_SEL,
			TAS2560_PLL_SRC_MASK, pll_in << 6);
		pTAS2560->mnClkid = clk_id;
		pTAS2560->mnClkin = freq;
	}

	return ret;
}

int tas2560_get_volume(struct tas2560_priv *pTAS2560)
{
	int ret = -1;
	int value = -1;

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	ret = pTAS2560->read(pTAS2560, TAS2560_SPK_CTRL_REG, &value);
	if (ret >= 0)
		return (value & 0x0f);

	return ret;
}

int tas2560_set_volume(struct tas2560_priv *pTAS2560, int volume)
{
	int ret = -1;

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	ret = pTAS2560->update_bits(pTAS2560, TAS2560_SPK_CTRL_REG, 0x0f, volume&0x0f);

	return ret;
}

int tas2560_parse_dt(struct device *dev, struct tas2560_priv *pTAS2560)
{
	struct device_node *np = dev->of_node;
	int rc = 0, ret = 0;

	rc = of_property_read_u32(np, "ti,asi-format", &pTAS2560->mnASIFormat);
	if (rc) {
		dev_err(pTAS2560->dev, "Looking up %s property in node %s failed %d\n",
			"ti,asi-format", np->full_name, rc);
	} else {
		dev_dbg(pTAS2560->dev, "ti,asi-format=%d", pTAS2560->mnASIFormat);
	}

	pTAS2560->mnResetGPIO = of_get_named_gpio(np, "ti,reset-gpio", 0);
	if (!gpio_is_valid(pTAS2560->mnResetGPIO)) {
		dev_err(pTAS2560->dev, "Looking up %s property in node %s failed %d\n",
			"ti,reset-gpio", np->full_name, pTAS2560->mnResetGPIO);
	} else {
		dev_dbg(pTAS2560->dev, "ti,reset-gpio=%d", pTAS2560->mnResetGPIO);
	}

	pTAS2560->mnIRQGPIO = of_get_named_gpio(np, "ti,irq-gpio", 0);
	if (!gpio_is_valid(pTAS2560->mnIRQGPIO)) {
		dev_err(pTAS2560->dev, "Looking up %s property in node %s failed %d\n",
			"ti,irq-gpio", np->full_name, pTAS2560->mnIRQGPIO);
	} else {
		dev_dbg(pTAS2560->dev, "ti,irq-gpio=%d", pTAS2560->mnIRQGPIO);
	}

	rc = of_property_read_u32(np, "ti,pll", &pTAS2560->mnPLL);
	if (rc) {
		dev_err(pTAS2560->dev, "Looking up %s property in node %s failed %d\n",
			"ti,pll", np->full_name, rc);
	} else {
		dev_dbg(pTAS2560->dev, "ti,pll=%d", pTAS2560->mnPLL);
	}

	return ret;
}

int tas2560_load_default(struct tas2560_priv *pTAS2560)
{
	return tas2560_i2c_load_data(pTAS2560, p_tas2560_default_data);
}

int tas2560_load_platdata(struct tas2560_priv *pTAS2560)
{
	int nResult = 0;

	if (gpio_is_valid(pTAS2560->mnIRQGPIO)) {
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_irq_config);
		if (nResult < 0)
			goto end;
	}

	if (pTAS2560->mnPLL == PLL_BCLK_1P536_48KSR)
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_48kSR_BCLK_1P536_data);
	else if (pTAS2560->mnPLL == PLL_BCLK_3P072_48KSR)
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_48kSR_BCLK_3P072_data);
	if (nResult < 0)
		goto end;

	if ((pTAS2560->mnPLL == PLL_BCLK_1P536_48KSR)
		|| (pTAS2560->mnPLL == PLL_BCLK_3P072_48KSR))
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_48khz_data);
	if (nResult < 0)
		goto end;

	if (pTAS2560->mnASIFormat == TAS2560_DATAFORMAT_I2S) {
		nResult = tas2560_set_ASI_fmt(pTAS2560,
			SND_SOC_DAIFMT_CBS_CFS|SND_SOC_DAIFMT_NB_NF|SND_SOC_DAIFMT_I2S);
		if (nResult < 0)
			goto end;
		nResult = pTAS2560->write(pTAS2560, TAS2560_ASI_OFFSET_1, 0x00);
	} else if (pTAS2560->mnASIFormat == TAS2560_DATAFORMAT_DSP) {
		nResult = tas2560_set_ASI_fmt(pTAS2560,
			SND_SOC_DAIFMT_CBS_CFS|SND_SOC_DAIFMT_IB_IF|SND_SOC_DAIFMT_DSP_A);
		if (nResult < 0)
			goto end;
		nResult = pTAS2560->write(pTAS2560, TAS2560_ASI_OFFSET_1, 0x01);
	} else {
		dev_err(pTAS2560->dev, "need to implement!!!\n");
	}

	if (nResult < 0)
		goto end;

	nResult = tas2560_set_bit_rate(pTAS2560, 16);

end:

	return nResult;
}

static int tas2560_load_postpwrup(struct tas2560_priv *pTAS2560)
{
	int nResult = 0;

	nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_idle_chnl_detect);
	if (nResult < 0)
		goto end;

	nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_haptics_postpwrup);
	if (nResult < 0)
		goto end;

	nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_disable_voltage_limiter);
	if (nResult < 0)
		goto end;

	nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_HPF_data);
	if (nResult < 0)
		goto end;

	//set TAS2560_SPK_CTRL_REG to 0x0d from Immersion
	#if 0
	nResult = tas2560_set_volume(pTAS2560, 0x0d);//13db
	if (nResult < 0)
		goto end;
	#endif
	//end
end:
	return nResult;
}

int tas2560_LoadConfig(struct tas2560_priv *pTAS2560, bool bPowerOn)
{
	int nResult = 0;
	//int i, err;

	if (bPowerOn) {
		dev_dbg(pTAS2560->dev, "%s power down to load config\n", __func__);
		if (hrtimer_active(&pTAS2560->mtimer))
			hrtimer_cancel(&pTAS2560->mtimer);
		pTAS2560->enableIRQ(pTAS2560, false);
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_shutdown_data);
		if (nResult < 0)
			goto end;
	}

	pTAS2560->hw_reset(pTAS2560);

	nResult = pTAS2560->write(pTAS2560, TAS2560_SW_RESET_REG, 0x01);
	if (nResult < 0)
		goto end;
	msleep(1);

	nResult = tas2560_load_default(pTAS2560);
	if (nResult < 0) {
		goto end;
	}

	nResult = tas2560_load_platdata(pTAS2560);
	if (nResult < 0) {
		goto end;
	}

	if (bPowerOn) {
		dev_dbg(pTAS2560->dev, "%s power up\n", __func__);
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_startup_data);
		if (nResult < 0)
			goto end;
		nResult = tas2560_load_postpwrup(pTAS2560);
		if (nResult < 0)
			goto end;
		pTAS2560->enableIRQ(pTAS2560, true);
	}
end:
	return nResult;
}
bool get_Channel_PowerStatus(struct tas2560_priv *pTAS2560, int channel){
	switch(channel){
		case 1:
			return pTAS2560->mbPowerUp[0];//station left
			break;
		case 2:
			return pTAS2560->mbPowerUp[1];//station right
			break;
		case 3://both
			return pTAS2560->mbPowerUp[0];//station left
			break;
		case 4:
			return pTAS2560->mbPowerUp[2];//phone
			break;
		default:
			return pTAS2560->mbPowerUp[2];//phone
	}
}
void set_Channel_powerStatus(struct tas2560_priv *pTAS2560, int channel, bool status){
	switch(channel){
		case 1:
			pTAS2560->mbPowerUp[0]=status;//station left
			break;
		case 2:
			pTAS2560->mbPowerUp[1]=status;//station right
			break;
		case 3:
			pTAS2560->mbPowerUp[0]=status;
			pTAS2560->mbPowerUp[1]=status;
		case 4:
			pTAS2560->mbPowerUp[2]=status;//phone
			break;
		default:
			pTAS2560->mbPowerUp[2]=status;//phone
	}
}
int tas2560_simple_enable(struct tas2560_priv *pTAS2560, int channel, bool bEnable)
{
	int nResult = 0;
	int old_channel=-1;
	int i, err;

	old_channel=pTAS2560->mnCurrentChannel;
	pTAS2560->mnCurrentChannel=channel;
	if((channel&channel_both) &&(pTAS2560->dongleType==2) && suspend_by_hall){ 
		printk("[VIB]%s:station_reset_enable=%s\n", __func__, pTAS2560->station_reset_enable? "high":"low");
		if(!pTAS2560->station_reset_enable){ //station reset pin is low, need to be pull high
			for(i=0;i<2;i++){//wait twice, too long will block display
				err = asus_wait4hid();
				if (err){
					printk("[VIB] Fail to wait HID\n");
				}
				else
					break;
			}
			if(err==0){
				printk("[VIB] %s:station_reset_pin pull high...\n",__func__);
				hid_to_gpio_set(pTAS2560->mnSResetGPIO,1);
				pTAS2560->station_reset_enable=true;
				tas2560_LoadConfig(pTAS2560, true);//After hw reset, must use this function.
			}
			else
				printk("[VIB] %s:station_reset_pin pull high failed\n",__func__);			
		}
	}
	if (bEnable) {
		printk("tas2560-core.c:%s:channel=%d, power up +++\n", __func__, channel);
		pTAS2560->clearIRQ(pTAS2560);
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_startup_data);
		if (nResult < 0)
			goto end;
		nResult = tas2560_load_postpwrup(pTAS2560);
		if (nResult < 0)
			goto end;
		set_Channel_powerStatus(pTAS2560, channel, true);
		pTAS2560->enableIRQ(pTAS2560, true);
		printk("tas2560-core.c:%s:channel=%d, power up ---\n", __func__, channel);
	} else {
		printk("tas2560-core.c:%s:channel=%d, power down +++\n", __func__, channel);
		pTAS2560->enableIRQ(pTAS2560, false);
		nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_shutdown_data);
		set_Channel_powerStatus(pTAS2560, channel, false);
		printk("tas2560-core.c:%s:channel=%d, power down ---\n", __func__, channel);
	}
end:
	pTAS2560->mnCurrentChannel=old_channel;
	return nResult;
}


int tas2560_enable(struct tas2560_priv *pTAS2560, bool bEnable)
{
	int nResult = 0;
	int i, err;

	//check station connection status
	//if((pTAS2560->dongleType==2) && (pTAS2560->dongleType!=gDongleType)){
	if(pTAS2560->dongleType!=gDongleType){
		pTAS2560->dongleType=gDongleType;
		printk("tas2560-regmap.c:%s:dongleType changed...\n",__func__);		
		if(pTAS2560->dongleType==0)//no dongle, phone only
			pTAS2560->mnCurrentChannel=channel_phone;
		if(pTAS2560->dongleType==1)//inbox, phone only
			pTAS2560->mnCurrentChannel=channel_phone;
		if(pTAS2560->dongleType==2)//Station
		{
			//switch to station, the phone power state has to set power down.
			//station must be power up again, so station is set power down first.
			set_Channel_powerStatus(pTAS2560,pTAS2560->mnCurrentChannel,false);
			pTAS2560->mnCurrentChannel=channel_both;
			set_Channel_powerStatus(pTAS2560,pTAS2560->mnCurrentChannel,false);
		}
		if(pTAS2560->dongleType==3)//DT, phone only
			pTAS2560->mnCurrentChannel=channel_phone;
		if(pTAS2560->dongleType==4)//Unknow dongle, phone only
			pTAS2560->mnCurrentChannel=channel_phone;
		set_Channel_powerStatus(pTAS2560,pTAS2560->mnCurrentChannel,false);
		printk("tas2560-regmap.c:%s:dongleType=0x%0x\n",__func__, pTAS2560->dongleType);		
		printk("tas2560-regmap.c:%s:CurrentChannel=0x%0x\n",__func__, pTAS2560->mnCurrentChannel);	
	}
	if((pTAS2560->dongleType==2) && suspend_by_hall){ 
		//pTAS2560->sec_i2s_status=tas2560_sec_mi2s_status();
		printk("[VIB]%s:station_reset_enable=%s, sec_mi2s_status=%s\n", __func__, pTAS2560->station_reset_enable? "high":"low", tas2560_sec_mi2s_status() ? "true":"false");
		if(!pTAS2560->station_reset_enable && bEnable){ //station reset pin is low, and bEnable=true, need to be pull high
			for(i=0;i<2;i++){//wait twice, too long will block display
				err = asus_wait4hid();
				if (err){
					printk("[VIB] Fail to wait HID\n");
				}
				else
					break;
			}
			if(err==0){
				printk("[VIB] %s:station_reset_pin pull high...\n",__func__);
				hid_to_gpio_set(pTAS2560->mnSResetGPIO,1);
				pTAS2560->station_reset_enable=true;
				#if 1
				//check i2c status
				for(i=0;i<4;i++){
					nResult=check_i2c(pTAS2560);
					if (nResult<0){
						printk("[VIB]%s:i2c error and sleep 20ms (%d)\n",__func__, i+1);
						msleep(20);
					}
					else
						break;
				}
				if(i==4) goto end;
				//end
				#endif
				if(!tas2560_sec_mi2s_status()){
					printk("[VIB]%s:tas2560_sec_mi2s_enable\n",__func__);
					tas2560_sec_mi2s_enable(true);
				}
				tas2560_LoadConfig(pTAS2560, true);//After hw reset, must use this function.
			}
			else{
				printk("[VIB] %s:station_reset_pin pull high failed\n",__func__);
			}
		}
	}
	if (bEnable) {
		//if (!pTAS2560->mbPowerUp) {
		if (!get_Channel_PowerStatus(pTAS2560,pTAS2560->mnCurrentChannel) || (suspend_by_hall)) {
			//dev_dbg(pTAS2560->dev, "%s power up\n", __func__);
			printk("tas2560-core.c:%s:power up +++\n", __func__);
			pTAS2560->clearIRQ(pTAS2560);
			nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_startup_data);
			if (nResult < 0)
				goto end;
			nResult = tas2560_load_postpwrup(pTAS2560);
			if (nResult < 0)
				goto end;

			//pTAS2560->mbPowerUp = true;
			set_Channel_powerStatus(pTAS2560, pTAS2560->mnCurrentChannel, true);
			pTAS2560->enableIRQ(pTAS2560, true);
			printk("tas2560-core.c:%s:power up ---\n", __func__);
		}
		else
			printk("tas2560-core.c:%s:channel=%d, still power up\n", __func__, pTAS2560->mnCurrentChannel);
	} else {
		if(suspend_by_hall){
			printk("tas2560-core.c:%s:station cover is off, power down by station_reset low\n", __func__);
			//in this state, i2c maybe fail, need to pull low station reset pin
			if(pTAS2560->station_reset_enable){ //station reset pin is high, need to be pull low
				for(i=0;i<2;i++){//wait twice, too long will block display
					err = asus_wait4hid();
					if (err){
						printk("[VIB] Fail to wait HID\n");
					}
					else
						break;
				}
				if(err==0){
					printk("[VIB] %s:station_reset_pin pull low...\n",__func__);
					hid_to_gpio_set(pTAS2560->mnSResetGPIO,0);
					pTAS2560->station_reset_enable=false;
					set_Channel_powerStatus(pTAS2560, pTAS2560->mnCurrentChannel, false);
				}
				else{
					printk("[VIB] %s:station_reset_pin pull low failed\n",__func__);
				}
			}
			goto end;
		}
		//if (pTAS2560->mbPowerUp) {
		if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
			//dev_dbg(pTAS2560->dev, "%s power down\n", __func__);
			printk("tas2560-core.c:%s:power down +++\n", __func__);
			if (hrtimer_active(&pTAS2560->mtimer))
				hrtimer_cancel(&pTAS2560->mtimer);
			pTAS2560->enableIRQ(pTAS2560, false);
			nResult = tas2560_i2c_load_data(pTAS2560, p_tas2560_shutdown_data);
			//pTAS2560->mbPowerUp = false;
			set_Channel_powerStatus(pTAS2560, pTAS2560->mnCurrentChannel, false);
			printk("tas2560-core.c:%s:power down ---\n", __func__);
		}
	}
end:
	return nResult;
}

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2560 common functions for Android Linux");
MODULE_LICENSE("GPL v2");
