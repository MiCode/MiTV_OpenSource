#include <sound/soc.h>
#include "ad82088.h"

static struct ad82088_reg_map i2c_reg_maps[] = {
	{0x00,  1, "Reg[00h]", "State_Control_1"},
	{0x01,  1, "Reg[01h]", "State_Control_2"},
	{0x02,  1, "Reg[02h]", "State_Control_3"},
	{0x03,  1, "Reg[03h]", "Master_volume"},
	{0x04,  1, "Reg[04h]", "Channel_1_volume"},
	{0x05,  1, "Reg[05h]", "Channel_2_volume"},
	{0x06,  1, "Reg[06h]", "Channel_3_volume"},
	{0x07,  1, "Reg[07h]", "Channel_4_volume"},
	{0x08,  1, "Reg[08h]", "Channel_5_volume"},
	{0x09,  1, "Reg[09h]", "Channel_6_volume"},
	{0x0A,  1, "Reg[0Ah]", "Bass_tone_boost_cut"},
	{0x0B, 1, "Reg[0Bh]", "Treble_tone_boost_cut"},
	{0x0C, 1, "Reg[0Ch]", "State_Control_4"},
	{0x0d, 1, "Reg[0dh]", "Channel_1_configuration_registers"},
	{0x0e, 1, "Reg[0eh]", "Channel_2_configuration_registers"},
	{0x0f, 1, "Reg[0fh]", "Channel_3_configuration_registers"},
	{0x10, 1, "Reg[10h]", "Channel_4_configuration_registers"},
	{0x11, 1, "Reg[11h]", "Channel_5_configuration_registers"},
	{0x12, 1, "Reg[12h]", "Channel_6_configuration_registers"},
	{0x13, 1, "Reg[13h]", "Channel_7_configuration_registers"},
	{0x14, 1, "Reg[14h]", "Channel_8_configuration_registers"},
	{0x15, 1, "Reg[15h]", "DRC1_limiter_attack/release_rate"},
	{0x16, 1, "Reg[16h]", "DRC2_limiter_attack/release_rate"},
	{0x17, 1, "Reg[17h]", "DRC3_limiter_attack/release_rate"},
	{0x18, 1, "Reg[18h]", "DRC4_limiter_attack/release_rate"},
	{0x19, 1, "Reg[19h]", "Error_Delay"},
	{0x1a, 1, "Reg[1ah]", "State_Control_5"},
	{0x1b, 1, "Reg[1bh]", "State_Control_6"},
	{0x1c, 1, "Reg[1ch]", "State_Control_7"},
	{0x1d, 1, "Reg[1dh]", "Coefficient_RAM_Base_Address"},
	{0x1e, 1, "Reg[1eh]", "Top_8-bits_of_coefficients_A1"},
	{0x1f, 1, "Reg[1fh]", "Middle_8-bits_of_coefficients_A1"},
	{0x20, 1, "Reg[20h]", "Bottom_8-bits_of_coefficients_A1"},
	{0x21, 1, "Reg[21h]", "Top_8-bits_of_coefficients_A2"},
	{0x22, 1, "Reg[22h]", "Middle_8-bits_of_coefficients_A2"},
	{0x23, 1, "Reg[23h]", "Bottom_8-bits_of_coefficients_A2"},
	{0x24, 1, "Reg[24h]", "Top_8-bits_of_coefficients_B1"},
	{0x25, 1, "Reg[25h]", "Middle_8-bits_of_coefficients_B1"},
	{0x26, 1, "Reg[26h]", "Bottom_8-bits_of_coefficients_B1"},
	{0x27, 1, "Reg[27h]", "Top_8-bits_of_coefficients_B2"},
	{0x28, 1, "Reg[28h]", "Middle_8-bits_of_coefficients_B2"},
	{0x29, 1, "Reg[29h]", "Bottom_8-bits_of_coefficients_B2"},
	{0x2a, 1, "Reg[2ah]", "Top_8-bits_of_coefficients_A0"},
	{0x2b, 1, "Reg[2bh]", "Middle_8-bits_of_coefficients_A0"},
	{0x2c, 1, "Reg[2ch]", "Bottom_8-bits_of_coefficients_A0"},
	{0x2d, 1, "Reg[2dh]", "Coefficient_R/W_control"},
	{0x2e, 1, "Reg[2eh]", "Protection_Enable/Disable"},
	{0x2f, 1, "Reg[2fh]", "Memory_BIST_status"},
	{0x30, 1, "Reg[30h]", "Power_Stage_Status(Read_only)"},
	{0x31, 1, "Reg[31h]", "PWM_Output_Control"},
	{0x32, 1, "Reg[32h]", "Test_Mode_Control_Reg."},
	{0x33, 1, "Reg[33h]", "Qua-Ternary/Ternary_Switch_Level"},
	{0x34, 1, "Reg[34h]", "Volume_Fine_tune"},
	{0x35, 1, "Reg[35h]", "Volume_Fine_tune"},
	{0x36, 1, "Reg[36h]", "OC_bypass_&_GVDD_selection"},
	{0x37, 1, "Reg[37h]", "Device_ID_register"},
	{0x38, 1, "Reg[38h]", "RAM1_test_register_address"},
	{0x39, 1, "Reg[39h]", "Top_8-bits_of_RAM1_Data"},
	{0x3a, 1, "Reg[3ah]", "Middle_8-bits_of_RAM1_Data"},
	{0x3b, 1, "Reg[3bh]", "Bottom_8-bits_of_RAM1_Data"},
	{0x3c, 1, "Reg[3ch]", "RAM1_test_r/w_control"},
	{0x3d, 1, "Reg[3dh]", "RAM2_test_register_address"},
	{0x3e, 1, "Reg[3eh]", "Top_8-bits_of_RAM2_Data"},
	{0x3f, 1, "Reg[3fh]", "Middle_8-bits_of_RAM2_Data"},
	{0x40, 1, "Reg[40h]", "Bottom_8-bits_of_RAM2_Data"},
	{0x41, 1, "Reg[41h]", "RAM2_test_r/w_control"},
	{0x42, 1, "Reg[42h]", "Level_Meter_Clear"},
	{0x43, 1, "Reg[43h]", "Power_Meter_Clear"},
	{0x44, 1, "Reg[44h]", "TOP_of_C1_Level_Meter"},
	{0x45, 1, "Reg[45h]", "Middle_of_C1_Level_Meter"},
	{0x46, 1, "Reg[46h]", "Bottom_of_C1_Level_Meter"},
	{0x47, 1, "Reg[47h]", "TOP_of_C2_Level_Meter"},
	{0x48, 1, "Reg[48h]", "Middle_of_C2_Level_Meter"},
	{0x49, 1, "Reg[49h]", "Bottom_of_C2_Level_Meter"},
	{0x4a, 1, "Reg[4ah]", "TOP_of_C3_Level_Meter"},
	{0x4b, 1, "Reg[4bh]", "Middle_of_C3_Level_Meter"},
	{0x4c, 1, "Reg[4ch]", "Bottom_of_C3_Level_Meter"},
	{0x4d, 1, "Reg[4dh]", "TOP_of_C4_Level_Meter"},
	{0x4e, 1, "Reg[4eh]", "Middle_of_C4_Level_Meter"},
	{0x4f, 1, "Reg[4fh]", "Bottom_of_C4_Level_Meter"},
	{0x50, 1, "Reg[50h]", "TOP_of_C5_Level_Meter"},
	{0x51, 1, "Reg[51h]", "Middle_of_C5_Level_Meter"},
	{0x52, 1, "Reg[52h]", "Bottom_of_C5_Level_Meter"},
	{0x53, 1, "Reg[53h]", "TOP_of_C6_Level_Meter"},
	{0x54, 1, "Reg[54h]", "Middle_of_C6_Level_Meter"},
	{0x55, 1, "Reg[55h]", "Bottom_of_C6_Level_Meter"},
	{0x56, 1, "Reg[56h]", "TOP_of_C7_Level_Meter"},
	{0x57, 1, "Reg[57h]", "Middle_of_C7_Level_Meter"},
	{0x58, 1, "Reg[58h]", "Bottom_of_C7_Level_Meter"},
	{0x59, 1, "Reg[59h]", "TOP_of_C8_Level_Meter"},
	{0x5a, 1, "Reg[5ah]", "Middle_of_C8_Level_Meter"},
	{0x5b, 1, "Reg[5bh]", "Bottom_of_C8_Level_Meter"},
	{0x5c, 1, "Reg[5ch]", "I2S_Data_Output_Selection_Register"},
	{0x5d, 1, "Reg[5dh]", "Reserve"},
	{0x5e, 1, "Reg[5eh]", "Reserve"},
	{0x5f, 1, "Reg[5fh]", "Reserve"},
	{0x60, 1, "Reg[60h]", "Reserve"},
	{0x61, 1, "Reg[61h]", "Reserve"},
	{0x62, 1, "Reg[62h]", "Reserve"},
	{0x63, 1, "Reg[63h]", "Reserve"},
	{0x64, 1, "Reg[64h]", "Reserve"},
	{0x65, 1, "Reg[65h]", "Reserve"},
	{0x66, 1, "Reg[66h]", "Reserve"},
	{0x67, 1, "Reg[67h]", "Reserve"},
	{0x68, 1, "Reg[68h]", "Reserve"},
	{0x69, 1, "Reg[69h]", "Reserve"},
	{0x6a, 1, "Reg[6ah]", "Reserve"},
	{0x6b, 1, "Reg[6bh]", "Reserve"},
	{0x6c, 1, "Reg[6ch]", "Reserve"},
	{0x6d, 1, "Reg[6dh]", "Reserve"},
	{0x6e, 1, "Reg[6eh]", "Reserve"},
	{0x6f, 1, "Reg[6fh]", "Reserve"},
	{0x70, 1, "Reg[70h]", "Reserve"},
	{0x71, 1, "Reg[71h]", "Reserve"},
	{0x72, 1, "Reg[72h]", "Reserve"},
	{0x73, 1, "Reg[73h]", "Reserve"},
	{0x74, 1, "Reg[74h]", "Mono_Key_High_Byte"},
	{0x75, 1, "Reg[75h]", "Mono_Key_Low_Byte"},
	{0x76, 1, "Reg[76h]", "Boost_Control"},
	{0x77, 1, "Reg[77h]", "Hi-res_Item"},
	{0x78, 1, "Reg[78h]", "Test_Mode_register"},
	{0x79, 1, "Reg[79h]", "Boost_Strap_OV/UV_Selection"},
	{0x7a, 1, "Reg[7ah]", "OC_Selection_2"},
	{0x7b, 1, "Reg[7bh]", "MBIST_User_Program_Top_Byte_Even"},
	{0x7c, 1, "Reg[7ch]", "MBIST_User_Program_Middle_Byte_Even"},
	{0x7d, 1, "Reg[7dh]", "MBIST_User_Program_Bottom_Byte_Even"},
	{0x7e, 1, "Reg[7eh]", "MBIST_User_Program_Top_Byte_Odd"},
	{0x7f, 1, "Reg[7fh]", "MBIST_User_Program_Middle_Byte_Odd"},
	{0x80, 1, "Reg[80h]", "MBIST_User_Program_Bottom_Byte_Odd"},
	{0x81, 1, "Reg[81h]", "ERROR_clear_register"},
	{0x82, 1, "Reg[82h]", "Minimum_duty_test"},
	{0x83, 1, "Reg[83h]", "Reserve"},
	{0x84, 1, "Reg[84h]", "Reserve"},
	{0x85, 1, "Reg[85h]", "Reserve"},
};

