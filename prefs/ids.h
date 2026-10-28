/*
 * Panorama - ids.h: PanoramaPrefs' command IDs, a block per menu with a
 * fixed start, so adding an item never moves another menu's numbers
 * (CMD=n refers to them). New items go at the end of their block.
 */
#ifndef MP_IDS_H
#define MP_IDS_H

enum {
	/* Project */
	ID_SAVE = 100, ID_USE, ID_INSTALL, ID_ABOUT, ID_QUIT,
	/* Edit */
	ID_DEFAULTS = 200, ID_LASTSAVED, ID_RESTORE,
	/* Desktop */
	ID_OPEN = 300, ID_CLOSE, ID_REFRESH,
	/* View: the pages, then the look */
	ID_PAGE0 = 400, ID_PAGE1, ID_PAGE2, ID_PAGE3, ID_PAGE4,
	ID_DARK = 420,
	/* not menu items: the controls reporting */
	ID_LAYOUT = 900, ID_SIZE, ID_WB, ID_MAP, ID_PAGE, ID_ADDMODE, ID_REMOVEMODE
};
#endif
