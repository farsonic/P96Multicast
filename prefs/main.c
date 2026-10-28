/*
 * Panorama - PanoramaPrefs: settings for Panorama.card and Wrap.card,
 * and their installation, without P96's own tools.
 *
 *   PanoramaPrefs                      the window (also from Workbench)
 *   PanoramaPrefs PAGE=1               ... on a page: 0 Desktop, 1 Network,
 *                                       2 Display, 3 Wrap.card, 4 Status
 *   PanoramaPrefs PUBSCREEN=name       ... on that public screen (e.g. one
 *                                       on the card itself, mctest PUBSCREEN)
 *   PanoramaPrefs TIMEOUT=10           ... closing after 10 seconds (tests)
 *   PanoramaPrefs SHOW                 the settings, and their defaults
 *   PanoramaPrefs SET FPS=10 MODES=640x480,1280x960
 *                                       change settings, saved to ENVARC:
 *                                       and ENV: (USE: ENV: only); "FPS 10"
 *                                       works too, and "SPAN=" unsets
 *   PanoramaPrefs SET CARD=Panorama.card SPAN=2x2 HEAD=Panorama.card
 *                                       (sets BOARDS=5 to match)
 *   PanoramaPrefs INSTALL [SETUP=SINGLE|TWO|MIRROR|SPAN] [DRY] [FROM=dir]
 *                                       everything for the next boot: the
 *                                       cards (from FROM, or the package),
 *                                       mctest, wrapstat and PanoramaPrefs,
 *                                       the monitor, screen modes if P96 has
 *                                       none, DEST=10.0.2.2 on UAE's NAT; with
 *                                       SETUP, one or two Panorama screens,
 *                                       your display card mirrored, or its
 *                                       screen over two Panorama boards (SPAN,
 *                                       SPAN2x2: four), one desktop. DRY says
 *                                       what it would do
 *                                       Then it asks to reboot (REBOOT: without
 *                                       asking, NOREBOOT: not at all)
 *   PanoramaPrefs STATUS               what the running cards say
 *   PanoramaPrefs OPEN                 the window's Open: the viewers' mouse
 *                                       and keys on, P96ScreenCx started if
 *                                       it is installed, the desktop opened
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "mcprefs.h"

static const char version[] = "$VER: PanoramaPrefs " MP_VERSION " (" MP_DATE ")";

static const char template[] = "SHOW/S,SET/M,INSTALL/S,DRY/S,FROM/K,SETUP/K,USE/S,STATUS/S,PAGE/N,TIMEOUT/N,PUBSCREEN/K,REBOOT/S,NOREBOOT/S,TRACE/K,NOMUI/S,OPEN/S";
enum { A_SHOW, A_SET, A_INSTALL, A_DRY, A_FROM, A_SETUP, A_USE, A_STATUS, A_PAGE, A_TIMEOUT, A_PUBSCREEN,
       A_REBOOT, A_NOREBOOT, A_TRACE, A_NOMUI, A_OPEN,
       A_COUNT };

/*
 * y or n from the console, a key at a time: a line read (fgets) waits for a
 * newline that some shells' terminals (bash's) never send for Return. The
 * first y or n counts; Return alone, or nothing for 30 s, is no.
 */
static BOOL ask_yes(const char *question)
{
	BPTR in = Input();
	int waited = 0;
	char c;

	printf("%s", question);
	fflush(stdout);
	while (waited < 30) {
		if (!WaitForChar(in, 1000000)) {
			waited++;
			continue;
		}
		if (Read(in, &c, 1) != 1)
			break;
		if ((c | 32) == 'y') {
			printf("\n");
			return TRUE;
		}
		if ((c | 32) == 'n' || c == '\r' || c == '\n')
			break;
	}
	printf(waited >= 30 ? "\nNo answer: not rebooting.\n" : "\n");
	return FALSE;
}

static void print_note(const char *s)
{
	printf("%s\n", s);
}

static int prefs_main(int argc);

/* (bigstack.s) fn(arg) on the stack ss describes */
long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg);

