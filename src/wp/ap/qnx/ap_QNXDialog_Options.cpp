/* AbiWord
 * Copyright (C) 1998 AbiSource, Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "ut_types.h"
#include "ut_string.h"
#include "ut_assert.h"
#include "ut_debugmsg.h"

#include "gr_QNXGraphics.h"

#include "xap_App.h"
#include "xap_QNXApp.h"
#include "xap_QNXFrame.h"
#include "xap_Prefs.h"

#include "ap_Dialog_Id.h"
#include "ap_Prefs_SchemeIds.h"

#include "ap_Strings.h"

#include "ap_QNXDialog_Options.h"
#include "ut_qnxHelper.h"

/*****************************************************************/
static int s_ok_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	dlg->event_OK(); 
	return Pt_CONTINUE;
}

static int s_cancel_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(widget && dlg); 
	dlg->event_Cancel(); 
	return Pt_CONTINUE;
}

static int s_apply_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(widget && dlg); 
	dlg->event_Apply(); 
	return Pt_CONTINUE;
}

static int s_delete_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	UT_DEBUGMSG(("AP_QNXDialog_Options::s_delete_clicked\n"));
	dlg->event_WindowDelete(); 
	return Pt_CONTINUE;
}


static int s_ignore_reset_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	dlg->event_IgnoreReset(); 
	return Pt_CONTINUE;
}

static int s_ignore_edit_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	dlg->event_IgnoreEdit(); 
	return Pt_CONTINUE;
}

static int s_dict_edit_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	dlg->event_DictionaryEdit(); 
	return Pt_CONTINUE;
}

static int s_defaults_clicked(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(widget && dlg); 
	dlg->event_SetDefaults(); 
	return Pt_CONTINUE;
}


