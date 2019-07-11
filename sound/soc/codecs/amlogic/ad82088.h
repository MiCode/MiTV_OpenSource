#ifndef _AD82088_H_
#define _AD82088_H_

#define AD82088_REGISTER_COUNT	0x86

#define ESMT_STATE_CTL_1        0x00
#define ESMT_STATE_CTL_2        0x01
#define ESMT_STATE_CTL_3        0x02
#define ESMT_MASTER_VOL         0x03
#define ESMT_CH1_VOL            0x04
#define ESMT_CH2_VOL            0x05
#define ESMT_CH3_VOL            0x06
#define ESMT_STATE_CTL_7        0x1c
#define FADE_SPEED_BIT			0x04
#define ESMT_BASS_TONE_BO0ST    0x0A
#define ESMT_TREBLE_TONE_BOOST  0x0B
#define ESMT_STATE_CTL_4        0x0C
#define ESMT_CH1_CONF           0x0D
#define ESMT_CH2_CONF           0x0E
#define ESMT_CH3_CONF           0x0F

#define ESMT_DRC1_ATT_REL_RATE  0x15
#define ESMT_STATE_CTL_5        0x1A
#define ESMT_STATE_CTL_6        0x1B
/* #define ESMT_STATE_CTL_7        0x1C */

#define ESMT_RAM_REG_ADDR       0x1D
#define ESMT_RAM_TOP_8_A1       0x1E
#define ESMT_RAM_MID_8_A1       0x1F
#define ESMT_RAM_BOT_8_A1       0x20
#define ESMT_RAM_TOP_8_A2       0x21
#define ESMT_RAM_MID_8_A2       0x22
#define ESMT_RAM_BOT_8_A2       0x23
#define ESMT_RAM_TOP_8_B1       0x24
#define ESMT_RAM_MID_8_B1       0x25
#define ESMT_RAM_BOT_8_B1       0x26
#define ESMT_RAM_TOP_8_B2       0x27
#define ESMT_RAM_MID_8_B2       0x28
#define ESMT_RAM_BOT_8_B2       0x29
#define ESMT_RAM_TOP_8_A0       0x2a
#define ESMT_RAM_MID_8_A0       0x2b
#define ESMT_RAM_BOT_8_A0       0x2c
#define ESMT_RAM_WR_STATUS      0x2d

#define ESMT_TERNARY_SW_LVL     0x33
#define ESMT_VOL_FINE_TUNE_L    0x34
#define ESMT_VOL_FINE_TUNE_H    0x35
#define ESMT_DEVICE_ID          0x37

#define ESMT_I2S_DATA_OUT       0x5C

#define ESMT_MONO_KEY_H         0x74
#define ESMT_MONO_KEY_L         0x75


#define ESMT_RAM_WR_BIT_RBS     (6)
#define ESMT_RAM_WR_BIT_R3      (5)
#define ESMT_RAM_WR_BIT_W3      (4)
#define ESMT_RAM_WR_BIT_RA      (3)
#define ESMT_RAM_WR_BIT_R1      (2)
#define ESMT_RAM_WR_BIT_WA      (1)
#define ESMT_RAM_WR_BIT_W1      (0)

#define ESMT_CH1_EQ1			0x00
#define ESMT_CH1_EQ15			0x46
#define ESMT_L_SRS_HPF			0x4f
#define ESMT_L_SRS_LPF			0x52
#define ESMT_DRC7_POWER			0x67
#define ESMT_CH1_DEQ1			0x68
#define ESMT_CH1_DEQ4			0x77

#define ESMT_CH2_EQ1			0x00
#define ESMT_CH2_EQ15			0x46
#define ESMT_R_SRS_HPF			0x4f
#define ESMT_R_SRS_LPF			0x52
#define ESMT_RESERVED_1			0x55
#define ESMT_RESERVED_2			0x63
#define ESMT_CH2_DEQ1			0x68
#define ESMT_CH2_DEQ4			0x77
#define ESMT_RESERVED_3			0x7c
#define ESMT_RESERVED_4			0x7f

struct ad82088_platform_data {
	int reset_pin;
	int reset_pin_active_low;
	int pdn_pin;
	int pdn_pin_active_low;
};

#ifdef CONFIG_MITV_AUDIO_CODEC_DEBUG
enum ad82088_bank_num {
	AD82088_BANK_1   = 1,
	AD82088_BANK_2   = 2,
	AD82088_BANK_MAX = 3,
};

enum ad82088_reg_type {
	AD82088_REG_TYPE_I2C = 0,
	AD82088_REG_TYPE_RAM = 1,
	AD82088_REG_TYPE_MAX = 2,
};

struct ad82088_reg_map {
	int  address;
	int  numofbytes;
	const char reg_str[12];
	char regname[50];
};
#endif

#endif
