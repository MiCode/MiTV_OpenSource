#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/device.h>
#include "ad82088.h"
#include <linux/xiaomi/xiaomi_private_audio_interface.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static void ad82088_early_suspend(struct early_suspend *h);
static void ad82088_late_resume(struct early_suspend *h);
#endif

#define AD82088_RATES (SNDRV_PCM_RATE_32000 | \
		       SNDRV_PCM_RATE_44100 | \
		       SNDRV_PCM_RATE_48000 | \
		       SNDRV_PCM_RATE_64000 | \
		       SNDRV_PCM_RATE_88200 | \
		       SNDRV_PCM_RATE_96000 | \
		       SNDRV_PCM_RATE_176400 | \
		       SNDRV_PCM_RATE_192000)

#define AD82088_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S32_LE)

static unsigned int fade_speed;

static const DECLARE_TLV_DB_SCALE(mvol_tlv, -10300, 50, 1);
static const DECLARE_TLV_DB_SCALE(chvol_tlv, -10300, 50, 1);

static const struct snd_kcontrol_new ad82088_snd_controls[] = {
	SOC_SINGLE_TLV("AD82088 Master Volume", ESMT_MASTER_VOL, 0,
			0xe7, 1, mvol_tlv),
	SOC_SINGLE_TLV("Ch1 Volume", ESMT_CH1_VOL, 0,
			0xe7, 1, chvol_tlv),
	SOC_SINGLE_TLV("Ch2 Volume", ESMT_CH2_VOL, 0,
			0xe7, 1, chvol_tlv),
	SOC_SINGLE("Ch1 Switch", ESMT_STATE_CTL_3, 5, 1, 1),
	SOC_SINGLE("Ch2 Switch", ESMT_STATE_CTL_3, 4, 1, 1),
	/* This interface is old, now is no use */
	SOC_SINGLE_RANGE("Fine Master Volume", ESMT_CH3_VOL, 0,
			0x68, 0x6b, 1),
	SOC_SINGLE("Hard Mute", ESMT_STATE_CTL_3, 6, 1, 0),
};

