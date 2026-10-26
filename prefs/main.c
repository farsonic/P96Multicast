/*
 * P96Multicast - MulticastPrefs: settings for Multicast.card and Wrap.card,
 * and their installation, without P96's own tools.
 *
 *   MulticastPrefs                      the window (also from Workbench)
 *   MulticastPrefs PAGE=1               ... on a page: 0 Network, 1 Display,
 *                                       2 Wrap.card, 3 Status
 *   MulticastPrefs PUBSCREEN=name       ... on that public screen (e.g. one
 *                                       on the card itself, mctest PUBSCREEN)
 *   MulticastPrefs TIMEOUT=10           ... closing after 10 seconds (tests)
 *   MulticastPrefs SHOW                 the settings, and their defaults
 *   MulticastPrefs SET FPS=10 MODES=640x480,1280x960
 *                                       change settings, saved to ENVARC:
 *                                       and ENV: (USE: ENV: only); "FPS 10"
 *                                       works too, and "SPAN=" unsets
 *   MulticastPrefs SET CARD=Multicast.card SPAN=2x2 HEAD=Multicast.card
 *                                       (sets BOARDS=5 to match)
 *   MulticastPrefs INSTALL [SETUP=SINGLE|TWO|MIRROR|SPAN] [DRY] [FROM=dir]
 *                                       everything for the next boot: the
 *                                       cards (from FROM, or the package),
 *                                       mctest, wrapstat and MulticastPrefs,
 *                                       the monitor, screen modes if P96 has
 *                                       none, DEST=10.0.2.2 on UAE's NAT; with
 *                                       SETUP, one or two Multicast screens,
 *                                       your display card mirrored, or its
 *                                       screen over two Multicast boards (SPAN,
 *                                       SPAN2x2: four), one desktop. DRY says
 *                                       what it would do
 *   MulticastPrefs STATUS               what the running cards say
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "mcprefs.h"

static const char version[] = "$VER: MulticastPrefs " MP_VERSION " (" MP_DATE ")";

static const char template[] = "SHOW/S,SET/M,INSTALL/S,DRY/S,FROM/K,SETUP/K,USE/S,STATUS/S,PAGE/N,TIMEOUT/N,PUBSCREEN/K";
enum { A_SHOW, A_SET, A_INSTALL, A_DRY, A_FROM, A_SETUP, A_USE, A_STATUS, A_PAGE, A_TIMEOUT, A_PUBSCREEN,
       A_COUNT };

static void print_note(const char *s)
{
	printf("%s\n", s);
}

int main(int argc, char **argv)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	int rc = RETURN_OK;

	prefs_load();
	if (argc == 0)
		return gui(0, 0, NULL);     /* from Workbench */

	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), (STRPTR)"MulticastPrefs");
		return RETURN_FAIL;
	}
	if (args[A_SET]) {
		STRPTR *kv;

		for (kv = (STRPTR *)args[A_SET]; *kv; kv++) {
			char pair[200];
			const char *why;
			int i;

			/* KEY=VALUE as one item, or (ReadArgs may split it) KEY and VALUE */
			for (i = 0; (*kv)[i] && (*kv)[i] != '='; i++)
				;
			if ((*kv)[i] == '=') {
				snprintf(pair, sizeof(pair), "%s", (char *)*kv);
			} else if (kv[1] && !find_setting((char *)kv[1])) {
				snprintf(pair, sizeof(pair), "%s=%s", (char *)kv[0], (char *)kv[1]);
				kv++;
			} else {
				snprintf(pair, sizeof(pair), "%s=", (char *)*kv);
			}
			why = prefs_set_line(pair);

			if (why) {
				printf("MulticastPrefs: %s\n", why);
				rc = RETURN_ERROR;
			}
		}
		if (rc == RETURN_OK) {
			prefs_fix(print_note);
			if (!prefs_write(!args[A_USE])) {
				printf("MulticastPrefs: could not write the settings\n");
				rc = RETURN_FAIL;
			}
		}
	}
	if (args[A_INSTALL] && rc == RETURN_OK) {
		prefs_fix(print_note);
		if (!install(print_note, args[A_DRY] != 0, (const char *)args[A_FROM],
		             (const char *)args[A_SETUP]))
			rc = RETURN_WARN;
		if (!args[A_DRY] && !prefs_write(TRUE))
			rc = RETURN_FAIL;
	}
	if (args[A_SHOW])
		prefs_show();
	if (args[A_STATUS]) {
		char lines[24][80];
		int n = status_lines(lines, 24), i;

		for (i = 0; i < n; i++)
			printf("%s\n", lines[i]);
	}
	if (!args[A_SET] && !args[A_INSTALL] && !args[A_SHOW] && !args[A_STATUS])
		rc = gui(args[A_PAGE] ? *(LONG *)args[A_PAGE] : 0,
		         args[A_TIMEOUT] ? *(LONG *)args[A_TIMEOUT] : 0, (const char *)args[A_PUBSCREEN]);
	FreeArgs(rda);
	return rc;
}