/* TODO:
 * reg map is different with bank 1 and band2,
 * need add a maps for bank 2.
 **/
static struct ad82088_reg_map ram_reg_bank_one_maps[] = {
	{0x00,  3, "Coef[00h]", "EQ1_A1"},
	{0x01,  3, "Coef[01h]", "EQ1_A2"},
	{0x02,  3, "Coef[02h]", "EQ1_B1"},
	{0x03,  3, "Coef[03h]", "EQ1_B2"},
	{0x04,  3, "Coef[04h]", "EQ1_A0"},
	{0x05,  3, "Coef[05h]", "EQ2_A1"},
	{0x06,  3, "Coef[06h]", "EQ2_A2"},
	{0x07,  3, "Coef[07h]", "EQ2_B1"},
	{0x08,  3, "Coef[08h]", "EQ2_B2"},
	{0x09,  3, "Coef[09h]", "EQ2_A0"},
	{0x0a,  3, "Coef[0Ah]", "EQ3_A1"},
	{0x0b,  3, "Coef[0Bh]", "EQ3_A2"},
	{0x0c,  3, "Coef[0Ch]", "EQ3_B1"},
	{0x0d,  3, "Coef[0Dh]", "EQ3_B2"},
	{0x0e,  3, "Coef[0Eh]", "EQ3_A0"},
	{0x0f,  3, "Coef[0Fh]", "EQ4_A1"},
	{0x10,  3, "Coef[10h]", "EQ4_A2"},
	{0x11,  3, "Coef[11h]", "EQ4_B1"},
	{0x12,  3, "Coef[12h]", "EQ4_B2"},
	{0x13,  3, "Coef[13h]", "EQ4_A0"},
	{0x14,  3, "Coef[14h]", "EQ5_A1"},
	{0x15,  3, "Coef[15h]", "EQ5_A2"},
	{0x16,  3, "Coef[16h]", "EQ5_B1"},
	{0x17,  3, "Coef[17h]", "EQ5_B2"},
	{0x18,  3, "Coef[18h]", "EQ5_A0"},
	{0x19,  3, "Coef[19h]", "EQ6_A1"},
	{0x1a,  3, "Coef[1Ah]", "EQ6_A2"},
	{0x1b,  3, "Coef[1Bh]", "EQ6_B1"},
	{0x1c,  3, "Coef[1Ch]", "EQ6_B2"},
	{0x1d,  3, "Coef[1Dh]", "EQ6_A0"},
	{0x1e,  3, "Coef[1Eh]", "EQ7_A1"},
	{0x1f,  3, "Coef[1Fh]", "EQ7_A2"},
	{0x20,  3, "Coef[20h]", "EQ7_B1"},
	{0x21,  3, "Coef[21h]", "EQ7_B2"},
	{0x22,  3, "Coef[22h]", "EQ7_A0"},
	{0x23,  3, "Coef[23h]", "EQ8_A1"},
	{0x24,  3, "Coef[24h]", "EQ8_A2"},
	{0x25,  3, "Coef[25h]", "EQ8_B1"},
	{0x26,  3, "Coef[26h]", "EQ8_B2"},
	{0x27,  3, "Coef[27h]", "EQ8_A0"},
	{0x28,  3, "Coef[28h]", "EQ9_A1"},
	{0x29,  3, "Coef[29h]", "EQ9_A2"},
	{0x2A,  3, "Coef[2Ah]", "EQ9_B1"},
	{0x2B,  3, "Coef[2Bh]", "EQ9_B2"},
	{0x2C,  3, "Coef[2Ch]", "EQ9_A0"},
	{0x2D,  3, "Coef[2Dh]", "EQ10_A1"},
	{0x2E,  3, "Coef[2Eh]", "EQ10_A2"},
	{0x2F,  3, "Coef[2Fh]", "EQ10_B1"},
	{0x30,  3, "Coef[30h]", "EQ10_B2"},
	{0x31,  3, "Coef[31h]", "EQ10_A0"},
	{0x32,  3, "Coef[32h]", "EQ11_A1"},
	{0x33,  3, "Coef[33h]", "EQ11_A2"},
	{0x34,  3, "Coef[34h]", "EQ11_B1"},
	{0x35,  3, "Coef[35h]", "EQ11_B2"},
	{0x36,  3, "Coef[36h]", "EQ11_A0"},
	{0x37,  3, "Coef[37h]", "EQ12_A1"},
	{0x38,  3, "Coef[38h]", "EQ12_A2"},
	{0x39,  3, "Coef[39h]", "EQ12_B1"},
	{0x3A,  3, "Coef[3Ah]", "EQ12_B2"},
	{0x3B,  3, "Coef[3Bh]", "EQ12_A0"},
	{0x3C,  3, "Coef[3Ch]", "EQ13_A1"},
	{0x3D,  3, "Coef[3Dh]", "EQ13_A2"},
	{0x3E,  3, "Coef[3Eh]", "EQ13_B1"},
	{0x3F,  3, "Coef[3Fh]", "EQ13_B2"},
	{0x40,  3, "Coef[40h]", "EQ13_A0"},
	{0x41,  3, "Coef[41h]", "EQ14_A1"},
	{0x42,  3, "Coef[42h]", "EQ14_A2"},
	{0x43,  3, "Coef[43h]", "EQ14_B1"},
	{0x44,  3, "Coef[44h]", "EQ14_B2"},
	{0x45,  3, "Coef[45h]", "EQ14_A0"},
	{0x46,  3, "Coef[46h]", "EQ15_A1"},
	{0x47,  3, "Coef[47h]", "EQ15_A2"},
	{0x48,  3, "Coef[48h]", "EQ15_B1"},
	{0x49,  3, "Coef[49h]", "EQ15_B2"},
	{0x4A,  3, "Coef[4Ah]", "EQ15_A0"},
	{0x4B,  3, "Coef[4Bh]", "MIXER_1"},
	{0x4C,  3, "Coef[4Ch]", "MIXER_2"},
	{0x4D,  3, "Coef[4Dh]", "PRE_SCALE"},
	{0x4E,  3, "Coef[4Eh]", "POST_SCALE"},
	{0x4F,  3, "Coef[4Fh]", "SRS_HPF_A0"},
	{0x50,  3, "Coef[50h]", "SRS_HPF_A1"},
	{0x51,  3, "Coef[51h]", "SRS_HPF_B1"},
	{0x52,  3, "Coef[52h]", "SRS_LPF_A0"},
	{0x53,  3, "Coef[53h]", "SRS_LPF_A1"},
	{0x54,  3, "Coef[54h]", "SRS_LPF_B1"},
	{0x55,  3, "Coef[55h]", "Power_Clipping"},
	{0x56,  3, "Coef[56h]", "DRC1_ATH"},
	{0x57,  3, "Coef[57h]", "DRC1_RTH"},
	{0x58,  3, "Coef[58h]", "DRC2_ATH"},
	{0x59,  3, "Coef[59h]", "DRC2_RTH"},
	{0x5A,  3, "Coef[5Ah]", "DRC3_ATH"},
	{0x5B,  3, "Coef[5Bh]", "DRC3_RTH"},
	{0x5C,  3, "Coef[5Ch]", "DRC4_ATH"},
	{0x5D,  3, "Coef[5Dh]", "DRC4_RTH"},
	{0x5E,  3, "Coef[5Eh]", "NGAL"},
	{0x5F,  3, "Coef[5Fh]", "NGRL"},
	{0x60,  3, "Coef[60h]", "DRC1_EC"},
	{0x61,  3, "Coef[61h]", "DRC2_EC"},
	{0x62,  3, "Coef[62h]", "DRC3_EC"},
	{0x63,  3, "Coef[63h]", "DRC4_EC"},
	{0x64,  3, "Coef[64h]", "C1_RMS"},
	{0x65,  3, "Coef[65h]", "C3_RMS"},
	{0x66,  3, "Coef[66h]", "C5_RMS"},
	{0x67,  3, "Coef[67h]", "C7_RMS"},
	{0x68,  3, "Coef[68h]", "EQ1A1"},
	{0x69,  3, "Coef[69h]", "EQ1A2"},
	{0x6A,  3, "Coef[6Ah]", "EQ1B1"},
	{0x6B,  3, "Coef[6Bh]", "EQ1B2"},
	{0x6C,  3, "Coef[6Ch]", "EQ1A0"},
	{0x6D,  3, "Coef[6Dh]", "EQ2A1"},
	{0x6E,  3, "Coef[6Eh]", "EQ2A2"},
	{0x6F,  3, "Coef[6Fh]", "EQ2B1"},
	{0x70,  3, "Coef[70h]", "EQ2B2"},
	{0x71,  3, "Coef[71h]", "EQ2A0"},
	{0x72,  3, "Coef[72h]", "EQ3A1"},
	{0x73,  3, "Coef[73h]", "EQ3A2"},
	{0x74,  3, "Coef[74h]", "EQ3B1"},
	{0x75,  3, "Coef[75h]", "EQ3B2"},
	{0x76,  3, "Coef[76h]", "EQ3A0"},
	{0x77,  3, "Coef[77h]", "EQ4A1"},
	{0x78,  3, "Coef[78h]", "EQ4A2"},
	{0x79,  3, "Coef[79h]", "EQ4B1"},
	{0x7A,  3, "Coef[7Ah]", "EQ4B2"},
	{0x7B,  3, "Coef[7Bh]", "EQ4A0"},
};

