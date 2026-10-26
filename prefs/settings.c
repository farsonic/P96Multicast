/*
 * P96Multicast - MulticastPrefs: the settings.
 *
 * ENV:Multicast.prefs and ENV:Wrap.prefs (or ENVARC:, if ENV: has none),
 * KEY=VALUE per line, '#' for comments: the same files the cards read. A
 * file is kept as its lines, so comments, lines this tool doesn't know and
 * their order survive; a change replaces the line with the key, or a
 * commented-out one ("#FPS=25"), or goes at the end.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

const struct Setting settings[] = {
	/* Multicast.prefs: see README.md */
	{ "DEST",      FILE_MC, T_IP,   0, 0,       "239.255.96.1", "Where to send: a multicast group, or one machine" },
	{ "PORT",      FILE_MC, T_NUM,  1, 65535,   "9696",  "UDP port; board n sends to PORT+n" },
	{ "TTL",       FILE_MC, T_NUM,  0, 255,     "1",     "Multicast hops (1: this LAN only)" },
	{ "FPS",       FILE_MC, T_NUM,  1, 50,      "50",    "Update passes per second" },
	{ "RATE",      FILE_MC, T_NUM,  16, 1000000, "1500", "Most KB per second to send" },
	{ "REFRESH",   FILE_MC, T_NUM,  0, 3600,    "10",    "Seconds between whole screens while no viewer answers (0: never)" },
	{ "SCAN",      FILE_MC, T_NUM,  1, 60,      "2",     "Seconds to check the whole screen in, at the fastest" },
	{ "CPU",       FILE_MC, T_NUM,  1, 100,     "20",    "Most CPU percent for checking the screen" },
	{ "HISTORY",   FILE_MC, T_NUM,  16, 4096,   "512",   "KB of sent data kept to repair losses" },
	{ "POINTER",   FILE_MC, T_BOOL, 0, 1,       "1",     "The viewers draw the mouse pointer" },
	{ "SPLIT",     FILE_MC, T_SPAN, 0, 0,       "1x1",   "The screen in CxR parts, a stream (and a window) each, ports PORT, PORT+1, ..." },
	/* the shared screen MulticastDesktop opens (the card itself ignores these) */
	{ "MONITOR",   FILE_MC, T_MODES, 0, 0,      "800x600", "Size of each part (monitor) of the shared screen" },
	{ "COLOURS",   FILE_MC, T_NUM,  8, 32,      "16",    "Bits per pixel of the shared screen: 8, 16 or 32" },
	{ "PUBSCREEN", FILE_MC, T_TEXT, 0, 31,      "MULTICAST", "Name of the shared public screen" },
	{ "DEFAULTSCREEN", FILE_MC, T_BOOL, 0, 1,   "1",     "Programs open on the shared screen" },
	{ "BEHIND",    FILE_MC, T_BOOL, 0, 1,       "0",     "Open the shared screen behind the Workbench" },
	{ "ATBOOT",    FILE_MC, T_BOOL, 0, 1,       "0",     "Open the shared screen at every boot (WBStartup)" },
	{ "HOTKEYS",   FILE_MC, T_TEXT, 0, 31,      "ctrl+alt", "Qualifiers of MulticastDesktop's hotkeys, + between them" },
	{ "VRAM",      FILE_MC, T_NUM,  1, 256,     "8",     "Video memory per board, MB (at boot)" },
	{ "MAXWIDTH",  FILE_MC, T_NUM,  320, 4096,  "1920",  "Widest built-in mode" },
	{ "MAXHEIGHT", FILE_MC, T_NUM,  200, 4096,  "1200",  "Highest built-in mode" },
	{ "MODES",     FILE_MC, T_MODES, 0, 0,      "",      "Screen modes the card adds: WxH,WxH,... (none: P96's own, Picasso96Mode)" },
	{ "BOARDS",    FILE_MC, T_NUM,  1, 8,       "1",     "Boards to offer (more for Wrap.card's heads)" },
	{ "BLITTER",   FILE_MC, T_BOOL, 0, 1,       "1",     "Send the drawing as orders, not pixels" },
	{ "BOARDTYPE", FILE_MC, T_NUM,  0, 255,     "6",     "P96 board type reported" },
	{ "PRI",       FILE_MC, T_NUM,  -128, 127,  "-1",    "Sender task priority" },
	{ "LOG",       FILE_MC, T_TEXT, 0, 79,      "",      "Log file, e.g. T:Multicast.log" },
	{ "QUIET",     FILE_MC, T_BOOL, 0, 1,       "0",     "Set BIF_QUIET" },
	/* Wrap.prefs: see docs/WRAP.md */
	{ "CARD",      FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver Wrap.card wraps, e.g. Multicast.card" },
	{ "DRAW",      FILE_WRAP, T_BOOL, 0, 1,     "1",     "Wrap the drawing functions too" },
	{ "MIRROR",    FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver to show the same screen on" },
	{ "SPAN",      FILE_WRAP, T_SPAN, 0, 0,     "",      "One big screen on CxR heads, e.g. 2x2" },
	{ "HEAD",      FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver of every head" },
	{ "HEAD1",     FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver of head 1" },
	{ "HEAD2",     FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver of head 2" },
	{ "HEAD3",     FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver of head 3" },
	{ "HEAD4",     FILE_WRAP, T_TEXT, 0, 31,    "",      "Card driver of head 4" },
	{ "HEADSHARE", FILE_WRAP, T_BOOL, 0, 1,     "0",     "Heads that can show any memory need no copy" },
	{ "MIRRORFPS", FILE_WRAP, T_NUM,  1, 50,    "25",    "Head updates per second" },
};
const int nsettings = sizeof(settings) / sizeof(settings[0]);

struct PrefsFile files[NFILES] = { { "Multicast.prefs" }, { "Wrap.prefs" } };

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int mp_stricmp(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return lower(*a) - lower(*b);
}

const struct Setting *find_setting(const char *key)
{
	int i;

	for (i = 0; i < nsettings; i++)
		if (!mp_stricmp(settings[i].key, key))
			return &settings[i];
	return NULL;
}

/* the line's key, if it is "KEY=..." (or "#KEY=..." with comment set) */
static BOOL line_key(const char *line, const char *key, BOOL comment, const char **value)
{
	const char *p = line;

	while (*p == ' ' || *p == '\t')
		p++;
	if (comment) {
		if (*p != '#')
			return FALSE;
		p++;
	} else if (*p == '#') {
		return FALSE;
	}
	while (*key) {
		if (lower(*p) != lower(*key))
			return FALSE;
		p++;
		key++;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != '=')
		return FALSE;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	*value = p;
	return TRUE;
}

static int find_line(struct PrefsFile *f, const char *key, BOOL comment)
{
	const char *v;
	int i;

	for (i = 0; i < f->nlines; i++)
		if (line_key(f->lines[i], key, comment, &v))
			return i;
	return -1;
}

static BOOL read_file(struct PrefsFile *f, const char *dir)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	char path[64];
	APTR oldwin = me->pr_WindowPtr;
	BPTR fh;

	snprintf(path, sizeof(path), "%s%s", dir, f->name);
	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)path, MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return FALSE;
	f->nlines = 0;
	while (f->nlines < MP_LINES && FGets(fh, (STRPTR)f->lines[f->nlines], MP_LINELEN)) {
		char *l = f->lines[f->nlines];
		int n = strlen(l);

		while (n && (l[n - 1] == '\n' || l[n - 1] == '\r'))
			l[--n] = 0;
		f->nlines++;
	}
	Close(fh);
	return TRUE;
}

void prefs_load(void)
{
	int i;

	for (i = 0; i < NFILES; i++) {
		files[i].nlines = 0;
		files[i].loaded = read_file(&files[i], "ENV:") || read_file(&files[i], "ENVARC:");
	}
}

static BOOL write_file(struct PrefsFile *f, const char *dir)
{
	char path[64];
	BPTR fh;
	int i;

	snprintf(path, sizeof(path), "%s%s", dir, f->name);
	fh = Open((STRPTR)path, MODE_NEWFILE);
	if (!fh)
		return FALSE;
	for (i = 0; i < f->nlines; i++) {
		FPuts(fh, (STRPTR)f->lines[i]);
		FPutC(fh, '\n');
	}
	Close(fh);
	return TRUE;
}

/*
 * Use: ENV: only, the cards read it when they start (and Multicast.card's
 * network settings again when the TCP/IP stack appears). Save: ENVARC: too,
 * for the next boot.
 */
BOOL prefs_write(BOOL envarc)
{
	BOOL ok = TRUE;
	int i;

	for (i = 0; i < NFILES; i++) {
		if (!files[i].nlines && !files[i].loaded)
			continue;
		ok &= write_file(&files[i], "ENV:");
		if (envarc)
			ok &= write_file(&files[i], "ENVARC:");
	}
	return ok;
}

const char *prefs_get(const struct Setting *s)
{
	struct PrefsFile *f = &files[s->file];
	const char *v;
	int i = find_line(f, s->key, FALSE);

	if (i < 0)
		return NULL;
	line_key(f->lines[i], s->key, FALSE, &v);
	return v;
}

const char *prefs_value(const struct Setting *s)
{
	const char *v = prefs_get(s);

	return v ? v : s->def;
}

static BOOL parse_long(const char **p, LONG *out)
{
	LONG n = 0;
	BOOL neg = FALSE, any = FALSE;

	if (**p == '-') {
		neg = TRUE;
		(*p)++;
	}
	while (**p >= '0' && **p <= '9') {
		n = n * 10 + (*(*p)++ - '0');
		any = TRUE;
	}
	*out = neg ? -n : n;
	return any;
}

/* why the value won't do, or NULL */
const char *prefs_check(const struct Setting *s, const char *v)
{
	static char why[80];
	const char *p = v;
	LONG n, m;
	int i;

	if (!*v)
		return NULL;                /* unset */
	switch (s->type) {
	case T_NUM:
		if (!parse_long(&p, &n) || *p || n < s->min || n > s->max) {
			snprintf(why, sizeof(why), "must be a number from %ld to %ld", s->min, s->max);
			return why;
		}
		return NULL;
	case T_BOOL:
		if ((v[0] != '0' && v[0] != '1') || v[1])
			return "must be 0 or 1";
		return NULL;
	case T_IP:
		for (i = 0; i < 4; i++) {
			if (!parse_long(&p, &n) || n > 255)
				return "must be an IPv4 address, like 239.255.96.1";
			if (i < 3 && *p++ != '.')
				return "must be an IPv4 address, like 239.255.96.1";
		}
		return *p ? "must be an IPv4 address, like 239.255.96.1" : NULL;
	case T_TEXT:
		for (i = 0; v[i]; i++)
			if (v[i] == ' ' || v[i] == '\t')
				return "must not contain blanks";
		if (i > s->max)
			return "is too long";
		return NULL;
	case T_MODES:
		if (!mp_stricmp(v, "none"))
			return NULL;
		for (i = 0; *p; i++) {
			if (!parse_long(&p, &n) || (*p != 'x' && *p != 'X'))
				return "must be sizes like 640x480,1280x960, or NONE";
			p++;
			if (!parse_long(&p, &m))
				return "must be sizes like 640x480,1280x960, or NONE";
			if (n < 160 || n > 4095 || m < 100 || m > 4095)
				return "sizes go from 160x100 to 4095x4095";
			while (*p == ',' || *p == ' ')
				p++;
		}
		if (i > 24)
			return "has more than 24 sizes";
		return NULL;
	case T_SPAN:
		if (!parse_long(&p, &n) || (*p != 'x' && *p != 'X'))
			return "must be columns x rows, like 2x2";
		p++;
		if (!parse_long(&p, &m) || *p || n < 1 || m < 1)
			return "must be columns x rows, like 2x2";
		if (n * m > 4)
			return "has more than 4 heads";
		return NULL;
	}
	return NULL;
}

/* "" unsets (the line is commented out, so it shows what it was) */
const char *prefs_set(const struct Setting *s, const char *value)
{
	struct PrefsFile *f = &files[s->file];
	const char *why = prefs_check(s, value);
	int i;

	if (why)
		return why;
	i = find_line(f, s->key, FALSE);
	if (!*value) {
		if (i >= 0) {
			char old[MP_LINELEN];

			strncpy(old, f->lines[i], MP_LINELEN - 2);
			old[MP_LINELEN - 2] = 0;
			snprintf(f->lines[i], MP_LINELEN, "#%s", old);
		}
		return NULL;
	}
	if (i < 0)
		i = find_line(f, s->key, TRUE);
	if (i < 0) {
		if (f->nlines >= MP_LINES)
			return "the file is full";
		i = f->nlines++;
	}
	snprintf(f->lines[i], MP_LINELEN, "%s=%s", s->key, value);
	return NULL;
}

const char *prefs_set_line(const char *kv)
{
	static char why[100];
	char key[16];
	const struct Setting *s;
	const char *err;
	int i;

	for (i = 0; kv[i] && kv[i] != '=' && i < sizeof(key) - 1; i++)
		key[i] = kv[i];
	key[i] = 0;
	if (kv[i] != '=')
		return "use KEY=VALUE";
	s = find_setting(key);
	if (!s) {
		snprintf(why, sizeof(why), "no setting %s", key);
		return why;
	}
	err = prefs_set(s, kv + i + 1);
	if (err) {
		snprintf(why, sizeof(why), "%s %s", s->key, err);
		return why;
	}
	return NULL;
}

static BOOL is_multicast(const char *card)
{
	const char *p = card, *name = card;

	for (; *p; p++)
		if (*p == ':' || *p == '/')
			name = p + 1;
	return !mp_stricmp(name, "Multicast.card");
}

/*
 * Settings that must agree: with Wrap.card wrapping Multicast.card and heads
 * that are Multicast boards too, BOARDS has to cover them all.
 */
/*
 * SPLIT= with the shared desktop of MONITOR= parts: the card must offer a
 * mode of the whole desktop's size (MODES=), and have the video memory for
 * it in COLOURS= bits.
 */
static void desktop_fix(void (*note)(const char *))
{
	const char *split = prefs_value(find_setting("SPLIT"));
	const char *mon = prefs_value(find_setting("MONITOR"));
	const char *modes = prefs_get(find_setting("MODES"));
	const char *p;
	LONG c, r, w, h, bits, mb, vram;
	char size[16], buf[200];

	p = split;
	if (!parse_long(&p, &c) || (*p != 'x' && *p != 'X'))
		return;
	p++;
	if (!parse_long(&p, &r) || c * r < 2)
		return;
	p = mon;
	if (!parse_long(&p, &w) || (*p != 'x' && *p != 'X'))
		return;
	p++;
	if (!parse_long(&p, &h))
		return;
	snprintf(size, sizeof(size), "%ldx%ld", c * w, r * h);
	/* the size in the list already? */
	if (!modes || !*modes || !mp_stricmp(modes, "none")) {
		prefs_set(find_setting("MODES"), size);
		snprintf(buf, sizeof(buf), "MODES=%s: the desktop's size (it needs a reboot)", size);
		note(buf);
	} else {
		int l = strlen(size), found = 0;

		for (p = modes; *p; ) {
			if (!strncmp(p, size, l) && (p[l] == ',' || !p[l]))
				found = 1;
			while (*p && *p != ',')
				p++;
			if (*p)
				p++;
		}
		if (!found && strlen(modes) + l + 2 < sizeof(buf)) {
			snprintf(buf, sizeof(buf), "%s,%s", size, modes);
			prefs_set(find_setting("MODES"), buf);
			snprintf(buf, sizeof(buf), "MODES= now has %s, the desktop's size (it needs a reboot)",
			         size);
			note(buf);
		}
	}
	p = prefs_value(find_setting("COLOURS"));
	parse_long(&p, &bits);
	mb = (c * w * r * h * ((bits + 7) / 8) + 1048575) / 1048576 + 1;
	p = prefs_value(find_setting("VRAM"));
	parse_long(&p, &vram);
	if (vram < mb) {
		snprintf(buf, sizeof(buf), "%ld", mb);
		prefs_set(find_setting("VRAM"), buf);
		snprintf(buf, sizeof(buf), "VRAM=%ld: enough for the desktop (it needs a reboot)", mb);
		note(buf);
	}
}

void prefs_fix(void (*note)(const char *))
{
	const struct Setting *bs = find_setting("BOARDS");
	const char *card = prefs_value(find_setting("CARD"));
	const char *span = prefs_get(find_setting("SPAN"));
	const char *mirror = prefs_get(find_setting("MIRROR"));
	const char *head = prefs_value(find_setting("HEAD"));
	LONG need, have, c, r;
	const char *p;
	char buf[80];
	int i;

	if (!*card) {
		desktop_fix(note);
		return;
	}
	/* the wrapped board, if it is a Multicast board, and each Multicast head */
	need = is_multicast(card) ? 1 : 0;
	if (span && *span) {
		p = span;
		parse_long(&p, &c);
		p++;
		parse_long(&p, &r);
		for (i = 0; i < c * r && i < 4; i++) {
			char key[6];
			const char *h;

			snprintf(key, sizeof(key), "HEAD%d", i + 1);
			h = prefs_get(find_setting(key));
			if (is_multicast(h && *h ? h : head))
				need++;
		}
	} else if (mirror && is_multicast(mirror)) {
		need++;
	}
	p = prefs_value(bs);
	parse_long(&p, &have);
	if (have < need) {
		snprintf(buf, sizeof(buf), "%ld", need);
		prefs_set(bs, buf);
		snprintf(buf, sizeof(buf), "BOARDS=%ld: %s Multicast heads", need,
		         is_multicast(card) ? "the wrapped board and its" : "the");
		note(buf);
	}
}

void prefs_show(void)
{
	int f, i;

	for (f = 0; f < NFILES; f++) {
		printf("%s%s\n", files[f].name, files[f].loaded ? "" : " (none yet)");
		for (i = 0; i < nsettings; i++) {
			const struct Setting *s = &settings[i];
			const char *v;

			if (s->file != f)
				continue;
			v = prefs_get(s);
			printf("  %-10s %-24s %s\n", s->key, v ? v : *s->def ? s->def : "-",
			       v ? "" : "(default)");
		}
	}
}
