/*
 * P96Multicast - settings.
 *
 * KEY=VALUE pairs, from the monitor icon's ToolTypes (real P96 only; AROS
 * passes none) and from ENV:Multicast.prefs, one pair per line, '#' starts
 * a comment. Unknown keys are ignored. See README.md for the list.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "mcard.h"
#include "p96mc_proto.h"

void mc_config_defaults(struct McConfig *cfg)
{
	cfg->dest = MC_DEFAULT_GROUP;
	cfg->port = MC_DEFAULT_PORT;
	cfg->ttl = 1;
	cfg->pri = -1;
	cfg->fps = 25;
	cfg->rate = 1500UL * 1024;
	cfg->refresh = 5;
	cfg->vramsize = 8UL * 1024 * 1024;
	cfg->maxwidth = 1920;
	cfg->maxheight = 1200;
	cfg->boardtype = BT_PicassoII;
	cfg->boards = 1;
	cfg->quiet = 0;
	cfg->log[0] = 0;
}

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* key matches s up to '=' or a blank, ignoring case */
static const char *match_key(const char *s, const char *key)
{
	while (*key) {
		if (lower(*s) != *key)
			return NULL;
		s++;
		key++;
	}
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s != '=')
		return NULL;
	s++;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

static BOOL parse_num(const char *s, LONG *out)
{
	LONG v = 0;
	BOOL neg = FALSE, any = FALSE;

	if (*s == '-') {
		neg = TRUE;
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s++ - '0');
		any = TRUE;
	}
	*out = neg ? -v : v;
	return any;
}

static BOOL parse_ip(const char *s, ULONG *out)
{
	ULONG ip = 0;
	int i;

	for (i = 0; i < 4; i++) {
		LONG part = 0;
		BOOL any = FALSE;

		while (*s >= '0' && *s <= '9') {
			part = part * 10 + (*s++ - '0');
			any = TRUE;
		}
		if (!any || part > 255)
			return FALSE;
		ip = (ip << 8) | part;
		if (i < 3 && *s++ != '.')
			return FALSE;
	}
	*out = ip;
	return TRUE;
}

void mc_config_line(struct McConfig *cfg, const char *line)
{
	const char *v;
	LONG n;

	while (*line == ' ' || *line == '\t')
		line++;
	if ((v = match_key(line, "dest")) != NULL) {
		parse_ip(v, &cfg->dest);
	} else if ((v = match_key(line, "port")) != NULL) {
		if (parse_num(v, &n) && n > 0 && n < 65536)
			cfg->port = n;
	} else if ((v = match_key(line, "ttl")) != NULL) {
		if (parse_num(v, &n) && n >= 0 && n < 256)
			cfg->ttl = n;
	} else if ((v = match_key(line, "pri")) != NULL) {
		if (parse_num(v, &n) && n >= -128 && n < 128)
			cfg->pri = n;
	} else if ((v = match_key(line, "fps")) != NULL) {
		if (parse_num(v, &n) && n >= 1 && n <= 50)
			cfg->fps = n;
	} else if ((v = match_key(line, "rate")) != NULL) {
		/* KB per second */
		if (parse_num(v, &n) && n >= 16 && n <= 1000000)
			cfg->rate = n * 1024;
	} else if ((v = match_key(line, "refresh")) != NULL) {
		/* 0 turns the background refresh off */
		if (parse_num(v, &n) && n >= 0 && n <= 3600)
			cfg->refresh = n;
	} else if ((v = match_key(line, "vram")) != NULL) {
		/* megabytes */
		if (parse_num(v, &n) && n >= 1 && n <= 256)
			cfg->vramsize = n * 1024 * 1024;
	} else if ((v = match_key(line, "maxwidth")) != NULL) {
		if (parse_num(v, &n) && n >= 320 && n <= 4096)
			cfg->maxwidth = n;
	} else if ((v = match_key(line, "maxheight")) != NULL) {
		if (parse_num(v, &n) && n >= 200 && n <= 4096)
			cfg->maxheight = n;
	} else if ((v = match_key(line, "boardtype")) != NULL) {
		if (parse_num(v, &n) && n >= 0 && n < 256)
			cfg->boardtype = n;
	} else if ((v = match_key(line, "boards")) != NULL) {
		if (parse_num(v, &n) && n >= 1 && n <= MC_MAXBOARDS)
			cfg->boards = n;
	} else if ((v = match_key(line, "quiet")) != NULL) {
		if (parse_num(v, &n))
			cfg->quiet = n != 0;
	} else if ((v = match_key(line, "log")) != NULL) {
		/* a file name, up to the first blank; LOG= turns logging off */
		int i;

		for (i = 0; v[i] && v[i] != ' ' && v[i] != '\t' && i < sizeof(cfg->log) - 1; i++)
			cfg->log[i] = v[i];
		cfg->log[i] = 0;
	}
}

/*
 * Needs a process: reads the file with dos.library. FindCard() runs early in
 * the boot, possibly before ENV: is assigned, so a missing volume must fail
 * quietly instead of putting up an "insert volume" requester.
 */
BOOL mc_config_file(struct McConfig *cfg, const char *path)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin;
	BPTR fh;
	char *buf, *p, *line;
	LONG len;

	oldwin = me->pr_WindowPtr;
	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)path, MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return FALSE;
	buf = AllocMem(2048, MEMF_PUBLIC);
	if (!buf) {
		Close(fh);
		return FALSE;
	}
	len = Read(fh, buf, 2047);
	Close(fh);
	if (len < 0)
		len = 0;
	buf[len] = 0;

	for (line = p = buf; ; p++) {
		if (*p == '\n' || *p == '\r' || *p == 0) {
			char end = *p;

			*p = 0;
			if (*line != '#')
				mc_config_line(cfg, line);
			if (!end)
				break;
			line = p + 1;
		}
	}
	FreeMem(buf, 2048);
	return TRUE;
}

/*
 * ENV:Multicast.prefs, or ENVARC:Multicast.prefs when ENV: has not been
 * filled yet (the card may start before the Startup-Sequence copies it).
 */
void mc_config_env(struct McConfig *cfg)
{
	if (!mc_config_file(cfg, "ENV:Multicast.prefs"))
		mc_config_file(cfg, "ENVARC:Multicast.prefs");
}
