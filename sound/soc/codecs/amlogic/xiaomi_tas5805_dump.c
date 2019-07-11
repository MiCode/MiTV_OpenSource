#include <sound/soc.h>

struct tas5805_reg_map {
	int  address;
	int  numofbytes;
	bool valid;
	char regname[50];
};

static struct tas5805_reg_map reg_maps[] = {
	{0x01,  1, true,  "RESET_CTRL"},
	{0x02,  1, true,  "DEVICE_CTRL_1"},
	{0x03,  1, true,  "DEVICE_CTRL_2"},
	{0x0f,  1, true,  "I2C_PAGE_AUTO_INC"},
	{0x28,  1, true,  "SIG_CH_CTRL"},
	{0x29,  1, true,  "CLOCK_DET_CTRL"},
	{0x30,  1, true,  "SDOUT_SEL"},
	{0x31,  1, true,  "I2S_CTRL"},
	{0x33,  1, true,  "SAP_CTRL1"},
	{0x34,  1, true,  "SAP_CTRL2"},
	{0x35,  1, true,  "SAP_CTRL3"},
	{0x37,  1, true,  "FS_MON"},
	{0x38,  1, true,  "BCK_MON"},
	{0x39,  1, true,  "CLKDET_STATUS"},
	{0x4c,  1, true,  "DIG_VOL_CTRL"},
	{0x4e,  1, true,  "DIG_VOL_CTRL2"},
	{0x4f,  1, true,  "DIG_VOL_CTRL3"},
	{0x50,  1, true,  "AUTO_MUTE_CTRL"},
	{0x51,  1, true,  "AUTO_MUTE_TIME"},
	{0x53,  1, true,  "ANA_CTRL"},
	{0x54,  1, true,  "AGAIN"},
	{0x5c,  1, true,  "BQ_WR_CTRL1"},
	{0x5d,  1, true,  "DAC_CTRL"},
	{0x60,  1, true,  "ADR_PIN_CTRL"},
	{0x61,  1, true,  "ADR_PIN_CONFIG"},
	{0x66,  1, true,  "DSP_MISC"},
	{0x67,  1, true,  "DIE_ID"},
	{0x68,  1, true,  "POWER_STATE"},
	{0x69,  1, true,  "AUTOMUTE_STATE"},
	{0x6a,  1, true,  "PHASE_CTRL"},
	{0x6b,  1, true,  "SS_CTRL0"},
	{0x6c,  1, true,  "SS_CTRL1"},
	{0x6d,  1, true,  "SS_CTRL2"},
	{0x6e,  1, true,  "SS_CTRL3"},
	{0x6f,  1, true,  "SS_CTRL4"},
	{0x70,  1, true,  "CHAN_FAULT"},
	{0x71,  1, true,  "GLOBAL_FAULT1"},
	{0x72,  1, true,  "GLOBAL_FAULT2"},
	{0x73,  1, true,  "OT WARNING"},
	{0x74,  1, true,  "PIN_CONTROL1"},
	{0x75,  1, true,  "PIN_CONTROL2"},
	{0x76,  1, true,  "MISC_CONTROL"},
	{0x78,  1, true,  "FAULT_CLEAR"},
};

static inline char *__get_reg_name(int addr)
{
	int reg_num = sizeof(reg_maps)/sizeof(struct tas5805_reg_map);
	int i;

	for (i = 0; i < reg_num; i++) {
		if (reg_maps[i].address == addr)
			return reg_maps[i].regname;
	}

	return "not-found";
}

static inline ssize_t _dump_book0_register(struct snd_soc_codec *codec,
	char *buffer, int addr)
{
	unsigned int data;
	ssize_t len = 0;

	/* Must make sure of book0, page0 */
	snd_soc_write(codec, 0x00, 0x00);
	snd_soc_write(codec, 0x7f, 0x00);

	data = snd_soc_read(codec, addr);
	/* reg addr */
	len = sprintf(buffer, "[0x%02x] ", addr);
	/* reg data */
	len += sprintf(buffer+len, "%02x ", data);
	/* wrap */
	len += sprintf(buffer+len, "(%s)\n", __get_reg_name(addr));

	return len;
}

static ssize_t spk_name_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = tas5805m->codec;
	ssize_t len = 0;

	if (codec == NULL)
		return 0;

	/* len = sprintf(buf, "%s\n", codec->name_prefix); */
	len = sprintf(buf, "xiaomi_tas5805m\n");

	return len;
}

static ssize_t register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = tas5805m->codec;
	ssize_t len = 0;
	int reg_num = sizeof(reg_maps)/sizeof(struct tas5805_reg_map);
	int i;

	if (codec == NULL)
		return 0;

	len = sprintf(buf, "Xiaomi - Dump registers of %s-%s:\n",
		codec->component.name_prefix,
		codec->component.name);

	mutex_lock(&tas5805m->mutex);

	/* control port registers */
	for (i = 0; i < reg_num; i++)
		len += _dump_book0_register(codec,
			buf+len, reg_maps[i].address);

	mutex_unlock(&tas5805m->mutex);

	return len;
}

static ssize_t err_status_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tas5805m_priv *tas5805m = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = tas5805m->codec;
	ssize_t len = 0;

	if (codec == NULL)
		return 0;

	len = sprintf(buf, "Xiaomi - Dump err status registers of %s-%s:\n\n",
		codec->component.name_prefix,
		codec->component.name);

	mutex_lock(&tas5805m->mutex);

	len += sprintf(buf+len, "Before write clean:\n");
	len += _dump_book0_register(codec, buf+len, 0x68);
	len += _dump_book0_register(codec, buf+len, 0x70);
	len += _dump_book0_register(codec, buf+len, 0x71);
	len += _dump_book0_register(codec, buf+len, 0x72);
	len += _dump_book0_register(codec, buf+len, 0x73);
	len += _dump_book0_register(codec, buf+len, 0x74);
	len += _dump_book0_register(codec, buf+len, 0x75);
	len += _dump_book0_register(codec, buf+len, 0x76);

	/* Must make sure of book0, page0 */
	snd_soc_write(codec, 0x00, 0x00);
	snd_soc_write(codec, 0x7f, 0x00);
	snd_soc_write(codec, 0x78, 0x80);

	len += sprintf(buf+len, "After write clean:\n");
	len += _dump_book0_register(codec, buf+len, 0x68);
	len += _dump_book0_register(codec, buf+len, 0x70);
	len += _dump_book0_register(codec, buf+len, 0x71);
	len += _dump_book0_register(codec, buf+len, 0x72);
	len += _dump_book0_register(codec, buf+len, 0x73);
	len += _dump_book0_register(codec, buf+len, 0x74);
	len += _dump_book0_register(codec, buf+len, 0x75);
	len += _dump_book0_register(codec, buf+len, 0x76);

	mutex_unlock(&tas5805m->mutex);

	return len;
}

static DEVICE_ATTR(speaker_name, 0644, spk_name_show, NULL);
static DEVICE_ATTR(dump,  0644, register_show, NULL);
static DEVICE_ATTR(error, 0644, err_status_show, NULL);

static struct attribute *tas5805_attributes[] = {
	&dev_attr_speaker_name.attr,
	&dev_attr_dump.attr,
	&dev_attr_error.attr,
	NULL
};

static struct attribute_group tas5805m_group = {
	.attrs = tas5805_attributes
};
