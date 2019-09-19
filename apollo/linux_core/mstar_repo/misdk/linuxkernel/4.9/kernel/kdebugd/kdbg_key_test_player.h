/*
 *  kernel/kdebugd/kdbg_key_test_player.h
 *
 *  Advance oprofile related declarations
 *
 *  Copyright (C) 2009  Samsung
 *
 *  2010.03.05 Created by gaurav.j
 *
 */

#ifndef _LINUX_KDBG_KEY_TEST_PLAYER_H
#define _LINUX_KDBG_KEY_TEST_PLAYER_H

#include "kdebugd.h"

#define KDBG_KEY_TEST_PLAYER_DEFAULT_DELAY 2500
#define KEY_TEST_PLAYER_START_DELAY 100

/* Key Test Player structure */
struct kdbg_key_test_player {
	int delay;		/* Delay before each key events */
	bool restart;	/* 1-restart history logging */
	bool capture;	/* stop capture(0) and start capture(1) */
	bool prev_capture;
};

extern struct kdbg_key_test_player g_player;

/* Capture events in a history logging file */
int kdbg_capture_history(char *event, int size);

/* Start the Key test Player thread - read from the file */
int kdbg_start_key_test_player_thread(void);
/* Stop the Key test Player thread - read from the file */
int kdbg_stop_key_test_player_thread(void);
/*
 * Key Test Player  Module init function, which initialize Key Test Player Module and start functions
 * and allocateKey Test Player module.
 */
int kdbg_key_test_player_init(void);

#endif /* !_LINUX_KDBG_ELF_SYM_API_H */