/* Power-up register defaults */
static const
struct reg_default ad82088_reg_defaults[AD82088_REGISTER_COUNT] = {
	{0x00, 0x00},//##State_Control_1
	{0x01, 0x04},//##State_Control_2
	{0x02, 0x00},//##State_Control_3
	{0x03, 0x4e},//##Master_volume_control
	{0x04, 0x00},//##Channel_1_volume_control
	{0x05, 0x00},//##Channel_2_volume_control
	{0x06, 0x18},//##Channel_3_volume_control
	{0x07, 0x18},//##Channel_4_volume_control
	{0x08, 0x18},//##Channel_5_volume_control
	{0x09, 0x18},//##Channel_6_volume_control
	{0x0a, 0x10},//##Bass_Tone_Boost_and_Cut
	{0x0b, 0x10},//##treble_Tone_Boost_and_Cut
	{0x0c, 0x90},//##State_Control_4
	{0x0d, 0x00},//##Channel_1_configuration_registers
	{0x0e, 0x00},//##Channel_2_configuration_registers
	{0x0f, 0x00},//##Channel_3_configuration_registers
	{0x10, 0x00},//##Channel_4_configuration_registers
	{0x11, 0x00},//##Channel_5_configuration_registers
	{0x12, 0x00},//##Channel_6_configuration_registers
	{0x13, 0x00},//##Channel_7_configuration_registers
	{0x14, 0x00},//##Channel_8_configuration_registers
	{0x15, 0x6a},//##DRC1_limiter_attack/release_rate
	{0x16, 0x6a},//##DRC2_limiter_attack/release_rate
	{0x17, 0x6a},//##DRC3_limiter_attack/release_rate
	{0x18, 0x6a},//##DRC4_limiter_attack/release_rate
	{0x19, 0x06},//##Error_Delay
	{0x1a, 0x32},//##State_Control_5
	{0x1b, 0x01},//##HVUV_selection
	{0x1c, 0x00},//##State_Control_6
	{0x1d, 0x7f},//##Coefficient_RAM_Base_Address
	{0x1e, 0x00},//##Top_8-bits_of_coefficients_A1
	{0x1f, 0x00},//##Middle_8-bits_of_coefficients_A1
	{0x20, 0x00},//##Bottom_8-bits_of_coefficients_A1
	{0x21, 0x00},//##Top_8-bits_of_coefficients_A2
	{0x22, 0x00},//##Middle_8-bits_of_coefficients_A2
	{0x23, 0x00},//##Bottom_8-bits_of_coefficients_A2
	{0x24, 0x00},//##Top_8-bits_of_coefficients_B1
	{0x25, 0x00},//##Middle_8-bits_of_coefficients_B1
	{0x26, 0x00},//##Bottom_8-bits_of_coefficients_B1
	{0x27, 0x00},//##Top_8-bits_of_coefficients_B2
	{0x28, 0x00},//##Middle_8-bits_of_coefficients_B2
	{0x29, 0x00},//##Bottom_8-bits_of_coefficients_B2
	{0x2a, 0x40},//##Top_8-bits_of_coefficients_A0
	{0x2b, 0x00},//##Middle_8-bits_of_coefficients_A0
	{0x2c, 0x00},//##Bottom_8-bits_of_coefficients_A0
	{0x2d, 0x40},//##Coefficient_R/W_control
	{0x2e, 0x00},//##Protection_Enable/Disable
	{0x2f, 0x00},//##Memory_BIST_status
	{0x30, 0x00},//##Power_Stage_Status(Read_only)
	{0x31, 0x00},//##PWM_Output_Control
	{0x32, 0x00},//##Test_Mode_Control_Reg.
	{0x33, 0x6d},//##Qua-Ternary/Ternary_Switch_Level
	{0x34, 0x00},//##Volume_Fine_tune
	{0x35, 0x00},//##Volume_Fine_tune
	{0x36, 0x60},//##OC_bypass_&_GVDD_selection
	{0x37, 0x52},//##Device_ID_register
	{0x38, 0x00},//##RAM1_test_register_address
	{0x39, 0x00},//##Top_8-bits_of_RAM1_Data
	{0x3a, 0x00},//##Middle_8-bits_of_RAM1_Data
	{0x3b, 0x00},//##Bottom_8-bits_of_RAM1_Data
	{0x3c, 0x00},//##RAM1_test_r/w_control
	{0x3d, 0x00},//##RAM2_test_register_address
	{0x3e, 0x00},//##Top_8-bits_of_RAM2_Data
	{0x3f, 0x00},//##Middle_8-bits_of_RAM2_Data
	{0x40, 0x00},//##Bottom_8-bits_of_RAM2_Data
	{0x41, 0x00},//##RAM2_test_r/w_control
	{0x42, 0x00},//##Level_Meter_Clear
	{0x43, 0x00},//##Power_Meter_Clear
	{0x44, 0x02},//##TOP_of_C1_Level_Meter
	{0x45, 0xea},//##Middle_of_C1_Level_Meter
	{0x46, 0x49},//##Bottom_of_C1_Level_Meter
	{0x47, 0x02},//##TOP_of_C2_Level_Meter
	{0x48, 0xea},//##Middle_of_C2_Level_Meter
	{0x49, 0x49},//##Bottom_of_C2_Level_Meter
	{0x4a, 0x00},//##TOP_of_C3_Level_Meter
	{0x4b, 0x00},//##Middle_of_C3_Level_Meter
	{0x4c, 0x00},//##Bottom_of_C3_Level_Meter
	{0x4d, 0x00},//##TOP_of_C4_Level_Meter
	{0x4e, 0x00},//##Middle_of_C4_Level_Meter
	{0x4f, 0x00},//##Bottom_of_C4_Level_Meter
	{0x50, 0x00},//##TOP_of_C5_Level_Meter
	{0x51, 0x00},//##Middle_of_C5_Level_Meter
	{0x52, 0x00},//##Bottom_of_C5_Level_Meter
	{0x53, 0x00},//##TOP_of_C6_Level_Meter
	{0x54, 0x00},//##Middle_of_C6_Level_Meter
	{0x55, 0x00},//##Bottom_of_C6_Level_Meter
	{0x56, 0x00},//##TOP_of_C7_Level_Meter
	{0x57, 0x00},//##Middle_of_C7_Level_Meter
	{0x58, 0x00},//##Bottom_of_C7_Level_Meter
	{0x59, 0x00},//##TOP_of_C8_Level_Meter
	{0x5a, 0x00},//##Middle_of_C8_Level_Meter
	{0x5b, 0x00},//##Bottom_of_C8_Level_Meter
	{0x5c, 0x06},//##I2S_Data_Output_Selection_Register
	{0x5d, 0x00},//##Reserve
	{0x5e, 0x00},//##Reserve
	{0x5f, 0x00},//##Reserve
	{0x60, 0x00},//##Reserve
	{0x61, 0x00},//##Reserve
	{0x62, 0x00},//##Reserve
	{0x63, 0x00},//##Reserve
	{0x64, 0x00},//##Reserve
	{0x65, 0x00},//##Reserve
	{0x66, 0x00},//##Reserve
	{0x67, 0x00},//##Reserve
	{0x68, 0x00},//##Reserve
	{0x69, 0x00},//##Reserve
	{0x6a, 0x00},//##Reserve
	{0x6b, 0x00},//##Reserve
	{0x6c, 0x00},//##Reserve
	{0x6d, 0x00},//##Reserve
	{0x6e, 0x00},//##Reserve
	{0x6f, 0x00},//##Reserve
	{0x70, 0x00},//##Reserve
	{0x71, 0x00},//##Reserve
	{0x72, 0x00},//##Reserve
	{0x73, 0x00},//##Reserve
	{0x74, 0x00},//##Mono_Key_High_Byte
	{0x75, 0x00},//##Mono_Key_Low_Byte
	{0x76, 0x00},//##Boost_Control
	{0x77, 0x07},//##Hi-res_Item
	{0x78, 0x40},//##Test_Mode_register
	{0x79, 0x62},//##Boost_Strap_OV/UV_Selection
	{0x7a, 0x8c},//##OC_Selection_2
	{0x7b, 0x55},//##MBIST_User_Program_Top_Byte_Even
	{0x7c, 0x55},//##MBIST_User_Program_Middle_Byte_Even
	{0x7d, 0x55},//##MBIST_User_Program_Bottom_Byte_Even
	{0x7e, 0x55},//##MBIST_User_Program_Top_Byte_Odd
	{0x7f, 0x55},//##MBIST_User_Program_Middle_Byte_Odd
	{0x80, 0x55},//##MBIST_User_Program_Bottom_Byte_Odd
	{0x81, 0x00},//##ERROR_clear_register
	{0x82, 0x0c},//##Minimum_duty_test
	{0x83, 0x06},//##Reserve
	{0x84, 0xfe},//##Reserve
	{0x85, 0x6a},//##Reserve
};

