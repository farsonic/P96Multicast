/*
 * Panorama - ids.h: PanoramaPrefs' command IDs, in blocks with fixed starts
 * (as RiptidePrefs' mui_ids.h) so adding a command never moves another
 * block (CMD=n refers to them). New commands go at the end of their block.
 */
#ifndef MP_IDS_H
#define MP_IDS_H

enum {
	/* Project menu */
	ID_SAVE = 100, ID_USE, ID_INSTALL, ID_ABOUT, ID_QUIT,
	/* Edit menu */
	ID_DEFAULTS = 200, ID_LASTSAVED, ID_RESTORE,
	/* Desktop menu */
	ID_OPEN = 300, ID_CLOSE, ID_REFRESH,
	/* Settings menu */
	ID_DARK = 400, ID_TIPS,
	/* not menu items: the window reporting */
	ID_CANCEL = 900, ID_PAGE, ID_LAYOUT, ID_SIZE, ID_WB, ID_MAP,
	/* the buttons of a message: ID_DLGBTN + button */
	ID_DLGBTN = 950,
	/* the screen mode list: chosen, Add, Replace, Delete */
	ID_MODESEL = 960, ID_MODEADD, ID_MODEREPL, ID_MODEDEL
};
#endif
