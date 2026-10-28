/*
 * Panorama - lang.h: every string the user sees goes through tr(), so the
 * programs can be translated without touching the code. Strings are
 * written in English; lang_table.h holds the translations.
 */
#ifndef MP_LANG_H
#define MP_LANG_H

void lang_init(void);              /* the language from locale.library's default locale */
const char *tr(const char *english);   /* its translation, or itself */
#endif
