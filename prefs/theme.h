/*
 * Panorama - theme.h: PanoramaPrefs' look, which is RiptidePrefs' (Riptide's
 * src/amiga/prefs/mui_theme.h): every class draws through these functions
 * and never picks a colour or font itself.
 *
 * Colours are tokens (C_*, Riptide's T_* in the same order and values) with
 * one RGB value in a light and one in a dark table; theme_setup() obtains a
 * pen for each on the window's screen (ObtainBestPen, so palette screens
 * share or approximate them) and the last theme_cleanup() gives them back.
 * Fonts: the screen's font. Icons are drawn in code from short vector
 * descriptions on a 16 x 16 grid (theme_icon()).
 */
#ifndef MP_THEME_H
#define MP_THEME_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/classusr.h>

enum {
	C_WINDOW,       /* the window's background */
	C_PANEL,        /* the page list and the button bar */
	C_HEAD,         /* the page head */
	C_BORDER,       /* separator lines, button outlines */
	C_TEXT,
	C_DIM,          /* secondary text */
	C_DISABLED,
	C_ICON,         /* the ink of icons */
	C_HOVER,        /* under the pointer */
	C_PRESSED,      /* chosen, or held down */
	C_ACCENT,       /* the one thing that is on */
	C_WHITE,        /* paper inside icons */
	C_LIGHT,        /* light grey parts of icons */
	C_RED, C_GREEN, C_ORANGE, C_YELLOW,
	C_BUTTON,       /* button face */
	C_NPENS
};

/* the light or dark table: before the window opens, or between closing and reopening it */
void theme_set_dark(int dark);
int  theme_dark(void);
/* the compact layout for short screens (under 400 lines) */
void theme_set_compact(int on);
int  theme_compact(void);
/* help bubbles on or off (theme_bubble() returns NULL when off) */
void theme_set_tips(int on);
int  theme_tips(void);

/* pens for obj's screen: call in MUIM_Setup / MUIM_Cleanup of every class
 * (counts objects; MUI may send either twice) */
void theme_setup(Object *obj);
void theme_cleanup(Object *obj);
LONG tpen(int token);
/* "2:rrrrrrrr,gggggggg,bbbbbbbb", for MUIA_Background */
const char *theme_spec(int token);

struct TextFont *theme_font(void);
int  theme_fh(void);                   /* the font's height */

/* drawing; rectangles are inclusive, text is placed by its top edge and
 * passes through tr() */
LONG theme_textlen(struct RastPort *rp, const char *s);
void theme_text(struct RastPort *rp, LONG x, LONG y, const char *s, LONG pen, int bold);
/* text with the letter after '_' underlined (the '_' is not drawn) */
void theme_keytext(struct RastPort *rp, LONG x, LONG y, const char *s, LONG pen);
LONG theme_keytextlen(struct RastPort *rp, const char *s);
void theme_rect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);
void theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);
void theme_line(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen);

/* icons: one per page */
enum { IC_DESKTOP, IC_NETWORK, IC_DISPLAY, IC_WRAP, IC_STATUS, IC_NICONS };
/* icon at x,y, size pixels square; dim: everything but white in C_DISABLED */
void theme_icon(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, int dim);

/* "\33bName\33n\nbody wrapped at 46" in a static buffer (one bubble at a
 * time), or NULL with bubbles off */
const char *theme_bubble(const char *name, const char *body);
/* the same into buf, for texts that stay with an object */
void theme_bubble_to(char *buf, int len, const char *name, const char *body);

#endif