#if 0
static const int m_reg_tab[][2] = {
	{0x00, 0x01},//##State_Control_1
	{0x01, 0x04},//##State_Control_2
	{0x02, 0x40},//##State_Control_3
	{0x03, 0x1d},//##Master_volume_control
	{0x04, 0x00},//##Channel_1_volume_control
	{0x05, 0x00},//##Channel_2_volume_control
	{0x06, 0x00},//##Channel_3_volume_control
	{0x07, 0x00},//##Channel_4_volume_control
	{0x08, 0x00},//##Channel_5_volume_control
	{0x09, 0x00},//##Channel_6_volume_control
	{0x0a, 0x10},//##Bass_Tone_Boost_and_Cut
	{0x0b, 0x10},//##treble_Tone_Boost_and_Cut
	{0x0c, 0x98},//##State_Control_4
	{0x0d, 0x00},//##Channel_1_configuration_registers
	{0x0e, 0x00},//##Channel_2_configuration_registers
	{0x0f, 0x00},//##Channel_3_configuration_registers
	{0x10, 0x00},//##Channel_4_configuration_registers
	{0x11, 0x00},//##Channel_5_configuration_registers
	{0x12, 0x00},//##Channel_6_configuration_registers
	{0x13, 0x00},//##Channel_7_configuration_registers
	{0x14, 0x00},//##Channel_8_configuration_registers
	{0x15, 0x6a},//##DRC1_limiter_attack/release_rate
	{0x16, 0x6a},//##DRC2_limiter_attack/release_rate
	{0x17, 0x6a},//##DRC3_limiter_attack/release_rate
	{0x18, 0x6a},//##DRC4_limiter_attack/release_rate
	{0x1a, 0x32},//##State_Control_5
	{0x1b, 0x01},//##HVUV_selection
	{0x1c, 0x40},//##State_Control_6
	{0x33, 0x6d},//##Qua-Ternary/Ternary_Switch_Level
	{0x34, 0x00},//##Volume_Fine_tune
	{0x35, 0x00},//##Volume_Fine_tune
	{0x5c, 0x06},//##I2S_Data_Output_Selection_Register
	{0x74, 0x00},//##Mono_Key_High_Byte
	{0x75, 0x00},//##Mono_Key_Low_Byte
};
#endif

/* codec private data */
struct ad82088_priv {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct ad82088_platform_data *pdata;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	unsigned char *init_regs;
	int init_length;
	unsigned char *ram1_regs;
	int ram1_length;
	unsigned char *ram2_regs;
	int ram2_length;
	unsigned char *ram1_null_regs;
	int ram1_null_length;
	unsigned char *ram2_null_regs;
	int ram2_null_length;
	struct soc_enum eq_conf_enum;
	int eq_cfg;
	int eq_enable;
};

