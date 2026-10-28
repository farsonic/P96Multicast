/*
 * Panorama - PanoramaPrefs: settings for Panorama.card and Wrap.card
 * without P96's own tools.
 *
 * settings.c  the settings (ENV:Panorama.prefs, ENV:Wrap.prefs): read,
 *             check, change, write, keeping lines it doesn't know
 * install.c   copies the cards, makes the monitor and its icon
 * status.c    what the running cards say about themselves
 * gui.c       the GadTools window
 * main.c      the command line and Workbench start
 */
#ifndef P96MC_MCPREFS_H
#define P96MC_MCPREFS_H

#include <exec/types.h>

#define MP_VERSION "3.1"
#define MP_DATE "26.10.2026"

/* the two files */
#define FILE_MC   0   /* Panorama.prefs */
#define FILE_WRAP 1   /* Wrap.prefs */
#define NFILES    2

/* kinds of value */
#define T_NUM   0     /* a number between min and max */
#define T_BOOL  1     /* 0 or 1 */
#define T_IP    2     /* a.b.c.d */
#define T_TEXT  3     /* a name or a path, no blanks */
#define T_MODES 4     /* WxH,WxH,... or NONE */
#define T_SPAN  5     /* CxR, at most 4 heads */
#define T_PLACE 6     /* LEFT, RIGHT, ABOVE, BELOW and a monitor (LEFT1, BELOW2), or NONE */

struct Setting {
	const char *key;
	UBYTE file;
	UBYTE type;
	LONG  min, max;
	const char *def;    /* what the card uses without the line */
	const char *help;
};

extern const struct Setting settings[];
extern const int nsettings;

#define MP_LINES   128
#define MP_LINELEN 200

struct PrefsFile {
	const char *name;           /* "Panorama.prefs" */
	char  lines[MP_LINES][MP_LINELEN];
	int   nlines;
	BOOL  loaded;               /* read from ENV: or ENVARC: */
};

extern struct PrefsFile files[NFILES];

/* settings.c */
const struct Setting *find_setting(const char *key);
void prefs_load(void);
BOOL prefs_write(BOOL envarc);          /* ENV:, and ENVARC: too */
const char *prefs_get(const struct Setting *s);       /* NULL: not set */
const char *prefs_value(const struct Setting *s);     /* set, or the default */
const char *prefs_check(const struct Setting *s, const char *value);  /* NULL: fine */
const char *prefs_set(const struct Setting *s, const char *value);    /* "" unsets */
const char *prefs_set_line(const char *keyvalue);     /* "KEY=VALUE" */
void prefs_fix(void (*note)(const char *));          /* consistent settings */
void prefs_show(void);
int  mp_stricmp(const char *a, const char *b);

/* TRACE=file: each step to that file, written at once (to find a hang) */
extern const char *mp_trace_file;
void mp_trace(const char *fmt, ...);

/*
 * WORKBENCH=: where the Workbench screen is, next to the desktop: on the
 * side (WB_LEFT ...) of monitor mon (0 for monitor 1). wb_fit() moves it to
 * an outer edge of a C x R desktop, where the mouse can cross over.
 */
#define WB_NONE  0
#define WB_LEFT  1
#define WB_RIGHT 2
#define WB_ABOVE 3
#define WB_BELOW 4
BOOL wb_place(const char *v, int *side, int *mon);
void wb_fit(int *side, int *mon, int cols, int rows);
void wb_name(char *buf, int len, int side, int mon);

/* install.c */
BOOL install(void (*note)(const char *), BOOL dry, const char *from, const char *setup);
void mp_reboot(void);
void place_monitors(void (*note)(const char *));
void desktop_ready(void (*note)(const char *));    /* INPUT on, P96ScreenCx running */   /* tool types for P96ScreenCx */

/* status.c */
int status_lines(char lines[][80], int max);

/* gui.c */
int gui(int page, int timeout, const char *pubscreen);       /* GadTools, without MUI */
int mui_gui(int page, int timeout, const char *pubscreen);   /* MUI; -1 if it isn't there */

#endif
