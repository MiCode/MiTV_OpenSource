#include <sound/soc.h>
#include "xiaomi_debug_utils.h"

struct tas5707_reg_map {
	int  address;
	int  numofbytes;
	bool valid;
	char regname[50];
};

static unsigned char tas5707_eq_map[280];

static unsigned char tas5707_drc1_map[24];

static unsigned char tas5707_drc1_tko_map[12];

static struct tas5707_reg_map reg_maps[] = {
	{0x00,  1, true,  "Clock control"},
	{0x01,  1, true,  "Device ID"},
	{0x02,  1, true,  "Error status"},
	{0x03,  1, true,  "System control reg1"},
	{0x04,  1, true,  "Serial data interface"},
	{0x05,  1, true,  "System control reg2"},
	{0x06,  1, true,  "Soft mute"},
	{0x07,  1, true,  "Master volume"},
	{0x08,  1, true,  "Channel 1 vol"},
	{0x09,  1, true,  "Channel 2 vol"},
	{0x0A,  1, true,  "Channel 3 vol"},
	{0x0B,  1, false, "reserved"},
	{0x0C,  1, false, "reserved"},
	{0x0D,  1, false, "reserved"},
	{0x0E,  1, true,  "Volume configuration"},
	{0x0F,  1, false, "reserved"},
	{0x10,  1, true,  "Modulation limit"},
	{0x11,  1, true,  "IC delay channel 1"},
	{0x12,  1, true,  "IC delay channel 2"},
	{0x13,  1, true,  "IC delay channel 3"},
	{0x14,  1, true,  "IC delay channel 4"},
	{0x15,  1, false, "reserved"},
	{0x16,  1, false, "reserved"},
	{0x17,  1, false, "reserved"},
	{0x18,  1, false, "reserved"},
	{0x19,  1, false, "reserved"},
	{0x1A,  1, true,  "Start/Stop period"},
	{0x1B,  1, true,  "Oscillator trim"},
	{0x1C,  1, true,  "BKND_ERR"},
	{0x1D,  1, false, "reserved"},
	{0x1E,  1, false, "reserved"},
	{0x1F,  1, false, "reserved"},
	{0x20,  4, true,  "Input MUX"},
	{0x21,  4, false, "reserved"},
	{0x22,  4, false, "reserved"},
	{0x23,  4, false, "reserved"},
	{0x24,  4, false, "reserved"},
	{0x25,  4, true,  "PWM MUX"},
	{0x26,  4, false, "reserved"},
	{0x27,  4, false, "reserved"},
	{0x28,  4, false, "reserved"},
	/* EQ Ch1*/
	{0x29, 20, true,  "ch1_bq[0]"},
	{0x2A, 20, true,  "ch1_bq[1]"},
	{0x2B, 20, true,  "ch1_bq[2]"},
	{0x2C, 20, true,  "ch1_bq[3]"},
	{0x2D, 20, true,  "ch1_bq[4]"},
	{0x2E, 20, true,  "ch1_bq[5]"},
	{0x2F, 20, true,  "ch1_bq[6]"},
	/* EQ Ch2*/
	{0x30, 20, true,  "ch2_bq[0]"},
	{0x31, 20, true,  "ch2_bq[1]"},
	{0x32, 20, true,  "ch2_bq[2]"},
	{0x33, 20, true,  "ch2_bq[3]"},
	{0x34, 20, true,  "ch2_bq[4]"},
	{0x35, 20, true,  "ch2_bq[5]"},
	{0x36, 20, true,  "ch2_bq[6]"},
	{0x37, 20, false, "reserved"},
	{0x38, 20, false, "reserved"},
	{0x39, 20, false, "reserved"},
	/* DRC */
	{0x3A,  8, true,  "DRC ae"},
	{0x3B,  8, true,  "DRC aa"},
	{0x3C,  8, true,  "DRC ad"},
	{0x3D,  8, false, "reserved"},
	{0x3E,  8, false, "reserved"},
	{0x3F,  8, false, "reserved"},
	{0x40,  4, true,  "DRC-T"},
	{0x41,  4, false, "DRC-K"},
	{0x42,  4, false, "DRC-O"},
	{0x43,  4, false, "reserved"},
	{0x44,  4, false, "reserved"},
	{0x45,  4, false, "reserved"},
	/* DRC - ctrl */
	{0x46,  4, true,  "DRC control"},
	/* reserved */
	{0x47,  4, false, "reserved"},
	{0x48,  4, false, "reserved"},
	{0x49,  4, false, "reserved"},
	{0x4A,  4, false, "reserved"},
	{0x4B,  4, false, "reserved"},
	{0x4C,  4, false, "reserved"},
	{0x4D,  4, false, "reserved"},
	{0x4E,  4, false, "reserved"},
	{0x4F,  4, false, "reserved"},
	/* EQ - ctrl */
	{0x50,  4, true,  "Ban switch control &EQ Ctrl"},
	{0xF9,  4, true,  "Device address update"},
	/* reserved - [0xFA, 0xFF] */
};

