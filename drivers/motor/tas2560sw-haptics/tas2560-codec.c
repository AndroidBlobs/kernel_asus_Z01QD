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
**     tas2560-codec.c
**
** Description:
**     ALSA SoC driver for Texas Instruments TAS2560 High Performance 4W Smart Amplifier
**
** =============================================================================
*/

#ifdef CONFIG_TAS2560_CODEC

//#define DEBUG
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
#include "tas2560-core.h"

#define TAS2560_MDELAY 0xFFFFFFFE
#define KCONTROL_CODEC
extern bool get_Channel_PowerStatus(struct tas2560_priv *pTAS2560, int channel);
extern void set_Channel_powerStatus(struct tas2560_priv *pTAS2560, int channel, bool status);
extern int tas2560_simple_enable(struct tas2560_priv *pTAS2560, int channel, bool bEnable);
static unsigned int tas2560_codec_read(struct snd_soc_codec *codec,  unsigned int reg)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	unsigned int value = 0;
	int ret;

	printk("%s\n", __func__);
	ret = pTAS2560->read(pTAS2560, reg, &value);
	if (ret >= 0)
		return value;
	else
		return ret;
}

static int tas2560_codec_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	printk("%s\n", __func__);
	return pTAS2560->write(pTAS2560, reg, value);
}

static int tas2560_codec_suspend(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&pTAS2560->codec_lock);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	pTAS2560->runtime_suspend(pTAS2560);

	mutex_unlock(&pTAS2560->codec_lock);
	return ret;
}

static int tas2560_codec_resume(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	mutex_lock(&pTAS2560->codec_lock);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	pTAS2560->runtime_resume(pTAS2560);

	mutex_unlock(&pTAS2560->codec_lock);
	return ret;
}

static int tas2560_AIF_post_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{

	//printk("tas2560-codec.c: %s\n", __func__);

	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		dev_dbg(pTAS2560->dev, "SND_SOC_DAPM_POST_PMU");
	break;
	case SND_SOC_DAPM_POST_PMD:
		dev_dbg(pTAS2560->dev, "SND_SOC_DAPM_POST_PMD");
	break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget tas2560_dapm_widgets[] = {
	SND_SOC_DAPM_AIF_IN_E("ASI1", "ASI1 Playback", 0, SND_SOC_NOPM, 0, 0,
		tas2560_AIF_post_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUT_DRV("ClassD", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_OUTPUT("OUT")
};

static const struct snd_soc_dapm_route tas2560_audio_map[] = {
	{"DAC", NULL, "ASI1"},
	{"ClassD", NULL, "DAC"},
	{"OUT", NULL, "ClassD"},
	{"DAC", NULL, "PLL"},
};

extern bool suspend_by_hall;
extern int hid_to_gpio_set(u8 gpio, u8 value);
extern int asus_wait4hid (void);

static int tas2560_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	//int i, err;

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	printk("tas2560-codec.c: %s:Channel=%d\n", __func__, pTAS2560->mnCurrentChannel);
	return 0;
}

static void tas2560_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	printk("tas2560-codec.c: %s\n", __func__);
}

static int tas2560_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	mutex_lock(&pTAS2560->codec_lock);
	//dev_dbg(pTAS2560->dev, "%s, %d\n", __func__, mute);
	printk("%s, Channel=%d, %s\n", __func__, pTAS2560->mnCurrentChannel, !mute ? "UnMute":"Mute");
	tas2560_enable(pTAS2560, !mute);
	mutex_unlock(&pTAS2560->codec_lock);
	return 0;
}

static int tas2560_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
			unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return ret;
}

static int tas2560_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);

	return 0;
}

static int tas2560_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	//printk("tas2560-codec.c: %s\n", __func__);

	return 0;
}

static int tas2560_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(pTAS2560->dev, "%s, format=0x%x\n", __func__, fmt);

	return ret;
}

static struct snd_soc_dai_ops tas2560_dai_ops = {
	.startup = tas2560_startup,
	.shutdown = tas2560_shutdown,
	.digital_mute = tas2560_mute,
	.hw_params  = tas2560_hw_params,
	.prepare    = tas2560_prepare,
	.set_sysclk = tas2560_set_dai_sysclk,
	.set_fmt    = tas2560_set_dai_fmt,
};

