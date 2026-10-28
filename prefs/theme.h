/*
 * Panorama - theme.h: PanoramaPrefs' look, as Easel's: a dark palette
 * obtained as pens on the window's screen, flat drawing, and icons drawn in
 * code from short vector descriptions (none copied from other programs).
 */
#ifndef MP_THEME_H
#define MP_THEME_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/classusr.h>

/* the colours */
enum {
	C_BACK,        /* the window */
	C_PANEL,       /* the navigation pane, the button bar */
	C_FIELD,       /* inputs, choices, switches off */
	C_BORDER,      /* their outlines */
	C_HOVER,       /* a pane row the pointer is on */
	C_TEXT,
	C_DIM,         /* help text, units */
	C_DISABLED,
	C_ICON,
	C_ACCENT,      /* the page chosen, switches on, the main button */
	C_ACCENTHOVER,
	C_ONACCENT,    /* text on the accent */
	C_MONITOR,     /* the monitors in the picture */
	C_MONITOR2,
	C_WB,          /* the Workbench in the picture */
	C_RED, C_GREEN, C_ORANGE,
	C_NPENS
};

void theme_setup(Object *obj);      /* from MUIM_Setup: the pens on its screen (counted) */
void theme_cleanup(Object *obj);    /* from MUIM_Cleanup: given back with the last */
LONG tpen(int which);
const char *theme_back(void);       /* the window's colour as an MUI image spec */
const char *theme_panel_back(void); /* the pane's and the button bar's */

/* drawing: text with its top at y (bold: the soft style), outlines, fills (nothing if empty) */
LONG theme_textlen(struct RastPort *rp, const char *s, BOOL bold);
void theme_text(struct RastPort *rp, LONG x, LONG y, const char *s, LONG pen, BOOL bold);
void theme_rect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);
void theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);
/* a filled box with its corners cut by r pixels: the switches, the main button */
void theme_round(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG r, LONG pen);

/* icons, drawn in a size x size square at x, y */
enum {
	IC_NONE, IC_DESKTOP, IC_NETWORK, IC_DISPLAY, IC_WRAP, IC_STATUS, IC_SAVE, IC_USE,
	IC_INSTALL, IC_OPEN, IC_CLOSE, IC_ADD, IC_REMOVE, IC_LEFT, IC_RIGHT, IC_REFRESH,
	IC_N
};
void theme_icon(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, LONG pen);

/* a help bubble: the name in bold, then the text wrapped (a static buffer) */
const char *theme_bubble(const char *name, const char *body);
#endif