static inline bool __check_if_valid(int addr)
{
	int reg_num = sizeof(reg_maps)/sizeof(struct tas5707_reg_map);
	int i;

	for (i = 0; i < reg_num; i++) {
		if (reg_maps[i].address == addr)
			return reg_maps[i].valid;
	}

	return false;
}

static inline int __get_num_of_bytes(int addr)
{
	int reg_num = sizeof(reg_maps)/sizeof(struct tas5707_reg_map);
	int i;
	int numofbytes = 0;

	for (i = 0; i < reg_num; i++) {
		if (reg_maps[i].address == addr) {
			numofbytes = reg_maps[i].numofbytes;
			break;
		}
	}

	return numofbytes;
}

static inline char *__get_reg_name(int addr)
{
	int reg_num = sizeof(reg_maps)/sizeof(struct tas5707_reg_map);
	int i;

	for (i = 0; i < reg_num; i++) {
		if (reg_maps[i].address == addr)
			return reg_maps[i].regname;
	}

	return "not-found";
}

static inline int  ___pwm_map(unsigned char data,
		unsigned char pin, unsigned char *map)
{
	int len = 0;

	switch (data) {
	case 0:
		len = sprintf(map, "CH1_T->");
		break;
	case 1:
		len = sprintf(map, "CH2_T->");
		break;
	case 2:
		len = sprintf(map, "CH1_N->");
		break;
	case 3:
		len = sprintf(map, "CH2_N->");
		break;
	default:
		break;
	}

	len += sprintf(map+len, "OUT_%c|", pin);

	return len;
}

static inline char *__parse_pwm_mux_reg(unsigned char *data)
{
	static unsigned char ret[50];
	int len = 0;
	unsigned char out_a = (data[1]>>4)&0x0F;
	unsigned char out_b =  data[1]&0x0F;
	unsigned char out_c = (data[2]>>4)&0x0F;
	unsigned char out_d =  data[2]&0x0F;

	len = ___pwm_map(out_a, 'A', ret);
	len += ___pwm_map(out_b, 'B', ret+len);
	len += ___pwm_map(out_c, 'C', ret+len);
	len += ___pwm_map(out_d, 'D', ret+len);

	ret[len] = '\0';
	return ret;
}

static inline char *__parse_input_mux_reg(unsigned char data)
{
	static unsigned char ret[30];
	int len = 0;
	unsigned char ch1_data = (data>>4)&0x0F;
	unsigned char ch2_data = data&0x0F;

	switch (ch1_data&0x07) {
	case 0:
		len = sprintf(ret, "SDIN L to Ch1, %s Mode",
			(ch1_data&0x08)?"BD":"AD");
		break;
	case 1:
		len = sprintf(ret, "SDIN R to Ch1, %s Mode",
			(ch1_data&0x08)?"BD":"AD");
		break;
	case 6:
		len = sprintf(ret, "Ground(0) to Ch1, %s Mode",
			(ch1_data&0x08)?"BD":"AD");
		break;
	default:
		break;
	}

	switch (ch2_data&0x07) {
	case 0:
		len += sprintf(ret+len, "|SDIN L to Ch2, %s Mode",
			(ch2_data&0x08)?"BD":"AD");
		break;
	case 1:
		len += sprintf(ret+len, "|SDIN R to Ch2, %s Mode",
			(ch2_data&0x08)?"BD":"AD");
		break;
	case 6:
		len += sprintf(ret+len, "|Ground(0) to Ch2, %s Mode",
			(ch2_data&0x08)?"BD":"AD");
		break;
	default:
		break;
	}

	ret[len] = '\0';
	return ret;
}