static int ad82088_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int ad82088_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* Now we only use I2S */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad82088_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	unsigned int rate;

	rate = params_rate(params);
	pr_debug("rate: %u\n", rate);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_BE:
		pr_debug("24bit\n");
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S20_3BE:
		pr_debug("20bit\n");
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S16_BE:
		pr_debug("16bit\n");
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		pr_debug("32bit\n");
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ad82088_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	pr_debug("level = %d\n", level);
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->component.dapm.bias_level = level;

	return 0;
}

static const struct snd_soc_dai_ops ad82088_dai_ops = {
	.hw_params = ad82088_hw_params,
	.set_sysclk = ad82088_set_dai_sysclk,
	.set_fmt = ad82088_set_dai_fmt,
};

static struct snd_soc_dai_driver ad82088_dai = {
	.name = "ad82088",
	.playback = {
		.stream_name = "HIFI Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = AD82088_RATES,
		.formats = AD82088_FORMATS,
	},
	.ops = &ad82088_dai_ops,
};

static int config_ram_register(struct snd_soc_codec *codec,
			unsigned char *buffer, int reg_num, int ram_num)
{
	unsigned int data[1];
	unsigned int mute_status;
	int ret = 0;

	if (buffer == NULL || reg_num == 0) {
		pr_err("The dts ram reg is NULL.\n");
		return -1;
	}

	if (reg_num % 4 != 0) {
		pr_err("The dts ram reg size is not right.\n");
		return -1;
	}

	/* all channel mute */
	mute_status = snd_soc_read(codec, ESMT_STATE_CTL_3);
	data[0] = 0x40;
	snd_soc_write(codec,
			ESMT_STATE_CTL_3, data[0]);

	while (reg_num > 0) {
#if 0
		data[0] = snd_soc_read(codec, ESMT_RAM_WR_STATUS);
		if ((data[0] & 0x3f) != 0x00) {
			pr_err("The ram status reg is not right: %d\n",
				data[0]);
			ret = -1;
			goto err;
		}
#endif
		if (ram_num == 1) {
			if (((*buffer >= ESMT_CH1_EQ1) &&
				(*buffer <= ESMT_CH1_EQ15))
				|| ((*buffer >= ESMT_CH1_DEQ1)
				&& (*buffer <= ESMT_CH1_DEQ4))) {
				/* address */
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *buffer);
				/* set 0 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1,
						*(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1,
						*(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1,
						*(buffer+3));
				/* set 1 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A2,
						*(buffer+5));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A2,
						*(buffer+6));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A2,
						*(buffer+7));
				/* set 3 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B1,
						*(buffer+9));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B1,
						*(buffer+10));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B1,
						*(buffer+11));
				/* set 4 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B2,
						*(buffer+13));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B2,
						*(buffer+14));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B2,
						*(buffer+15));
				/* set 5 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A0,
						*(buffer+17));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A0,
						*(buffer+18));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A0,
						*(buffer+19));
				/* set WA bit to write a set(6*3=18byte) */
				data[0] = 0x02;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);
				mdelay(10);
				buffer += 4*5;
				reg_num -= 4*5;
			} else if ((*buffer >= ESMT_L_SRS_HPF) &&
					(*buffer <= ESMT_L_SRS_LPF)) {
				/* address */
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *(buffer));
				/* part 1 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A0,
						*(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A0,
						*(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A0,
						*(buffer+3));
				/* part 2 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1,
						*(buffer+5));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1,
						*(buffer+6));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1,
						*(buffer+7));
				/* part 3 */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B1,
						*(buffer+9));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B1,
						*(buffer+10));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B1,
						*(buffer+11));
				/* W3 to write three coff to RAM */
				data[0] = 0x10;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);

				mdelay(10);
				buffer += 4*3;
				reg_num -= 4*3;
			} else if ((*buffer >= ESMT_RESERVED_3) &&
					(*buffer <= ESMT_RESERVED_4)) {
				/* reserve register */
				buffer += 4;
				reg_num -= 4;
			} else if (*buffer <= ESMT_DRC7_POWER) {
				/* address */
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *(buffer));
				/* single */
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1, *(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1, *(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1, *(buffer+3));
				/* W1 to write a single coff to RAM */
				data[0] = 0x01;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);

				mdelay(1);
				buffer += 4;
				reg_num -= 4;
			} else {
				pr_err("The dts ram reg is out of range: %d\n",
						*buffer);
				ret = -1;
				goto err;
			}
		} else if (ram_num == 2) {
			if (((*buffer >= ESMT_CH1_EQ1) &&
				(*buffer <= ESMT_CH1_EQ15))
				|| ((*buffer >= ESMT_CH1_DEQ1)
				&& (*buffer <= ESMT_CH1_DEQ4))) {
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *(buffer));

				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1,
						*(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1,
						*(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1,
						*(buffer+3));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A2,
						*(buffer+5));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A2,
						*(buffer+6));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A2,
						*(buffer+7));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B1,
						*(buffer+9));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B1,
						*(buffer+10));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B1,
						*(buffer+11));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B2,
						*(buffer+13));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B2,
						*(buffer+14));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B2,
						*(buffer+15));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A0,
						*(buffer+17));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A0,
						*(buffer+18));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A0,
						*(buffer+19));

				data[0] = 0x42;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);

				mdelay(10);
				buffer += 4*5;
				reg_num -= 4*5;
			} else if ((*buffer >= ESMT_L_SRS_HPF) &&
					(*buffer <= ESMT_L_SRS_LPF)) {
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *(buffer));

				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A0,
						*(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A0,
						*(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A0,
						*(buffer+3));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1,
						*(buffer+5));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1,
						*(buffer+6));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1,
						*(buffer+7));
				snd_soc_write(codec,
						ESMT_RAM_TOP_8_B1,
						*(buffer+9));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_B1,
						*(buffer+10));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_B1,
						*(buffer+11));

				data[0] = 0x50;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);

				mdelay(10);
				buffer += 4*3;
				reg_num -= 4*3;
			} else if (((*buffer >= ESMT_RESERVED_1) &&
					(*buffer <= ESMT_RESERVED_2)) ||
					((*buffer >= ESMT_RESERVED_3) &&
					(*buffer <= ESMT_RESERVED_4))) {
				/* reserve register */
				buffer += 4;
				reg_num -= 4;
			} else if (*buffer <= ESMT_DRC7_POWER) {
				snd_soc_write(codec,
						ESMT_RAM_REG_ADDR, *(buffer));

				snd_soc_write(codec,
						ESMT_RAM_TOP_8_A1, *(buffer+1));
				snd_soc_write(codec,
						ESMT_RAM_MID_8_A1, *(buffer+2));
				snd_soc_write(codec,
						ESMT_RAM_BOT_8_A1, *(buffer+3));

				data[0] = 0x41;
				snd_soc_write(codec,
						ESMT_RAM_WR_STATUS, data[0]);

				mdelay(1);
				buffer += 4;
				reg_num -= 4;
			} else {
				pr_err("The dts ram reg is out of range: %d\n",
						*buffer);
				ret = -1;
				goto err;
			}
		}
	}