static inline int dbg_get_data_length(int addr, enum ad82088_reg_type reg_type)
{
	int reg_num;
	int i;
	int numofbytes = 0;

	switch (reg_type) {
	case AD82088_REG_TYPE_I2C:
		reg_num = sizeof(i2c_reg_maps)/sizeof(struct ad82088_reg_map);
		for (i = 0; i < reg_num; i++) {
			if (i2c_reg_maps[i].address == addr) {
				numofbytes = i2c_reg_maps[i].numofbytes;
				break;
			}
		}
		break;
	case AD82088_REG_TYPE_RAM:
		reg_num =
		sizeof(ram_reg_bank_one_maps)/sizeof(struct ad82088_reg_map);
		for (i = 0; i < reg_num; i++) {
			if (ram_reg_bank_one_maps[i].address == addr) {
				numofbytes =
					ram_reg_bank_one_maps[i].numofbytes;
				break;
			}
		}
		break;
	default:
		pr_err("%s: wrong reg type %d\n", __func__, reg_type);
		return 0;
	}

	return numofbytes;
}

static inline char *dbg_get_reg_name(int addr, enum ad82088_reg_type reg_type)
{
	int reg_num;
	int i;

	switch (reg_type) {
	case AD82088_REG_TYPE_I2C:
		reg_num =
		sizeof(i2c_reg_maps)/sizeof(struct ad82088_reg_map);
		for (i = 0; i < reg_num; i++) {
			if (i2c_reg_maps[i].address == addr)
				return i2c_reg_maps[i].regname;
		}
		break;
	case AD82088_REG_TYPE_RAM:
		reg_num =
		sizeof(ram_reg_bank_one_maps)/sizeof(struct ad82088_reg_map);
		for (i = 0; i < reg_num; i++) {
			if (ram_reg_bank_one_maps[i].address == addr)
				return ram_reg_bank_one_maps[i].regname;
		}
		break;
	default:
		pr_err("%s: wrong reg type %d\n", __func__, reg_type);
		break;
	}
	return "not-found";
}

