#ifndef __XIAOMI_PRIVATE_AUDIO_INTERFACE_H_
#define __XIAOMI_PRIVATE_AUDIO_INTERFACE_H_

#ifdef CONFIG_MITV_AUDIO_CODEC_DEBUG
struct class_compat *xiaomi_get_speaker_class(void);
#endif

enum {
	XIAOMI_SPK_TYPE_08W = 0,
	XIAOMI_SPK_TYPE_10W = 1,
	XIAOMI_SPK_TYPE_MAX,
};

int xiaomi_get_speaker_type(void);

#endif