#define TAS2560_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)
static struct snd_soc_dai_driver tas2560_dai_driver[] = {
	{
		.name = "tas2560 ASI1",
		.id = 0,
		.playback = {
			.stream_name    = "ASI1 Playback",
			.channels_min   = 2,
			.channels_max   = 2,
			.rates      = SNDRV_PCM_RATE_8000_192000,
			.formats    = TAS2560_FORMATS,
		},
		.ops = &tas2560_dai_ops,
		.symmetric_rates = 1,
	},
};

static int tas2560_codec_probe(struct snd_soc_codec *codec)
{
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(pTAS2560->dev, "%s\n", __func__);
	
	pTAS2560->codec=codec;//get codec data for control i2s gpio

	return 0;
}

static int tas2560_codec_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static int tas2560_get_Sampling_Rate(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);

	pUcontrol->value.integer.value[0] = pTAS2560->mnSamplingRate;
	dev_dbg(pCodec->dev, "%s: %d\n", __func__,
			pTAS2560->mnSamplingRate);
	return 0;
}

static int tas2560_set_Sampling_Rate(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pUcontrol)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *pCodec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *pCodec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(pCodec);
	int sampleRate = pUcontrol->value.integer.value[0];

	mutex_lock(&pTAS2560->codec_lock);
	dev_dbg(pCodec->dev, "%s: %d\n", __func__, sampleRate);
	printk("%s: %d\n", __func__, sampleRate);
	tas2560_set_SampleRate(pTAS2560, sampleRate);
	mutex_unlock(&pTAS2560->codec_lock);

	return 0;
}

static int tas2560_power_ctrl_get(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);

	//pValue->value.integer.value[0] = pTAS2560->mbPowerUp;
	pValue->value.integer.value[0] = get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel);
	dev_dbg(codec->dev, "tas2560_power_ctrl_get = 0x%x\n",
					//pTAS2560->mbPowerUp);
					get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel));

	return 0;
}

static int tas2560_power_ctrl_put(struct snd_kcontrol *pKcontrol,
				struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int bPowerUp = pValue->value.integer.value[0];

	mutex_lock(&pTAS2560->codec_lock);
	tas2560_enable(pTAS2560, bPowerUp);
	mutex_unlock(&pTAS2560->codec_lock);

	return 0;
}

//stereo
static const char * const chl_setup_text[] = {
	"Phone", 			//channel=0x04
	"Station_All", 		//channel=0x03
	"Station_Left", 	//channel=0x01
	"Station_Right", 	//channel=0x02
	"All", 				//channel=0x07
};
static const struct soc_enum chl_setup_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(chl_setup_text), chl_setup_text),
};

static int tas2560_chl_setup_get(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int channel_state;

	mutex_lock(&pTAS2560->codec_lock);

	switch(pTAS2560->mnCurrentChannel){
		case 1://station left
			pValue->value.integer.value[0]=TAS2560_VIB_STATION_LEFT;
			break;
		case 2://station right
			pValue->value.integer.value[0]=TAS2560_VIB_STATION_RIGHT;
			break;
		case 3://station left and right
			pValue->value.integer.value[0]=TAS2560_VIB_STATION_ALL;
			break;
		case 4://phone
			pValue->value.integer.value[0]=TAS2560_VIB_PHONE;
			break;
		case 7://all vibrators
			pValue->value.integer.value[0]=TAS2560_VIB_ALL;
			break;
		default:
			pValue->value.integer.value[0]=TAS2560_VIB_PHONE;
			break;
	}
	mutex_unlock(&pTAS2560->codec_lock);
	channel_state = pValue->value.integer.value[0];
	printk("%s:pValue->value.integer.value[0]=%d\n", __func__, channel_state);
	printk("%s:dongleType=%d, channel=%d\n",__func__, pTAS2560->dongleType, pTAS2560->mnCurrentChannel);
	return 0;
}

static int tas2560_chl_setup_put(struct snd_kcontrol *pKcontrol,
			struct snd_ctl_elem_value *pValue)
{
#ifdef KCONTROL_CODEC
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(pKcontrol);
#else
	struct snd_soc_codec *codec = snd_kcontrol_chip(pKcontrol);
#endif
	struct tas2560_priv *pTAS2560 = snd_soc_codec_get_drvdata(codec);
	int channel_state = pValue->value.integer.value[0];