static inline ssize_t dump_ram_register(struct snd_soc_codec *codec,
	char *buffer, enum ad82088_bank_num bank, int addr)
{
	unsigned char data[15];
	ssize_t len = 0;

	/* only support single ram read now */
	data[0] = (unsigned char)addr;
	access_ram_register_single(codec, bank, addr, data);

	len += sprintf(buffer+len, "Coef[%02xh]=%02x%02x%02xh\n",
		(unsigned char)addr, data[0], data[1], data[2]);

	return len;
}

static inline ssize_t dump_i2c_register(struct snd_soc_codec *codec,
	char *buffer, int addr)
{
	unsigned char data[20];
	int length = dbg_get_data_length(addr, 0);
	ssize_t len = 0;

	/* on 82088, data length of i2c register are all 1byte */
	if (length) {
		data[0] = snd_soc_read(codec, addr);
		len = sprintf(buffer, "Reg[%02xh]=", addr);
		len += sprintf(buffer+len, "%02xh ", data[0]);
		len += sprintf(buffer+len, "##");
		/* TODO: parse some registers */
		len += sprintf(buffer+len, "  (%s)\n",
				dbg_get_reg_name(addr, 0));
		return len;
	}

	return len;
}

static ssize_t sys_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct ad82088_priv *ad82088 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = ad82088->codec;
	ssize_t len = 0;
	int i = 0;

	if (codec == NULL)
		return 0;

	len = sprintf(buf, "Xiaomi - Dump registers\n\n");

	/* TODO: add more register */
	/* dump i2c register */
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_1);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_2);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_3);
	len += dump_i2c_register(codec, buf+len, ESMT_MASTER_VOL);
	len += dump_i2c_register(codec, buf+len, ESMT_CH1_VOL);
	len += dump_i2c_register(codec, buf+len, ESMT_CH2_VOL);
	len += dump_i2c_register(codec, buf+len, ESMT_CH3_VOL);
	len += dump_i2c_register(codec, buf+len, ESMT_BASS_TONE_BO0ST);
	len += dump_i2c_register(codec, buf+len, ESMT_TREBLE_TONE_BOOST);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_4);
	len += dump_i2c_register(codec, buf+len, ESMT_CH1_CONF);
	len += dump_i2c_register(codec, buf+len, ESMT_CH2_CONF);
	len += dump_i2c_register(codec, buf+len, ESMT_CH3_CONF);
	len += dump_i2c_register(codec, buf+len, ESMT_DRC1_ATT_REL_RATE);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_5);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_6);
	len += dump_i2c_register(codec, buf+len, ESMT_STATE_CTL_7);
	len += dump_i2c_register(codec, buf+len, ESMT_TERNARY_SW_LVL);
	len += dump_i2c_register(codec, buf+len, ESMT_VOL_FINE_TUNE_L);
	len += dump_i2c_register(codec, buf+len, ESMT_VOL_FINE_TUNE_H);
	len += dump_i2c_register(codec, buf+len, ESMT_DEVICE_ID);
	len += dump_i2c_register(codec, buf+len, ESMT_I2S_DATA_OUT);
	len += dump_i2c_register(codec, buf+len, ESMT_MONO_KEY_H);
	len += dump_i2c_register(codec, buf+len, ESMT_MONO_KEY_L);