// these function will allow multiple widget to tie into the same logic
// function (at the AP level) to enable/disable stuff
static int s_checkbutton_toggle(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{ 
#if 0
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;
	UT_ASSERT(dlg); 
	UT_ASSERT( w && GTK_IS_WIDGET(w));
	int i = (int) gtk_object_get_data( GTK_OBJECT(w), "tControl" );
	dlg->_enableDisableLogic( (AP_Dialog_Options::tControl) i );
#endif
	return Pt_CONTINUE;
}

static int s_menu_item_activate(PtWidget_t *widget, void *data, PtCallbackInfo_t *info)
{
#if 0
	AP_QNXDialog_Options * dlg = (AP_QNXDialog_Options *)data;

	UT_ASSERT(widget && dlg);

	PtWidget_t *option_menu = (PtWidget_t *)gtk_object_get_data(GTK_OBJECT(widget),
												 WIDGET_MENU_OPTION_PTR);
	UT_ASSERT( option_menu && GTK_IS_OPTION_MENU(option_menu));

	void * p = gtk_object_get_data( GTK_OBJECT(widget),
												WIDGET_MENU_VALUE_TAG);

	gtk_object_set_data( GTK_OBJECT(option_menu), WIDGET_MENU_VALUE_TAG, p );

	UT_DEBUGMSG(("s_menu_item_activate [%d %s]\n", p, UT_dimensionName( (UT_Dimension)((UT_uint32)p)) ) );
#endif
	return Pt_CONTINUE;
}

/*****************************************************************/

#define TR(str) UT_XML_transNoAmpersands((str)) 

XAP_Dialog * AP_QNXDialog_Options::static_constructor(XAP_DialogFactory * pFactory,
                                                         XAP_Dialog_Id id)
{
    AP_QNXDialog_Options * p = new AP_QNXDialog_Options(pFactory,id);
    return p;
}

AP_QNXDialog_Options::AP_QNXDialog_Options(XAP_DialogFactory * pDlgFactory,
                                                 XAP_Dialog_Id id)
    : AP_Dialog_Options(pDlgFactory,id)
{
	/* DEBUG stuff */
	XAP_Prefs *prefs = m_pApp->getPrefs();
	UT_ASSERT(prefs);
	UT_DEBUGMSG(("AP_QNXDialog_Options::AP_QNXDialog_Options[%s:%d]\n", __FILE__, __LINE__));
	UT_DEBUGMSG(("    current pref : %s\n",
		prefs->getCurrentScheme()->getSchemeName()) );

	UT_Bool b = prefs->savePrefsFile();
	UT_DEBUGMSG(("    prefs saved (%d) in %s\n", b, prefs->getPrefsPathname() ));

	UT_uint32 i;
	XAP_PrefsScheme *ps;
	for ( i = 0; (ps = prefs->getNthScheme(i)) != 0; i++ ) {
		UT_DEBUGMSG(("    %d [%s]\n", i, ps->getSchemeName() ));
	
		XML_Char const *pszKey, *pszValue;
		for ( UT_uint32 j = 0; ps->getNthValue(j, &pszKey, &pszValue ); j++ ) {
			UT_DEBUGMSG(("        %x %-30s : %s\n", j, pszKey, pszValue ));
		}
	}
}

AP_QNXDialog_Options::~AP_QNXDialog_Options(void)
{
}

/*****************************************************************/

void AP_QNXDialog_Options::runModal(XAP_Frame * pFrame)
{
	// To center the dialog, we need the frame of its parent.
	XAP_QNXFrame * pQNXFrame = (XAP_QNXFrame *)pFrame;
	UT_ASSERT(pQNXFrame);

    // Get the GtkWindow of the parent frame
    PtWidget_t * parentWindow = pQNXFrame->getTopLevelWindow();
    UT_ASSERT(parentWindow);

    // Build the window's widgets and arrange them
	PtSetParentWidget(parentWindow);
    PtWidget_t * mainWindow = _constructWindow();
    UT_ASSERT(mainWindow);

	// save for use with event
	m_pFrame = pFrame;

    // Populate the window's data items
    _populateWindowData();

    // Center our new dialog in its parent and make it a transient
    // so it won't get lost underneath
	UT_QNXCenterWindow(parentWindow, mainWindow);
	UT_QNXBlockWidget(parentWindow, 1);

	PtRealizeWidget(mainWindow);

one_more_time:
	int count = PtModalStart();
	done = 0;
	while(!done) {
		PtProcessEvent();
	}
	PtModalEnd(MODAL_END_ARG(count));

	switch ( m_answer ) {
	case AP_Dialog_Options::a_OK:
		_storeWindowData();
		break;

	case AP_Dialog_Options::a_APPLY:
		UT_DEBUGMSG(("Applying changes\n"));
		_storeWindowData();
		goto one_more_time;
		break;

	case AP_Dialog_Options::a_CANCEL:
		break;

	default:
		UT_ASSERT(UT_SHOULD_NOT_HAPPEN);
		break;
	};

	UT_QNXBlockWidget(parentWindow, 0);
	PtDestroyWidget(mainWindow);
}

void AP_QNXDialog_Options::event_OK(void)
{
	if (!done++) 
	    m_answer = AP_Dialog_Options::a_OK;
}

void AP_QNXDialog_Options::event_Cancel(void)
{
	if (!done++) 
	    m_answer = AP_Dialog_Options::a_CANCEL;
}

void AP_QNXDialog_Options::event_Apply(void)
{
	if (!done++)
	    m_answer = AP_Dialog_Options::a_APPLY;
}

void AP_QNXDialog_Options::event_WindowDelete(void)
{
	if (!done++)
	    m_answer = AP_Dialog_Options::a_CANCEL;    
}

//#define UNDERBAR((x)) void AP_QNXDialog_Options##(x)(void) { AP_Dialog_Options::_##(x); }

void AP_QNXDialog_Options::event_IgnoreEdit(void) { /* AP_Dialog_Options::_eventIgnoreEdit(); */ }
void AP_QNXDialog_Options::event_SetDefaults(void) { /* AP_Dialog_Options::_eventSetDefaults(); */ }
void AP_QNXDialog_Options::event_IgnoreReset(void) { /* AP_Dialog_Options::_eventIgnoreReset(); */ }
void AP_QNXDialog_Options::event_DictionaryEdit(void) { /* AP_Dialog_Options::_eventDictionaryEdit(); */ }

/*****************************************************************/
PtWidget_t* AP_QNXDialog_Options::_constructWindow ()
{
	// for the internationalization	
	const XAP_StringSet * pSS = m_pApp->getStringSet();
	
	PtWidget_t *windowOptions;
	PtWidget_t *table2;
	PtWidget_t *hbuttonbox2;
	PtWidget_t *buttonSave;
	PtWidget_t *buttonDefaults;
	PtWidget_t *buttonApply;
	PtWidget_t *buttonOk;
	PtWidget_t *buttonCancel;
	PtWidget_t *notebook1;
	PtWidget_t *tableSpell;
	PtWidget_t *checkbuttonSpellHideErrors;
	PtWidget_t *checkbuttonSpellSuggest;
	PtWidget_t *checkbuttonSpellMainOnly;
	PtWidget_t *checkbuttonSpellUppercase;
	PtWidget_t *checkbuttonSpellNumbers;
	PtWidget_t *checkbuttonSpellInternet;
	PtWidget_t *label4;
	PtWidget_t *checkbuttonSpellCheckAsType;
	PtWidget_t *buttonSpellIgnoreReset;
	PtWidget_t *buttonSpellIgnoreEdit;
	PtWidget_t *buttonSpellDictionary;
	PtWidget_t *label5;
	PtWidget_t *listSpellDicts;
	PtWidget_t *listSpellDicts_menu;
	PtWidget_t *glade_menuitem;
	PtWidget_t *labelSpell;
	PtWidget_t *tablePreferences;
	PtWidget_t *checkbuttonPrefsAutoSave;
	PtWidget_t *label6;
	PtWidget_t *comboPrefsSchemes;
	PtWidget_t *comboPrefsSchemesEdit;
	PtWidget_t *labelPreferences;
	PtWidget_t *hboxView;
	PtWidget_t *vbox4;
	PtWidget_t *frame2;
	PtWidget_t *vbox7;
	PtWidget_t *hbox10;
	PtWidget_t *checkbuttonViewRuler;
	PtWidget_t *labelUnits;
	PtWidget_t *listViewRulerUnit;
	PtWidget_t *listViewRulerUnit_menu;
	PtWidget_t *checkbuttonViewCursorBlink;
	PtWidget_t *checkbuttonViewToolbars;
	PtWidget_t *frameViewStuff;
	PtWidget_t *vbox6;
	PtWidget_t *checkbuttonViewAll;
	PtWidget_t *checkbuttonViewHidden;
	PtWidget_t *checkbuttonViewUnprintable;
	PtWidget_t *labelView;

	PtArg_t args[10];
	int		n;

#define WIN_WIDTH  100	
#define WIN_HEIGHT 200	
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_WINDOW_TITLE, pSS->getValue(AP_STRING_ID_DLG_Options_OptionsTitle), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, WIN_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, WIN_HEIGHT, 0);
	windowOptions = PtCreateWidget(PtWindow, NULL, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, WIN_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, WIN_HEIGHT, 0);
	PtWidget_t *vallgroup = PtCreateWidget(PtGroup, windowOptions, n, args);

	n = 0;
	PhPoint_t pos;
#define PANEL_WIDTH (WIN_WIDTH - 20)
#define PANEL_HEIGHT (WIN_HEIGHT - 50)
	pos.x = (WIN_WIDTH - PANEL_WIDTH) / 2;
	pos.y = (WIN_HEIGHT - PANEL_HEIGHT - 30) / 2;
	PtSetArg(&args[n++], Pt_ARG_POS, &pos, 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, 530, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, 380, 0);
	PtWidget_t *panelGroup = PtCreateWidget(PtPanelGroup, vallgroup, n, args);	

#define TAB_WIDTH  (PANEL_WIDTH - 30)
#define TAB_HEIGHT (PANEL_HEIGHT - 50)
	/*** Spelling Tab ***/
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TITLE,  pSS->getValue(AP_STRING_ID_DLG_Options_TabLabel_Spelling), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *spellingTab = PtCreateWidget(PtPane, panelGroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
//	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *vspellgroup = PtCreateWidget(PtGroup, spellingTab, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellHideErrors)), 0);
	checkbuttonSpellHideErrors = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellSuggest)), 0);
	checkbuttonSpellSuggest = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellMainOnly)), 0);
	checkbuttonSpellMainOnly = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellUppercase)), 0);
	checkbuttonSpellUppercase = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellNumbers)), 0);
	checkbuttonSpellNumbers = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellInternet)), 0);
	checkbuttonSpellInternet = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellCheckAsType)), 0);
	checkbuttonSpellCheckAsType = PtCreateWidget(PtToggleButton, vspellgroup, n, args);

	/* Align these items horizontally */
	n = 0;
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtWidget_t *hcustomdict = PtCreateWidget(PtGroup, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellCustomDict)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  3 * ABI_DEFAULT_BUTTON_WIDTH, 0);
	label5 = PtCreateWidget(PtLabel, hcustomdict, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	listSpellDicts = PtCreateWidget(PtComboBox, hcustomdict, n, args);
	const char *items1[] = { "custom.dic" };
	PtListAddItems(listSpellDicts, items1, 1, 0);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Btn_CustomDict)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonSpellDictionary = PtCreateWidget(PtButton, hcustomdict, n, args);

	/* Align these items horizontally */
	n = 0;
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtWidget_t *hignorewords = PtCreateWidget(PtGroup, vspellgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_SpellIgnoredWord)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  3 * ABI_DEFAULT_BUTTON_WIDTH, 0);
	label4 = PtCreateWidget(PtLabel, hignorewords, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Btn_IgnoreReset)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonSpellIgnoreReset = PtCreateWidget(PtButton, hignorewords, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Btn_IgnoreEdit)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonSpellIgnoreEdit = PtCreateWidget(PtButton, hignorewords, n, args);

	/* Preferences Tab */
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TITLE,  TR(pSS->getValue(AP_STRING_ID_DLG_Options_TabLabel_Preferences)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *prefTab = PtCreateWidget(PtPane, panelGroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
//	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *vprefgroup = PtCreateWidget(PtGroup, prefTab, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_PrefsAutoSave)), 0);
	checkbuttonPrefsAutoSave = PtCreateWidget(PtToggleButton, vprefgroup, n, args);

 	/** Group these together horizontally **/
	n = 0;
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtWidget_t *hprefscheme = PtCreateWidget(PtGroup, vprefgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue( AP_STRING_ID_DLG_Options_Label_PrefsCurrentScheme)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  3 * ABI_DEFAULT_BUTTON_WIDTH, 0);
	label6 = PtCreateWidget(PtLabel, hprefscheme, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	comboPrefsSchemes = PtCreateWidget(PtComboBox, hprefscheme, n, args);

	/*** View Tab ***/
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TITLE,  TR(pSS->getValue(AP_STRING_ID_DLG_Options_TabLabel_View)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *viewTab = PtCreateWidget(PtPane, panelGroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
//	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT, 0);
	PtWidget_t *vviewgroup = PtCreateWidget(PtGroup, viewTab, n, args);

	/** View View Show/Hide **/
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TITLE, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewShowHide)), 0);
	PtSetArg(&args[n++], Pt_ARG_CONTAINER_FLAGS, Pt_SHOW_TITLE | Pt_ETCH_TITLE_AREA, 
												 Pt_SHOW_TITLE | Pt_ETCH_TITLE_AREA);
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
//	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT / 2, 0);
	PtWidget_t *vshowgroup = PtCreateWidget (PtGroup, vviewgroup, n, args);

	n = 0;
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtWidget_t *hrulergroup = PtCreateWidget(PtGroup, vshowgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewRuler)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  2 * ABI_DEFAULT_BUTTON_WIDTH, 0);
	checkbuttonViewRuler = PtCreateWidget(PtToggleButton, hrulergroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewUnits)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	labelUnits = PtCreateWidget(PtLabel, hrulergroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	listViewRulerUnit = PtCreateWidget(PtComboBox, hrulergroup, n, args);
	const char *items2[] = { "inch", "mm", "cm", "twips", "points", "pico"  };
	PtListAddItems(listViewRulerUnit, items2, 6, 0);
	
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewToolbars)), 0);
	checkbuttonViewToolbars = PtCreateWidget(PtToggleButton, vshowgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewCursorBlink)), 0);
	checkbuttonViewCursorBlink = PtCreateWidget(PtToggleButton, vshowgroup, n, args);

	/** View View Frame **/
	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TITLE, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewViewFrame)), 0);
	PtSetArg(&args[n++], Pt_ARG_CONTAINER_FLAGS, Pt_SHOW_TITLE | Pt_ETCH_TITLE_AREA, 
												 Pt_SHOW_TITLE | Pt_ETCH_TITLE_AREA);
	PtSetArg(&args[n++], Pt_ARG_GROUP_ORIENTATION, Pt_GROUP_VERTICAL, 0);
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
//	PtSetArg(&args[n++], Pt_ARG_HEIGHT, TAB_HEIGHT / 2, 0);
	PtWidget_t *vviewviewgroup = PtCreateWidget (PtGroup, vviewgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewAll)), 0);
	checkbuttonViewAll = PtCreateWidget(PtToggleButton, vviewviewgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewHiddenText)), 0);
	checkbuttonViewHidden = PtCreateWidget(PtToggleButton, vviewviewgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Label_ViewUnprintable)), 0);
	checkbuttonViewUnprintable = PtCreateWidget(PtToggleButton, vviewviewgroup, n, args);

	/* Create the horizontal button group */
	n = 0;