	printk("%s:pValue->value.integer.value[0]=%d\n", __func__, channel_state);
	mutex_lock(&pTAS2560->codec_lock);
	
	switch(channel_state){
		case TAS2560_VIB_PHONE:
			pTAS2560->mnCurrentChannel=0x04;
			break;
		case TAS2560_VIB_STATION_ALL:
			if(pTAS2560->dongleType==2){
				pTAS2560->mnCurrentChannel=0x03;
			}
			else{
				pTAS2560->mnCurrentChannel=0x04;
			}
			break;
		case TAS2560_VIB_STATION_LEFT:
			if(pTAS2560->dongleType==2){
				pTAS2560->mnCurrentChannel=0x01;
			}
			else{
				pTAS2560->mnCurrentChannel=0x04;
			}
			break;
		case TAS2560_VIB_STATION_RIGHT:
			if(pTAS2560->dongleType==2){
				pTAS2560->mnCurrentChannel=0x02;
			}
			else{
				pTAS2560->mnCurrentChannel=0x04;

			}
			break;
		case TAS2560_VIB_ALL:
			if(pTAS2560->dongleType==2){
				pTAS2560->mnCurrentChannel=0x07;
			}
			else{
				pTAS2560->mnCurrentChannel=0x04;
			}
			break;
		default:
			pTAS2560->mnCurrentChannel=0x04;
			break;
	}

	mutex_unlock(&pTAS2560->codec_lock);
	printk("%s:dongleType=%d, channel=%d\n",__func__, pTAS2560->dongleType, pTAS2560->mnCurrentChannel);
	return 0;
}

//end
static const char *Sampling_Rate_text[] = {"48_khz", "44.1_khz", "16_khz", "8_khz"};

static const struct soc_enum Sampling_Rate_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Sampling_Rate_text), Sampling_Rate_text),
};

/*
 * DAC digital volumes. From 0 to 15 dB in 1 dB steps
 */
static DECLARE_TLV_DB_SCALE(dac_tlv, 0, 100, 0);

static const struct snd_kcontrol_new tas2560_snd_controls[] = {
	SOC_SINGLE_TLV("DAC Playback Volume", TAS2560_SPK_CTRL_REG, 0, 0x0f, 0,
			dac_tlv),
	SOC_ENUM_EXT("TAS2560 Sampling Rate", Sampling_Rate_enum[0],
			tas2560_get_Sampling_Rate, tas2560_set_Sampling_Rate),
	SOC_SINGLE_EXT("TAS2560 PowerCtrl", SND_SOC_NOPM, 0, 0x0001, 0,
			tas2560_power_ctrl_get, tas2560_power_ctrl_put),
	SOC_ENUM_EXT("TAS2560 Channel Setup", chl_setup_enum[0],
		tas2560_chl_setup_get, tas2560_chl_setup_put),
};

static struct snd_soc_codec_driver soc_codec_driver_tas2560 = {
	.probe			= tas2560_codec_probe,
	.remove			= tas2560_codec_remove,
	.read			= tas2560_codec_read,
	.write			= tas2560_codec_write,
	.suspend		= tas2560_codec_suspend,
	.resume			= tas2560_codec_resume,
	.component_driver = {
		.controls		= tas2560_snd_controls,
		.num_controls		= ARRAY_SIZE(tas2560_snd_controls),
		.dapm_widgets		= tas2560_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(tas2560_dapm_widgets),
		.dapm_routes		= tas2560_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(tas2560_audio_map),
	},
};

int tas2560_register_codec(struct tas2560_priv *pTAS2560)
{
	int nResult = 0;

	dev_info(pTAS2560->dev, "%s, enter\n", __func__);
	nResult = snd_soc_register_codec(pTAS2560->dev,
		&soc_codec_driver_tas2560,
		tas2560_dai_driver, ARRAY_SIZE(tas2560_dai_driver));

	return nResult;
}

int tas2560_deregister_codec(struct tas2560_priv *pTAS2560)
{
	snd_soc_unregister_codec(pTAS2560->dev);

	return 0;
}

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2560 ALSA SOC Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif /* CONFIG_TAS2560_CODEC */