#if 1
	/* TODO: can not read anything from ram registers now */
	/* dump ram registers */
	len += sprintf(buf+len, "\nEQ RAM Bank 1:\n");

	for (i = ESMT_CH1_EQ1; i <= (ESMT_CH1_EQ15+4); i++)
		len += dump_ram_register(codec, buf+len, AD82088_BANK_1, i);

	for (i = ESMT_L_SRS_HPF; i <= (ESMT_L_SRS_LPF+2); i++)
		len += dump_ram_register(codec, buf+len, AD82088_BANK_1, i);

	len += sprintf(buf+len, "\nEQ RAM Bank 2:\n");

	for (i = ESMT_CH2_EQ1; i <= (ESMT_CH2_EQ15+4); i++)
		len += dump_ram_register(codec, buf+len, AD82088_BANK_2, i);

	for (i = ESMT_L_SRS_HPF; i <= (ESMT_L_SRS_LPF+2); i++)
		len += dump_ram_register(codec, buf+len, AD82088_BANK_2, i);
#endif

	return len;
}

static ssize_t sys_spkname_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct ad82088_priv *ad82088 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = ad82088->codec;
	ssize_t len = 0;

	if (codec == NULL)
		return 0;

	len = sprintf(buf, "Xiaomi Codec AD82088\n");

	return len;
}

static DEVICE_ATTR(speaker_name, 0644, sys_spkname_show,  NULL);
static DEVICE_ATTR(dump,         0644, sys_register_show, NULL);

static struct attribute *ad82088_attributes[] = {
	&dev_attr_speaker_name.attr,
	&dev_attr_dump.attr,
	NULL
};

static struct attribute_group ad82088_group = {
	.attrs = ad82088_attributes
};