static long main_on_stack(void *argc)
{
	return prefs_main((int)(LONG)argc);
}

/*
 * All of it on a stack of 64 KB when the Shell gave less than 32 KB (a
 * bash, or a Shell's default 4 KB): the windows, the install code and the
 * settings keep buffers on the stack, and running past a small one froze
 * the machine somewhere later (a click on Monitors, Save).
 */
int main(int argc, char **argv)
{
	struct Task *me = FindTask(NULL);
	ULONG have = (ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower;
	struct StackSwapStruct ss;
	APTR stack;
	long rc;

	if (have >= 32768 || !(stack = AllocVec(65536, MEMF_ANY)))
		return prefs_main(argc);
	ss.stk_Lower = stack;
	ss.stk_Upper = (ULONG)stack + 65536;
	ss.stk_Pointer = (APTR)ss.stk_Upper;
	rc = run_on_stack(&ss, main_on_stack, (void *)(LONG)argc);
	FreeVec(stack);
	return rc;
}

static int prefs_main(int argc)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	int rc = RETURN_OK;

	prefs_load();
	if (argc == 0)
	{
		int rc = mui_gui(0, 0, NULL);   /* from Workbench */

		return rc >= 0 ? rc : gui(0, 0, NULL);
	}

	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), (STRPTR)"PanoramaPrefs");
		return RETURN_FAIL;
	}
	if (args[A_TRACE]) {
		mp_trace_file = (const char *)args[A_TRACE];
		mp_trace("PanoramaPrefs " MP_VERSION ": tracing");
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
				printf("PanoramaPrefs: %s\n", why);
				rc = RETURN_ERROR;
			}
		}
		if (rc == RETURN_OK) {
			prefs_fix(print_note);
			if (!prefs_write(!args[A_USE])) {
				printf("PanoramaPrefs: could not write the settings\n");
				rc = RETURN_FAIL;
			} else {
				place_monitors(print_note);
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
		/* it all starts at the next boot: REBOOT does it, else ask */
		if (rc == RETURN_OK && !args[A_DRY] && !args[A_NOREBOOT]) {
			BOOL now = args[A_REBOOT] != 0;

			if (!now && IsInteractive(Input()))
				now = ask_yes("Reboot now? (y/n) ");
			if (now) {
				printf("Rebooting...\n");
				fflush(stdout);
				mp_reboot();
			}
		}
	}
	if (args[A_OPEN] && rc == RETURN_OK) {
		BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);

		desktop_ready(print_note);
		prefs_fix(print_note);
		if (!prefs_write(FALSE)) {
			printf("PanoramaPrefs: could not write the settings\n");
			rc = RETURN_FAIL;
		} else if (SystemTags((STRPTR)"C:PanoramaDesk", SYS_Input, in, SYS_Output, out,
		                      SYS_Asynch, TRUE, TAG_END) != -1) {
			in = out = 0;
		} else {
			printf("PanoramaPrefs: could not start C:PanoramaDesk\n");
			rc = RETURN_FAIL;
		}
		if (in)
			Close(in);
		if (out)
			Close(out);
	}
	if (args[A_SHOW])
		prefs_show();
	if (args[A_STATUS]) {
		char lines[24][80];
		int n = status_lines(lines, 24), i;

		for (i = 0; i < n; i++)
			printf("%s\n", lines[i]);
	}
	if (!args[A_SET] && !args[A_INSTALL] && !args[A_SHOW] && !args[A_STATUS] && !args[A_OPEN])
	{
		int page = args[A_PAGE] ? *(LONG *)args[A_PAGE] : 0;
		int timeout = args[A_TIMEOUT] ? *(LONG *)args[A_TIMEOUT] : 0;

		mp_trace("window: %s", args[A_NOMUI] ? "GadTools (NOMUI)" : "MUI");
		rc = args[A_NOMUI] ? -1 : mui_gui(page, timeout, (const char *)args[A_PUBSCREEN]);
		if (rc < 0)
			rc = gui(page, timeout, (const char *)args[A_PUBSCREEN]);
	}
	FreeArgs(rda);
	return rc;
}