err:
	/* all channel mute status recover */
	snd_soc_write(codec,
			ESMT_STATE_CTL_3, mute_status);

	return ret;
}

static int ad82088_put_eq_enum(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);
	int value = ucontrol->value.enumerated.item[0];

	if (value >= 2)
		return -EINVAL;

	ad82088->eq_cfg = value;

	if (value == 0) {
		pr_info("Enable EQ DRC follow the table\n");
		config_ram_register(codec, ad82088->ram1_regs,
					ad82088->ram1_length, AD82088_BANK_1);
		config_ram_register(codec, ad82088->ram2_regs,
					ad82088->ram2_length, AD82088_BANK_2);
	} else if (value == 1) {
		pr_info("Disable EQ DRC follow the null\n");
		config_ram_register(codec, ad82088->ram1_null_regs,
				ad82088->ram1_null_length, AD82088_BANK_1);
		config_ram_register(codec, ad82088->ram2_null_regs,
				ad82088->ram2_null_length, AD82088_BANK_2);
	}
	return 0;
}

static int ad82088_get_eq_enum(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = ad82088->eq_cfg;

	return 0;
}

/*
 * There are three kind to read RAM registers:
 *     one set coff
 *     single  coff
 *     three   coff
 * And you can use "single read" to read all registers
 **/
static int access_ram_register_single(struct snd_soc_codec *codec,
		int ram_bank, int address, unsigned char *buffer)
{
	unsigned int data[1];
	int ret = 0;

	if (buffer == NULL) {
		pr_err("%s: no buffer to store data read\n", __func__);
		return -1;
	}

	if (address > (ESMT_CH1_DEQ4+4)) {
		pr_err("%s: wrong ram address 0x%02x", __func__, address);
		return -1;
	}

	if (ram_bank < AD82088_BANK_1 || ram_bank > AD82088_BANK_2) {
		pr_err("%s: wrong bank number %d\n", __func__, ram_bank);
		return -1;
	}

	/* address */
	snd_soc_write(codec, ESMT_RAM_REG_ADDR, address);

	/* set read flag */
	data[0] = 0x00;
	data[0] |= (ram_bank-1)<<ESMT_RAM_WR_BIT_RBS;
	data[0] |= 1<<ESMT_RAM_WR_BIT_R1;
	snd_soc_write(codec, ESMT_RAM_WR_STATUS, data[0]);

	/* wait some time for data ready */
	mdelay(1);

	/* single read, three bytes per ram-register */
	buffer[0] = snd_soc_read(codec, ESMT_RAM_TOP_8_A1);
	buffer[1] = snd_soc_read(codec, ESMT_RAM_MID_8_A1);
	buffer[2] = snd_soc_read(codec, ESMT_RAM_BOT_8_A1);

	return ret;
}