static inline char *__parse_serial_data_reg(unsigned char data)
{
	static unsigned char ret[30];
	int len = 0;

	switch (data) {
	case 0:
		len = sprintf(ret, "16b,Right-Justified");
		break;
	case 1:
		len = sprintf(ret, "20b,Right-Justified");
		break;
	case 2:
		len = sprintf(ret, "24b,Right-Justified");
		break;
	case 3:
		len = sprintf(ret, "16b,I2S");
		break;
	case 4:
		len = sprintf(ret, "20b,I2S");
		break;
	case 5:
		len = sprintf(ret, "24b,I2S");
		break;
	case 6:
		len = sprintf(ret, "16b,Left-Justified");
		break;
	case 7:
		len = sprintf(ret, "20b,Left-Justified");
		break;
	case 8:
		len = sprintf(ret, "24b,Left-Justified");
		break;
	default:
		break;
	}

	ret[len] = '\0';
	return ret;
}

static inline char *__parse_clock_reg(unsigned char data)
{
	unsigned char fs   = (data>>5)&0x07;
	unsigned char mclk = (data>>2)&0x07;
	static unsigned char ret[30];
	int len = 0;

	switch (fs) {
	case 0:
		len = sprintf(ret, "fs=32k");
		break;
	case 1:
	case 2:
		len = sprintf(ret, "fs=reserved");
		break;
	case 3:
		len = sprintf(ret, "fs=44.1/48k");
		break;
	case 4:
		len = sprintf(ret, "fs=16k");
		break;
	case 5:
		len = sprintf(ret, "fs=22.05/24k");
		break;
	case 6:
		len = sprintf(ret, "fs=8k");
		break;
	case 7:
		len = sprintf(ret, "fs=11.025/12k");
		break;
	default:
		break;
	}

	switch (mclk) {
	case 0:
		len += sprintf(ret+len, "|mclk=64fs");
		break;
	case 1:
		len += sprintf(ret+len, "|mclk=128fs");
		break;
	case 2:
		len += sprintf(ret+len, "|mclk=192fs");
		break;
	case 3:
		len += sprintf(ret+len, "|mclk=256fs");
		break;
	case 4:
		len += sprintf(ret+len, "|mclk=384fs");
		break;
	case 5:
		len += sprintf(ret+len, "|mclk=512fs");
		break;
	case 6:
	case 7:
		len += sprintf(ret+len, "|mclk=reserved");
		break;
	default:
		break;
	}

	ret[len] = '\0';

	return ret;
}

static inline ssize_t _dump_one_register(struct snd_soc_codec *codec,
	char *buffer, int addr)
{
	struct tas5707_priv *tas5707 = snd_soc_codec_get_drvdata(codec);
	unsigned char data[20];
	int length = __get_num_of_bytes(addr);
	ssize_t len = 0;
	int i = 0;

	if (length) {
		/* WARN: need skip the cache to read */
		regcache_cache_bypass(tas5707->regmap, true);
		regmap_raw_read(tas5707->regmap, addr, data, length);
		regcache_cache_bypass(tas5707->regmap, false);
		/* reg addr */
		len = sprintf(buffer, "[0x%02x] ", addr);
		/* reg data */
		for (i = 0; i < length; i++)
			len += sprintf(buffer+len, "%02X ", data[i]);
		switch (addr) {
		case DDX_PWM_MUX:
			len += sprintf(buffer+len, "(%s)",
				__parse_pwm_mux_reg(data));
			break;
		case DDX_INPUT_MUX:
			len += sprintf(buffer+len, "(%s)",
				__parse_input_mux_reg(data[1]));
			break;
		case DDX_CLOCK_CTL:
			len += sprintf(buffer+len, "(%s)",
				__parse_clock_reg(data[0]));
			break;
		case DDX_SERIAL_DATA_INTERFACE:
			len += sprintf(buffer+len, "(%s)",
				__parse_serial_data_reg(data[0]));
			break;
		case DDX_MASTER_VOLUME:
		case DDX_CHANNEL1_VOL:
		case DDX_CHANNEL2_VOL:
		case DDX_CHANNEL3_VOL:
			len += sprintf(buffer+len, "(%d dB)", 24-(data[0]/2));
			break;
		default:
			break;
		}
		/* wrap */
		len += sprintf(buffer+len, "(%s)\n", __get_reg_name(addr));
		return len;
	}

	return len;
}

