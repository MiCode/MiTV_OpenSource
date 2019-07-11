#include <linux/module.h>
#include <linux/kernel.h>

char *get_reg_pos(const char *buf, const char *reg, size_t count)
{
	size_t l1, l2;

	l2 = strlen(reg);
	if (!l2)
		return NULL;

	l1 = count;
	while (l1 >= l2) {
		l1--;
		if (!memcmp(buf, reg, l2))
			return (char *)buf;
		buf++;
	}
	return NULL;
}

int parse_value_by_length(const char *buf,
	unsigned char *dest, int length)
{
	int i, ret;
	unsigned int tmp_value;
	char tmp_str[3] = {0, 0, 0};

	for (i = 0; i < length; i++) {
		while (' ' == *buf)
			buf++;
		tmp_str[0] = *buf++;
		tmp_str[1] = *buf++;

		ret = kstrtouint(tmp_str, 16, &tmp_value);
		if (ret < 0) {
			pr_err("[%s:%d]get value failed\n", __func__, __LINE__);
			return ret;
		}
		*(dest+i) = (unsigned char)tmp_value;
	}

	return 0;
}