static int reset_ad82088_GPIO(struct snd_soc_codec *codec)
{
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);
	struct ad82088_platform_data *pdata = ad82088->pdata;

	if (pdata->pdn_pin != 0 && pdata->reset_pin != 0) {
		int value;

		value = pdata->pdn_pin_active_low ? GPIOF_OUT_INIT_LOW :
				GPIOF_OUT_INIT_HIGH;
		gpio_direction_output(pdata->pdn_pin,
					value);

		value = pdata->reset_pin_active_low ? GPIOF_OUT_INIT_LOW :
				GPIOF_OUT_INIT_HIGH;
		gpio_direction_output(pdata->reset_pin,
					value);

		mdelay(30);

		value = pdata->reset_pin_active_low ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW;
		gpio_direction_output(pdata->reset_pin,
					value);

		mdelay(10);

		value = pdata->pdn_pin_active_low ? GPIOF_OUT_INIT_HIGH :
				GPIOF_OUT_INIT_LOW;
		gpio_direction_output(pdata->pdn_pin,
					value);

		mdelay(30);
	}
	return 0;
}

static int ad82088_init(struct snd_soc_codec *codec)
{
	int i;
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "ad82088_init!\n");
	reset_ad82088_GPIO(codec);

	if ((ad82088->init_regs != NULL) && (ad82088->init_length != 0)) {
		for (i = 0; i < ad82088->init_length / 2; i++)
			snd_soc_write(codec,
			*(unsigned char *)(ad82088->init_regs+i*2),
			*(unsigned char *)(ad82088->init_regs+i*2+1));
	}

	config_ram_register(codec, ad82088->ram1_regs,
			ad82088->ram1_length, AD82088_BANK_1);
	config_ram_register(codec, ad82088->ram2_regs,
			ad82088->ram2_length, AD82088_BANK_2);
	ad82088->eq_cfg = 0;
	/* unmute */
	snd_soc_write(codec, ESMT_STATE_CTL_3, 0x00);

	fade_speed = snd_soc_read(codec, ESMT_STATE_CTL_7) & FADE_SPEED_BIT;

	return 0;
}

static const char *const eq_texts[] = { "Enable", "Disable" };

#ifdef CONFIG_MITV_AUDIO_CODEC_DEBUG
#include "ad82088_debug.c"
#endif

static int ad82088_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ad82088->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	ad82088->early_suspend.suspend = ad82088_early_suspend;
	ad82088->early_suspend.resume = ad82088_late_resume;
	ad82088->early_suspend.param = codec;
	register_early_suspend(&(ad82088->early_suspend));
#endif
	ad82088_init(codec);

	{
		struct snd_kcontrol_new control =
			SOC_ENUM_EXT("EQ/DRC Enable", ad82088->eq_conf_enum,
				ad82088_get_eq_enum, ad82088_put_eq_enum);

		ad82088->eq_conf_enum.items = 2;
		ad82088->eq_conf_enum.texts = eq_texts;

		ret = snd_soc_add_codec_controls(codec, &control, 1);
		if (ret != 0)
			dev_err(codec->dev,
					"Fail to add EQ mode control: %d\n",
					ret);
	}

	ad82088->codec = codec;
#ifdef CONFIG_MITV_AUDIO_CODEC_DEBUG
	ret = sysfs_create_group(&codec->dev->kobj,
		&ad82088_group);
	if (ret < 0)
		dev_err(codec->dev, "Create sys fs node fail: %d\n", ret);
	else {
		if (xiaomi_get_speaker_class() != NULL) {
			ret = class_compat_create_link_with_name(
				xiaomi_get_speaker_class(),
				codec->dev,
				"AMP");
			if (ret)
				dev_warn(codec->dev,
					"Failed to create compatibility class link\n");
		}
	}
#endif

	return 0;
}

static int ad82088_remove(struct snd_soc_codec *codec)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct ad82088_priv *ad82088 = snd_soc_codec_get_drvdata(codec);

	unregister_early_suspend(&(ad82088->early_suspend));
#endif