static int __parse_eq_param(const char *buf, size_t count)
{
	char *pTmp;

	/* 0X29---ch1 */
	pTmp = get_reg_pos(buf, "X29", count);
	if (pTmp == NULL) {
		pr_err("No X29 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[0], 20);

	/* 0X2A---ch1 */
	pTmp = get_reg_pos(buf, "X2A", count);
	if (pTmp == NULL) {
		pr_err("No X2A settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[20], 20);

	/* 0X2B---ch1 */
	pTmp = get_reg_pos(buf, "X2B", count);
	if (pTmp == NULL) {
		pr_err("No X2B settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[40], 20);

	/* 0X2C---ch1 */
	pTmp = get_reg_pos(buf, "X2C", count);
	if (pTmp == NULL) {
		pr_err("No X2C settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[60], 20);

	/* 0X2D---ch1 */
	pTmp = get_reg_pos(buf, "X2D", count);
	if (pTmp == NULL) {
		pr_err("No X2D settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[80], 20);

	/* 0X2E---ch1 */
	pTmp = get_reg_pos(buf, "X2E", count);
	if (pTmp == NULL) {
		pr_err("No X2E settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[100], 20);

	/* 0X2F---ch1 */
	pTmp = get_reg_pos(buf, "X2F", count);
	if (pTmp == NULL) {
		pr_err("No X29 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[120], 20);

	/* 0X30---ch2 */
	pTmp = get_reg_pos(buf, "X30", count);
	if (pTmp == NULL) {
		pr_err("No X30 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[140], 20);

	/* 0X31---ch2 */
	pTmp = get_reg_pos(buf, "X31", count);
	if (pTmp == NULL) {
		pr_err("No X31 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[160], 20);

	/* 0X32---ch2 */
	pTmp = get_reg_pos(buf, "X32", count);
	if (pTmp == NULL) {
		pr_err("No X32 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[180], 20);

	/* 0X33---ch2 */
	pTmp = get_reg_pos(buf, "X33", count);
	if (pTmp == NULL) {
		pr_err("No X33 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[200], 20);

	/* 0X34---ch2 */
	pTmp = get_reg_pos(buf, "X34", count);
	if (pTmp == NULL) {
		pr_err("No X34 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[220], 20);

	/* 0X35---ch2 */
	pTmp = get_reg_pos(buf, "X35", count);
	if (pTmp == NULL) {
		pr_err("No X35 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[240], 20);

	/* 0X36---ch2 */
	pTmp = get_reg_pos(buf, "X36", count);
	if (pTmp == NULL) {
		pr_err("No X36 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_eq_map[260], 20);

	return 0;
}

static int __parse_drc1_param(const char *buf, size_t count)
{
	char *pTmp;

	/* 0X3A---drc1 */
	pTmp = get_reg_pos(buf, "X3A", count);
	if (pTmp == NULL) {
		pr_err("No X3A settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_map[0], 8);

	/* 0X3B---drc1 */
	pTmp = get_reg_pos(buf, "X3B", count);
	if (pTmp == NULL) {
		pr_err("No X3B settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_map[8], 8);

	/* 0X3C---drc1 */
	pTmp = get_reg_pos(buf, "X3C", count);
	if (pTmp == NULL) {
		pr_err("No X3C settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_map[16], 8);

	return 0;
}

static int __parse_drc1_tko_param(const char *buf, size_t count)
{
	char *pTmp;

	/* 0X40---drc1_TKO */
	pTmp = get_reg_pos(buf, "X40", count);
	if (pTmp == NULL) {
		pr_err("No X40 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_tko_map[0], 4);

	/* 0X41---drc1_TKO */
	pTmp = get_reg_pos(buf, "X41", count);
	if (pTmp == NULL) {
		pr_err("No X41 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_tko_map[4], 4);

	/* 0X42---drc1_TKO */
	pTmp = get_reg_pos(buf, "X42", count);
	if (pTmp == NULL) {
		pr_err("No X42 settings\n");
		return -1;
	}
	pTmp += 3;
	parse_value_by_length(pTmp, &tas5707_drc1_tko_map[8], 4);

	return 0;
}

static int __update_drc(struct snd_soc_codec *codec)
{
	u8 tas5707_drc_ctl_table[] = { 0x00, 0x00, 0x00, 0x00 };
	u8 tas5707_drc_table[3][8];
	u8 tas5707_drc_tko_table[3][4];
	int i = 0, j = 0;
	u8 *pTmp = NULL;
	struct tas5707_priv *tas5707 = snd_soc_codec_get_drvdata(codec);

	/* disable drc first */
	regmap_raw_write(tas5707->regmap, DDX_DRC_CTL,
			 tas5707_drc_ctl_table, 4);

	/* drc1 */
	pTmp = &tas5707_drc1_map[0];
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 8; j++)
			tas5707_drc_table[i][j] = pTmp[i * 8 + j];

		regmap_raw_write(tas5707->regmap, DDX_DRC1_AE + i,
				 tas5707_drc_table[i], 8);
	}

	/* drc1 tko */
	pTmp = &tas5707_drc1_tko_map[0];
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 4; j++)
			tas5707_drc_tko_table[i][j] = pTmp[i * 4 + j];

		regmap_raw_write(tas5707->regmap, DDX_DRC1_T + i,
				 tas5707_drc_tko_table[i], 4);
	}

	/* enable drc */
	tas5707_drc_ctl_table[3] = 0x01;
	regmap_raw_write(tas5707->regmap, DDX_DRC_CTL,
				 tas5707_drc_ctl_table, 4);

	return 0;
}

static int __update_eq(struct snd_soc_codec *codec)
{
	u8 tas5707_eq_ctl_table[] = { 0x00, 0x00, 0x00, 0x80 };
	int i = 0, j = 0, k = 0;
	u8 *pTmp = NULL;
	u8 addr;
	u8 tas5707_bq_table[20];
	struct tas5707_priv *tas5707 = snd_soc_codec_get_drvdata(codec);

	/* disable eq */
	regmap_raw_read(tas5707->regmap, DDX_BANKSWITCH_AND_EQCTL,
		tas5707_eq_ctl_table, 4);
	tas5707_eq_ctl_table[3] |= 0x80;
	regmap_raw_write(tas5707->regmap, DDX_BANKSWITCH_AND_EQCTL,
			 tas5707_eq_ctl_table, 4);

	/*
	 * EQ1: 0x29~0x2F
	 * EQ2: 0x30~0x36
	 */
	pTmp = &tas5707_eq_map[0];
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 7; j++) {
			addr = (DDX_CH1_BQ_0 + i * 7 + j);
			for (k = 0; k < 20; k++)
				tas5707_bq_table[k] =
					pTmp[i * 7 * 20 + j * 20 + k];
			regmap_raw_write(tas5707->regmap, addr,
					tas5707_bq_table, 20);
		}
	}

	/* enable eq */
	tas5707_eq_ctl_table[3] &= 0x7F;
	regmap_raw_write(tas5707->regmap, DDX_BANKSWITCH_AND_EQCTL,
			 tas5707_eq_ctl_table, 4);

	return 0;
}
static ssize_t spk_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct tas5707_priv *tas5707 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = tas5707->codec;

	if (codec == NULL)
		return 0;

#if 0
	len = sprintf(buf, "%s\n", codec->name_prefix);
#else
	len = sprintf(buf, "AMP\n");
#endif

	return len;
}

static ssize_t eq_drc_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct tas5707_priv *tas5707 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = tas5707->codec;

	if (codec == NULL)
		return 0;

#if 0
	len = sprintf(buf, "Xiaomi - Dump registers of %s-%s:\n",
		codec->name_prefix,
		codec->name);
#else
	len = sprintf(buf, "Xiaomi - Dump registers\n");
#endif

	/* system */
	len += sprintf(buf+len, "\nSYSTEM:\n");
	len += _dump_one_register(codec, buf+len, DDX_CLOCK_CTL);
	len += _dump_one_register(codec, buf+len, DDX_DEVICE_ID);
	len += _dump_one_register(codec, buf+len, DDX_SYS_CTL_1);
	len += _dump_one_register(codec, buf+len, DDX_SYS_CTL_2);
	len += _dump_one_register(codec, buf+len, DDX_SERIAL_DATA_INTERFACE);
	len += _dump_one_register(codec, buf+len, DDX_MODULATION_LIMIT);
	/* EQ/DRC control */
	len += sprintf(buf+len, "\nEQ/DRC En:\n");
	len += _dump_one_register(codec, buf+len, DDX_DRC_CTL);
	len += _dump_one_register(codec, buf+len, DDX_BANKSWITCH_AND_EQCTL);
	/* volume */
	len += sprintf(buf+len, "\nVOLUME:\n");
	len += _dump_one_register(codec, buf+len, DDX_MASTER_VOLUME);
	len += _dump_one_register(codec, buf+len, DDX_CHANNEL1_VOL);
	len += _dump_one_register(codec, buf+len, DDX_CHANNEL2_VOL);
	len += _dump_one_register(codec, buf+len, DDX_CHANNEL3_VOL);
	len += _dump_one_register(codec, buf+len, DDX_VOLUME_CONFIG);
	len += _dump_one_register(codec, buf+len, DDX_SOFT_MUTE);
	/* mixer */
	len += sprintf(buf+len, "\nMUX/MIXER:\n");
	len += _dump_one_register(codec, buf+len, DDX_INPUT_MUX);
	len += _dump_one_register(codec, buf+len, DDX_PWM_MUX);
	/* EQ - channel 1 */
	len += sprintf(buf+len, "\nEQ/DRC:\n");
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_0);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_1);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_2);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_3);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_4);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_5);
	len += _dump_one_register(codec, buf+len, DDX_CH1_BQ_6);
	/* EQ - channel 2 */
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_0);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_1);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_2);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_3);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_4);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_5);
	len += _dump_one_register(codec, buf+len, DDX_CH2_BQ_6);
	/* DRC - channel 1/2 */
	len += _dump_one_register(codec, buf+len, DDX_DRC1_AE);
	len += _dump_one_register(codec, buf+len, DDX_DRC1_AA);
	len += _dump_one_register(codec, buf+len, DDX_DRC1_AD);
	len += _dump_one_register(codec, buf+len, DDX_DRC1_T);
	len += _dump_one_register(codec, buf+len, DDX_DRC1_K);
	len += _dump_one_register(codec, buf+len, DDX_DRC1_O);

	return len;
}

static ssize_t err_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct tas5707_priv *tas5707 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = tas5707->codec;

	if (codec == NULL)
		return 0;
#if 0
	len = sprintf(buf, "Xiaomi - Dump err status registers of %s-%s:\n\n",
		codec->name_prefix,
		codec->name);
#endif

	len += sprintf(buf+len, "Before write clean: ");
	len += _dump_one_register(codec, buf+len, DDX_ERROR_STATUS);
	snd_soc_write(codec, DDX_ERROR_STATUS, 0x00);
	len += sprintf(buf+len, "After  write clean: ");
	len += _dump_one_register(codec, buf+len, DDX_ERROR_STATUS);

	return len;
}

static ssize_t eq_param_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct tas5707_priv *tas5707 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = tas5707->codec;
	int update_eq = 1;
	int update_drc = 1;

	if (!codec) {
		pr_err("[%s:%d] codec is NULL\n", __func__, __LINE__);
		return 0;
	}

	if (__parse_eq_param(buf, count) < 0) {
		pr_err("__parse_eq_param failed\n");
		update_eq = 0;
	}
	if (__parse_drc1_param(buf, count) < 0) {
		pr_err("__parse_drc1_param failed\n");
		update_drc = 0;
	}
	if (__parse_drc1_tko_param(buf, count) < 0) {
		pr_err("__parse_drc1_tko_param failed\n");
		update_drc = 0;
	}

	/* mute first */
	snd_soc_write(codec, DDX_SOFT_MUTE, 0x03);

	/* update drc */
	if (update_drc) {
		pr_info("start update drc\n");
		__update_drc(codec);
	}
	if (update_eq) {
		pr_info("start update eq\n");
		__update_eq(codec);
	}

	/* unmute */
	snd_soc_write(codec, DDX_SOFT_MUTE, 0x00);

	return count;
}


static DEVICE_ATTR(speaker_name, 0644, spk_name_show, NULL);
static DEVICE_ATTR(dump,  0644, eq_drc_show, NULL);
static DEVICE_ATTR(error, 0644, err_status_show, NULL);
static DEVICE_ATTR(eq_param, 0644, NULL, eq_param_store);

static struct attribute *tas5707_attributes[] = {
	&dev_attr_speaker_name.attr,
	&dev_attr_dump.attr,
	&dev_attr_error.attr,
	&dev_attr_eq_param.attr,
	NULL
};

static struct attribute_group tas5707_group = {
	.attrs = tas5707_attributes
};