//	PtSetArg(&args[n++], Pt_ARG_WIDTH, TAB_WIDTH, 0);
	PtSetArg(&args[n++], Pt_ARG_GROUP_SPACING_X, 5, 0);
	PtWidget_t *hbuttongroup = PtCreateWidget (PtGroup, vallgroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Btn_Save)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonSave = PtCreateWidget(PtButton, hbuttongroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Btn_Apply)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonApply = PtCreateWidget(PtButton, hbuttongroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(AP_STRING_ID_DLG_Options_Btn_Default)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonDefaults = PtCreateWidget(PtButton, hbuttongroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(XAP_STRING_ID_DLG_OK)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonOk = PtCreateWidget(PtButton, hbuttongroup, n, args);

	n = 0;
	PtSetArg(&args[n++], Pt_ARG_TEXT_STRING, TR(pSS->getValue(XAP_STRING_ID_DLG_Cancel)), 0);
	PtSetArg(&args[n++], Pt_ARG_WIDTH,  ABI_DEFAULT_BUTTON_WIDTH, 0);
	buttonCancel = PtCreateWidget(PtButton, hbuttongroup, n, args);

	PtAddCallback(windowOptions, Pt_CB_WINDOW_CLOSING, s_delete_clicked, this);
	PtAddCallback(buttonOk, Pt_CB_ACTIVATE, s_ok_clicked, this);
	PtAddCallback(buttonCancel, Pt_CB_ACTIVATE, s_cancel_clicked, this);
	PtAddCallback(buttonDefaults, Pt_CB_ACTIVATE, s_defaults_clicked, this);
	PtAddCallback(buttonApply, Pt_CB_ACTIVATE, s_apply_clicked, this);
	PtAddCallback(buttonSpellIgnoreEdit, Pt_CB_ACTIVATE, s_ignore_edit_clicked, this);
	PtAddCallback(buttonSpellIgnoreReset, Pt_CB_ACTIVATE, s_ignore_reset_clicked, this);
	PtAddCallback(buttonSpellDictionary, Pt_CB_ACTIVATE, s_dict_edit_clicked, this);

#if 0
	// to enable/disable other controls (hide errors)
	gtk_signal_connect(GTK_OBJECT(checkbuttonSpellCheckAsType),
						"toggled",
                       GTK_SIGNAL_FUNC(s_checkbutton_toggle),
                       (void *) this);
#endif

    m_windowMain = windowOptions;
	m_notebook = notebook1;

    m_checkbuttonSpellCheckAsType	= checkbuttonSpellCheckAsType;
    m_checkbuttonSpellHideErrors	= checkbuttonSpellHideErrors;
    m_checkbuttonSpellSuggest		= checkbuttonSpellSuggest;
    m_checkbuttonSpellMainOnly		= checkbuttonSpellMainOnly;
    m_checkbuttonSpellUppercase		= checkbuttonSpellUppercase;
    m_checkbuttonSpellNumbers		= checkbuttonSpellNumbers;
    m_checkbuttonSpellInternet		= checkbuttonSpellInternet;
	m_listSpellDicts				= listSpellDicts;
	m_listSpellDicts_menu			= listSpellDicts_menu;
	m_buttonSpellDictionary			= buttonSpellDictionary;
	m_buttonSpellIgnoreEdit			= buttonSpellIgnoreEdit;
	m_buttonSpellIgnoreReset		= buttonSpellIgnoreReset;

    m_checkbuttonPrefsAutoSave		= checkbuttonPrefsAutoSave;
	m_comboPrefsScheme				= comboPrefsSchemes;

    m_checkbuttonViewShowRuler		= checkbuttonViewRuler;
    m_listViewRulerUnits			= listViewRulerUnit;
    m_checkbuttonViewCursorBlink	= checkbuttonViewCursorBlink;
    m_checkbuttonViewShowToolbars	= checkbuttonViewToolbars;
    m_checkbuttonViewAll			= checkbuttonViewAll;
    m_checkbuttonViewHiddenText		= checkbuttonViewHidden;
    m_checkbuttonViewUnprintable	= checkbuttonViewUnprintable;

    m_buttonSave					= buttonSave;
    m_buttonDefaults				= buttonDefaults;
    m_buttonApply					= buttonApply;
    m_buttonOK						= buttonOk;
    m_buttonCancel					= buttonCancel;

    return windowOptions;
}

PtWidget_t *AP_QNXDialog_Options::_lookupWidget ( tControl id )
{
	switch (id)
	{
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// spell
	case id_CHECK_SPELL_CHECK_AS_TYPE:
		return m_checkbuttonSpellCheckAsType;
		break;

	case id_CHECK_SPELL_HIDE_ERRORS:
		return m_checkbuttonSpellHideErrors;
		break;

	case id_CHECK_SPELL_SUGGEST:
		return m_checkbuttonSpellSuggest;
		break;

	case id_CHECK_SPELL_MAIN_ONLY:
		return m_checkbuttonSpellMainOnly;
		break;

	case id_CHECK_SPELL_UPPERCASE:
		return m_checkbuttonSpellUppercase;
		break;

	case id_CHECK_SPELL_NUMBERS:
		return m_checkbuttonSpellNumbers;
		break;

	case id_CHECK_SPELL_INTERNET:
		return m_checkbuttonSpellInternet;
		break;

	case id_LIST_DICTIONARY:
		return m_listSpellDicts;
		break;

	case id_BUTTON_DICTIONARY_EDIT:
		return m_buttonSpellDictionary;
		break;

	case id_BUTTON_IGNORE_RESET:
		return m_buttonSpellIgnoreReset;
		break;

	case id_BUTTON_IGNORE_EDIT:
		return m_buttonSpellIgnoreEdit;
		break;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// prefs
	case id_CHECK_PREFS_AUTO_SAVE:
		return m_checkbuttonPrefsAutoSave;
		break;

	case id_COMBO_PREFS_SCHEME:
		return m_comboPrefsScheme;
		break;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
	// view
	case id_CHECK_VIEW_SHOW_RULER:
		return m_checkbuttonViewShowRuler;
		break;

	case id_LIST_VIEW_RULER_UNITS:
		return m_listViewRulerUnits;
		break;

	case id_CHECK_VIEW_CURSOR_BLINK:
		return m_checkbuttonViewCursorBlink;
		break;

	case id_CHECK_VIEW_SHOW_TOOLBARS:
		return m_checkbuttonViewShowToolbars;
		break;

	case id_CHECK_VIEW_ALL:
		return m_checkbuttonViewAll;
		break;

	case id_CHECK_VIEW_HIDDEN_TEXT:
		return m_checkbuttonViewHiddenText;
		break;

	case id_CHECK_VIEW_UNPRINTABLE:
		return m_checkbuttonViewUnprintable;
		break;

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// general
	case id_BUTTON_SAVE:
		return m_buttonSave;
		break;

	case id_BUTTON_DEFAULTS:
		return m_buttonDefaults;
		break;

	case id_BUTTON_OK:
		return m_buttonOK;
		break;

	case id_BUTTON_CANCEL:
		return m_buttonCancel;
		break;

	case id_BUTTON_APPLY:
		return m_buttonApply;
		break;

	default:
		UT_ASSERT("Unknown Widget");
		return 0;
		break;
	}
}

void AP_QNXDialog_Options::_controlEnable( tControl id, UT_Bool value )
{
	PtWidget_t *w = _lookupWidget(id);
	UT_ASSERT( w );

	if (!w) {
		printf("BAD CAN;t find %d widget \n", id);
		return;
	}

	PtArg_t arg;
	PtSetArg(&arg, Pt_ARG_FLAGS, 
				(value) ? (Pt_BLOCKED | Pt_GHOST) : 0,
				Pt_BLOCKED | Pt_GHOST);
	PtSetResources(w, 1, &arg);
}


#define DEFINE_GET_SET_BOOL(button) \
UT_Bool     AP_QNXDialog_Options::_gather##button(void) {       \
	UT_ASSERT(m_checkbutton##button);                           \
	PtArg_t arg;                                                \
	int *flags = NULL;                                          \
	PtSetArg(&arg, Pt_ARG_FLAGS, &flags, 0);                    \
	PtGetResources(m_checkbutton##button, 1, &arg);             \
	return ((flags) ? (*flags & Pt_SET) == Pt_SET : 0); }       \
void        AP_QNXDialog_Options::_set##button(UT_Bool b) {	    \
	UT_ASSERT(m_checkbutton##button);                           \
	PtArg_t arg;                                                \
	PtSetArg(&arg, Pt_ARG_FLAGS, (b) ? Pt_SET : 0, Pt_SET);     \
	PtSetResources(m_checkbutton##button, 1, &arg); }

DEFINE_GET_SET_BOOL(SpellCheckAsType);
DEFINE_GET_SET_BOOL(SpellHideErrors);
DEFINE_GET_SET_BOOL(SpellSuggest);
DEFINE_GET_SET_BOOL(SpellMainOnly);
DEFINE_GET_SET_BOOL(SpellUppercase);
DEFINE_GET_SET_BOOL(SpellNumbers);
DEFINE_GET_SET_BOOL(SpellInternet);

DEFINE_GET_SET_BOOL(PrefsAutoSave);

DEFINE_GET_SET_BOOL	(ViewShowRuler);

UT_Dimension AP_QNXDialog_Options::_gatherViewRulerUnits(void) 
{				
#if 0
	UT_ASSERT(m_listViewRulerUnits && GTK_IS_OPTION_MENU(m_listViewRulerUnits)); 
	return (UT_Dimension)((gint)gtk_object_get_data( GTK_OBJECT(m_listViewRulerUnits), WIDGET_MENU_VALUE_TAG )); 
#endif
}			

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
// This function will lookup a option box by the value stored in the 
//	user data under the key WIDGET_MENU_VALUE_TAG
//
typedef struct {
	int index;
	int found;
	char *key;
	void *data;
} search_data;

static void search_for_value ( PtWidget_t *widget, void * _value )
{
#if 0
	search_data *value = (search_data *)_value;

	if ( !GTK_IS_MENU_ITEM(widget))
		return;

	value->index++;

	gint v = (gint) gtk_object_get_data( GTK_OBJECT(widget), value->key );
	if ( v == (gint)value->data )
	{
		// UT_DEBUGMSG(("search_for_value [%d]", (gint) value->data ));
		value->found = value->index;
	}
#endif
}

// returns -1 if not found
int option_menu_set_by_key ( PtWidget_t *option_menu, void * value, char *key )
{
#if 0
	UT_ASSERT( option_menu && key && GTK_IS_OPTION_MENU(option_menu));

	// lookup for the key with the value of dim
	search_data data = { -1, -1, key, value };

	PtWidget_t *menu = gtk_option_menu_get_menu( GTK_OPTION_MENU(option_menu));
	UT_ASSERT(menu&&GTK_IS_MENU(menu));

	// iterate through all the values
	gtk_container_forall ( GTK_CONTAINER(menu), search_for_value, (void *) &data );

	// if we found a value that matches, then say select it
	if ( data.found >= 0 )
	{
		gtk_option_menu_set_history( GTK_OPTION_MENU(option_menu), data.found );
		//UT_DEBUGMSG(("search found %d\n", data.found ));
	}
	else
		UT_DEBUGMSG(("%s:%f search NOT found (searched %d indexes)\n", __FILE__, __LINE__, data.index ));

	return data.found;
#endif
	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

void    AP_QNXDialog_Options::_setViewRulerUnits(UT_Dimension dim) 
{	
#if 0
	UT_ASSERT(m_listViewRulerUnits && GTK_IS_OPTION_MENU(m_listViewRulerUnits)); 

	int r = option_menu_set_by_key ( m_listViewRulerUnits, (void *)dim, WIDGET_MENU_VALUE_TAG ); 
	UT_ASSERT( r != -1 );
#endif
}

DEFINE_GET_SET_BOOL	(ViewCursorBlink);
DEFINE_GET_SET_BOOL	(ViewShowToolbars);

DEFINE_GET_SET_BOOL	(ViewAll);
DEFINE_GET_SET_BOOL	(ViewHiddenText);
DEFINE_GET_SET_BOOL	(ViewUnprintable);

#undef DEFINE_GET_SET_BOOL

int AP_QNXDialog_Options::_gatherNotebookPageNum(void) 
{				
#if 0
	UT_ASSERT(m_notebook && GTK_IS_NOTEBOOK(m_notebook)); 
	return gtk_notebook_get_current_page( GTK_NOTEBOOK(m_notebook) ); 
#endif
}			

void    AP_QNXDialog_Options::_setNotebookPageNum(int pn) 
{	
#if 0
	UT_ASSERT(m_notebook && GTK_IS_NOTEBOOK(m_notebook)); 
	gtk_notebook_set_page( GTK_NOTEBOOK(m_notebook), pn ); 
#endif
}