#ifdef CONFIG_MITV_AUDIO_CODEC_DEBUG
	sysfs_remove_group(&codec->dev->kobj,
		&ad82088_group);
	if (xiaomi_get_speaker_class() != NULL)
		class_compat_remove_link_with_name(xiaomi_get_speaker_class(),
			codec->dev,
			"AMP");
#endif
	return 0;
}

#ifdef CONFIG_PM
static int ad82088_suspend(struct snd_soc_codec *codec)
{
	dev_info(codec->dev, "ad82088_suspend!\n");

	return 0;
}

static int ad82088_resume(struct snd_soc_codec *codec)
{
	dev_info(codec->dev, "ad82088_resume!\n");

	return 0;
}
#else
#define ad82088_suspend NULL
#define ad82088_resume NULL
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ad82088_early_suspend(struct early_suspend *h)
{
}

static void ad82088_late_resume(struct early_suspend *h)
{
}
#endif

static const struct snd_soc_dapm_widget ad82088_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "HIFI Playback", SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_codec_driver soc_codec_dev_ad82088 = {
	.probe = ad82088_probe,
	.remove = ad82088_remove,
	.suspend = ad82088_suspend,
	.resume = ad82088_resume,
	.set_bias_level = ad82088_set_bias_level,
	.component_driver = {
		.controls = ad82088_snd_controls,
		.num_controls = ARRAY_SIZE(ad82088_snd_controls),
		.dapm_widgets = ad82088_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(ad82088_dapm_widgets),
	}
};

static const struct regmap_config ad82088_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AD82088_REGISTER_COUNT,
	.reg_defaults = ad82088_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ad82088_reg_defaults),
	.cache_type = REGCACHE_NONE,
};

static void *alloc_and_get_data_array(struct device_node *p_node, char *str,
		int *lenp)
{
	int ret = 0, length = 0;
	char *p = NULL;

	if (of_find_property(p_node, str, &length) == NULL) {
		pr_err("DTD of %s not found!\n", str);
		goto exit;
	}
	pr_debug("prop name=%s,length=%d\n", str, length);
	p = kcalloc(length, sizeof(char *), GFP_KERNEL);
	if (p == NULL) {
		pr_err("ERROR, NO enough mem for %s!\n", str);
		length = 0;
		goto exit;
	}

	ret = of_property_read_u8_array(p_node, str, p, length);
	if (ret) {
		pr_err("no of property %s!\n", str);
		kfree(p);
		p = NULL;
		goto exit;
	}

	*lenp = length;

exit: return p;
}

static char init_reg[15] = "init_reg";
static char table_ram1[15] = "table_ram1";
static char table_ram2[15] = "table_ram2";

static int speaker_select(void)
{
	switch (xiaomi_get_speaker_type()) {
	case XIAOMI_SPK_TYPE_08W:
		sprintf(init_reg, "%s", "init_reg_n");
		sprintf(table_ram1, "%s", "table_ram1_n");
		sprintf(table_ram2, "%s", "table_ram2_n");
		break;
	case XIAOMI_SPK_TYPE_MAX:
		pr_info("ad82088 select eq/drc table failed, use default\n");
	case XIAOMI_SPK_TYPE_10W:
	default:
		sprintf(init_reg, "%s", "init_reg");
		sprintf(table_ram1, "%s", "table_ram1");
		sprintf(table_ram2, "%s", "table_ram2");
		break;
	}
	pr_info("xiaomi, ad82088, select eq/drc table: %s/%s/%s\n",
			init_reg, table_ram1, table_ram2);
	return 0;
}

static int ad82088_parse_dts(struct ad82088_priv *ad82088,
							struct device_node *np)
{
	int ret = 0;
	int pin;
	enum of_gpio_flags flags;

	int length = 0;
	char *regs = NULL;

	speaker_select();

	regs = alloc_and_get_data_array(np, init_reg, &length);
	if (regs == NULL) {
		pr_err("%s fail to get init_reg from dts!\n", __func__);
		ret = -1;
	} else {
		ad82088->init_regs = regs;
		ad82088->init_length = length;
	}

	regs = alloc_and_get_data_array(np, table_ram1, &length);
	if (regs == NULL) {
		pr_err("%s fail to get table_ram1 from dts!\n", __func__);
		ret = -1;
	} else {
		ad82088->ram1_regs = regs;
		ad82088->ram1_length = length;
	}

	regs = alloc_and_get_data_array(np, table_ram2, &length);
	if (regs == NULL) {
		pr_err("%s fail to get table_ram2 from dts!\n", __func__);
		ret = -1;
	} else {
		ad82088->ram2_regs = regs;
		ad82088->ram2_length = length;
	}

