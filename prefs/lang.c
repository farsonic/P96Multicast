/*
 * Panorama - lang.c: tr(), see lang.h. The table is searched by halves on
 * the English; a missing translation, or an unknown language, gives the
 * English back.
 */
#include <string.h>
#include <exec/types.h>
#include <libraries/locale.h>
#include <proto/exec.h>
#include <proto/locale.h>
#include "lang.h"
#include "lang_table.h"

struct LocaleBase *LocaleBase;
static int column;      /* 0 English, 1 German, 2 French, 3 Spanish, 4 Italian */

/* the default locale's language, if it is one of the table's */
void lang_init(void)
{
	static const char *const names[] = { "deutsch", "fran\347ais", "espa\361ol", "italiano" };
	struct Locale *loc;
	int i;

	column = 0;
	if (!(LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38)))
		return;
	if ((loc = OpenLocale(NULL)) != NULL) {
		for (i = 0; i < 4; i++)
			if (loc->loc_LanguageName &&
			    !strncmp((char *)loc->loc_LanguageName, names[i], strlen(names[i])))
				column = i + 1;
		CloseLocale(loc);
	}
	CloseLibrary((struct Library *)LocaleBase);
	LocaleBase = NULL;
}

/* the translation of english in the language chosen, or english itself */
const char *tr(const char *english)
{
	int lo = 0, hi = LANG_ROWS - 1;

	if (!english || !column)
		return english;
	while (lo <= hi) {
		int mid = (lo + hi) / 2, c = strcmp(english, lang_rows[mid][0]);

		if (!c)
			return lang_rows[mid][column] ? lang_rows[mid][column] : english;
		if (c < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return english;
}