	regs = alloc_and_get_data_array(np, "null_ram1", &length);
	if (regs == NULL) {
		pr_err("%s fail to get null_ram1 from dts!\n", __func__);
		ret = -1;
	} else {
		ad82088->ram1_null_regs = regs;
		ad82088->ram1_null_length = length;
	}

	regs = alloc_and_get_data_array(np, "null_ram2", &length);
	if (regs == NULL) {
		pr_err("%s fail to get null_ram2 from dts!\n", __func__);
		ret = -1;
	} else {
		ad82088->ram2_null_regs = regs;
		ad82088->ram2_null_length = length;
	}

	pin = of_get_named_gpio_flags(np, "reset_pin", 0, &flags);
	if (pin < 0) {
		pr_err("%s fail to get reset pin from dts!\n", __func__);
		ret = -1;
	} else {
		gpio_request(pin, "codec_reset");
		ad82088->pdata->reset_pin = pin;
		ad82088->pdata->reset_pin_active_low =
						flags & OF_GPIO_ACTIVE_LOW;
		pr_info("%s pdata->reset_pin = %d!\n", __func__,
				pin);
	}

	pin = of_get_named_gpio_flags(np, "pdn_pin", 0, &flags);
	if (pin < 0) {
		pr_err("%s fail to get pdn pin from dts!\n", __func__);
		ret = -1;
	} else {
		gpio_request(pin, "codec_pdn");
		ad82088->pdata->pdn_pin = pin;
		ad82088->pdata->pdn_pin_active_low = flags & OF_GPIO_ACTIVE_LOW;
		pr_info("%s pdata->pdn_pin = %d!\n", __func__,
				pin);
	}

	return ret;
}

static int ad82088_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct ad82088_priv *ad82088;
	struct ad82088_platform_data *pdata;
	int ret;

	ad82088 = devm_kzalloc(&i2c->dev, sizeof(struct ad82088_priv),
			       GFP_KERNEL);
	if (!ad82088)
		return -ENOMEM;

	ad82088->regmap = devm_regmap_init_i2c(i2c, &ad82088_regmap);
	if (IS_ERR(ad82088->regmap)) {
		ret = PTR_ERR(ad82088->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(i2c, ad82088);

	pdata = devm_kzalloc(&i2c->dev,
				sizeof(struct ad82088_platform_data),
				GFP_KERNEL);
	if (!pdata) {
		pr_err("%s failed to kzalloc for ad82088 pdata\n", __func__);
		return -ENOMEM;
	}
	ad82088->pdata = pdata;

	ad82088_parse_dts(ad82088, i2c->dev.of_node);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_ad82088,
				     &ad82088_dai, 1);
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to register codec (%d)\n", ret);

	return ret;
}

static int ad82088_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);

	return 0;
}

static void ad82088_i2c_shutdown(struct i2c_client *client)
{
	struct ad82088_priv *ad82088 = i2c_get_clientdata(client);
	struct ad82088_platform_data *pdata = ad82088->pdata;

	dev_info(&client->dev, "Enter into ad82088 shutdown\n");

	if (pdata->pdn_pin != 0 && pdata->reset_pin != 0) {
		int value;
		unsigned long ms;

		value = pdata->pdn_pin_active_low ? GPIOF_OUT_INIT_LOW :
				GPIOF_OUT_INIT_HIGH;
		gpio_direction_output(pdata->pdn_pin,
					value);

		ms = fade_speed ? 300 : 40;
		mdelay(ms);

		value = pdata->reset_pin_active_low ? GPIOF_OUT_INIT_LOW :
				GPIOF_OUT_INIT_HIGH;
		gpio_direction_output(pdata->reset_pin,
					value);
		udelay(100);
	}

	return;
}

static const struct i2c_device_id ad82088_i2c_id[] = {
	{ "ad82088", 0 },
	{}
};

static const struct of_device_id ad82088_of_id[] = {
	{ .compatible = "ESMT, ad82088", },
	{ /* senitel */ }
};
MODULE_DEVICE_TABLE(of, ad82088_of_id);

static struct i2c_driver ad82088_i2c_driver = {
	.driver = {
		.name = "ad82088",
		.of_match_table = ad82088_of_id,
		.owner = THIS_MODULE,
	},
	.probe = ad82088_i2c_probe,
	.remove = ad82088_i2c_remove,
	.shutdown = ad82088_i2c_shutdown,
	.id_table = ad82088_i2c_id,
};

module_i2c_driver(ad82088_i2c_driver);

MODULE_DESCRIPTION("ASoC ad82088 driver");
MODULE_AUTHOR("MITV team");
MODULE_LICENSE("GPL");
