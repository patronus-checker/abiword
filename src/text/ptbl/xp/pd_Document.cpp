/* -*- mode: C++; tab-width: 4; c-basic-offset: 4; -*- */

/* AbiWord
 * Copyright (C) 1998 AbiSource, Inc.
 * Copyright (c) 2001,2002,2003 Tomas Frydrych
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ut_types.h"
#include "ut_string.h"
#include "ut_debugmsg.h"
#include "ut_assert.h"
#include "ut_bytebuf.h"
#include "ut_base64.h"
#include "ut_misc.h"
#include "ut_rand.h"
#include "ut_uuid.h"
#include "pd_Document.h"
#include "xad_Document.h"
#include "xap_Strings.h"
#include "pt_PieceTable.h"
#include "pl_Listener.h"
#include "ie_imp.h"
#include "ie_exp.h"
#include "pf_Frag_Strux.h"
#include "pp_Property.h"
#include "pd_Style.h"
#include "pf_Frag_Object.h"
#include "pf_Frag_FmtMark.h"
#include "px_CR_Span.h"
#include "px_CR_SpanChange.h"
#include "px_CR_Strux.h"
#include "pf_Frag.h"
#include "pd_Iterator.h"
#include "fd_Field.h"
#include "po_Bookmark.h"
#include "fl_AutoNum.h"
#include "xap_Frame.h"
#include "xap_App.h"
#include "xap_Prefs.h"
#include "ap_Prefs.h"
#include "ut_units.h"
#include "ut_string_class.h"
#include "ut_sleep.h"
#include "ut_path.h"
#include "ut_locale.h"

// these are needed because of the exportGetVisDirectionAtPosition() mechanism
#include "fp_Run.h"
#include "fl_BlockLayout.h"
#include "fl_DocListener.h"
#include "fl_DocLayout.h"

// our currently used DTD
#define ABIWORD_FILEFORMAT_VERSION "1.1"

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

struct _dataItemPair
{
	UT_ByteBuf* pBuf;
	const void*	pToken;
};

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

// perhaps this should be a magic "unknown" or "NULL" value,
// but now we just depend on save() never being called without
// a previous saveAs() (which specifies a type)
PD_Document::PD_Document(XAP_App *pApp)
	: AD_Document(),
	  m_docPageSize("A4"),
	  m_ballowListUpdates(false),
	  m_pPieceTable(0),
	  m_hashDataItems(11),
	  m_lastOpenedType(IEFT_Bogus), // used to be: IE_Imp::fileTypeForSuffix(".abw"))
	  m_lastSavedAsType(IEFT_Bogus), // used to be: IE_Exp::fileTypeForSuffix(".abw")
	  m_bDoingPaste(false),
	  m_bAllowInsertPointChange(true),
	  m_bRedrawHappenning(false),
	  m_bLoading(false),
	  m_bLockedStyles(false),        // same as lockStyles(false)
	  m_indexAP(0xffffffff),
	  m_bDontImmediatelyLayout(false),
	  m_iLastDirMarker(0),
	  m_pVDBl(NULL),
	  m_pVDRun(NULL),
	  m_iVDLastPos(0xffffffff),
	  m_iNewHdrHeight(0), 
	  m_iNewFtrHeight(0),
	  m_bMarginChangeOnly(false)
{
	m_pApp = pApp;
	
	XAP_App::getApp()->getPrefs()->getPrefsValueBool(AP_PREF_KEY_LockStyles,&m_bLockedStyles);

#ifdef PT_TEST
	m_pDoc = this;
#endif
}

PD_Document::~PD_Document()
{
	if (m_pPieceTable)
		delete m_pPieceTable;

	_destroyDataItemData();

	UT_VECTOR_PURGEALL(fl_AutoNum*, m_vecLists);
	// remove the meta data
	m_metaDataMap.purgeData();
	//UT_HASH_PURGEDATA(UT_UTF8String*, &m_metaDataMap, delete) ;
	m_mailMergeMap.purgeData();
	//UT_HASH_PURGEDATA(UT_UTF8String*, &m_mailMergeMap, delete) ;

	// we do not purge the contents of m_vecListeners
	// since these are not owned by us.

	// TODO: delete the key/data pairs
}


//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

void PD_Document::setMetaDataProp ( const UT_String & key,
									const UT_UTF8String & value )
{
	UT_UTF8String * old = m_metaDataMap.pick ( key );
	DELETEP(old);
	
	UT_UTF8String * ptrvalue = new UT_UTF8String(value);
	m_metaDataMap.set (key, ptrvalue);
}

bool PD_Document::getMetaDataProp (const UT_String & key, UT_UTF8String & outProp) const
{
  bool found = false;
  outProp = "";

  const UT_UTF8String * val = m_metaDataMap.pick (key);
  found = (val != NULL);

  if (val && val->size ()) outProp = *val;

  return found;
}

UT_UTF8String PD_Document::getMailMergeField(const UT_String & key) const
{
  const UT_UTF8String * val = m_mailMergeMap.pick ( key );
  if (val)
    return *val;
  return "";
}

bool PD_Document::mailMergeFieldExists(const UT_String & key) const
{
    const UT_UTF8String * val = m_mailMergeMap.pick ( key );
    return (val != NULL);
}

void PD_Document::setMailMergeField(const UT_String & key,
									const UT_UTF8String & value)
{
	UT_UTF8String * old = m_mailMergeMap.pick ( key );
	DELETEP(old);

	UT_UTF8String * ptrvalue = new UT_UTF8String ( value ) ;
	m_mailMergeMap.set ( key, ptrvalue ) ;
}

void PD_Document::clearMailMergeMap()
{
	m_mailMergeMap.clear();
}

void PD_Document::setMarginChangeOnly(bool b)
{
	m_bMarginChangeOnly = b;
}

bool PD_Document::isMarginChangeOnly(void) const
{
	return m_bMarginChangeOnly;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

static void buildTemplateList(UT_String *template_list, const UT_String & base)
{
	UT_LocaleInfo locale(UT_LocaleInfo::system());

	UT_UTF8String lang (locale.getLanguage());
	UT_UTF8String terr (locale.getTerritory());

	/* try *6* combinations of the form:
	   1) /templates/normal.awt-en_US
	   2) /templates/normal.awt-en
	   3) /templates/normal.awt
	   4) /templates/normal.awt-en_US
	   5) /templates/normal.awt-en
	   6) /templates/normal.awt
	*/

	UT_String user_template_base (XAP_App::getApp()->getUserPrivateDirectory());
#if defined(WIN32)
	user_template_base += UT_String_sprintf("\\templates\\%s", base.c_str());
#else
	user_template_base += UT_String_sprintf("/templates/%s", base.c_str());
#endif
	UT_String global_template_base (XAP_App::getApp()->getAbiSuiteLibDir());
#if defined(WIN32)
	global_template_base += UT_String_sprintf("\\templates\\%s", base.c_str());
#else
	global_template_base += UT_String_sprintf("/templates/%s", base.c_str());
#endif

	template_list[0] = user_template_base; // always try to load user's normal.awt first
	template_list[1] = UT_String_sprintf ("%s-%s_%s", user_template_base.c_str(), lang.utf8_str(), terr.utf8_str());
	template_list[2] = UT_String_sprintf ("%s-%s", user_template_base.c_str(), lang.utf8_str());

	if (!XAP_App::getApp()->findAbiSuiteLibFile(template_list[5],base.c_str(),"templates"))
		template_list[5] = global_template_base; // always try to load global normal.awt last

	UT_String xbase = base;

	xbase += "-";
	xbase += lang.utf8_str();

	if (!XAP_App::getApp()->findAbiSuiteLibFile(template_list[4],xbase.c_str(),"templates"))
		template_list[4] = UT_String_sprintf ("%s-%s", global_template_base.c_str(), lang.utf8_str());

	xbase += "_";
	xbase += terr.utf8_str();

	if (!XAP_App::getApp()->findAbiSuiteLibFile(template_list[3],xbase.c_str(),"templates"))
		template_list[3] = UT_String_sprintf ("%s-%s_%s", global_template_base.c_str(), lang.utf8_str(), terr.utf8_str());
}

UT_Error PD_Document::importFile(const char * szFilename, int ieft,
								 bool markClean, bool bImportStylesFirst,
								 const char* impProps)
{
	if (!szFilename || !*szFilename)
	{
		UT_DEBUGMSG(("PD_Document::importFile -- invalid filename\n"));
		return UT_INVALIDFILENAME;
	}

	m_pPieceTable = new pt_PieceTable(this);
	if (!m_pPieceTable)
	{
		UT_DEBUGMSG(("PD_Document::importFile -- could not construct piece table\n"));
		return UT_NOPIECETABLE;
	}

	m_bLoading = true;
	m_pPieceTable->setPieceTableState(PTS_Loading);

	if (bImportStylesFirst) {
		UT_String template_list[6];

		buildTemplateList (template_list, "normal.awt");

		bool success = false;
		for (UT_uint32 i = 0; i < 6 && !success; i++)
			success = (importStyles(template_list[i].c_str(), ieft, true) == UT_OK);

		// don't worry if this fails
	}

	IE_Imp * pie = NULL;
	UT_Error errorCode;

	IEFileType savedAsType;
	errorCode = IE_Imp::constructImporter(this, szFilename, static_cast<IEFileType>(ieft), &pie, &savedAsType);
	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::importFile -- could not construct importer\n"));
		DELETEP(m_pPieceTable);
		return errorCode;
	}
	if (impProps && strlen(impProps))
		pie->setProps (impProps);

	// set standard document properties and attributes, such as dtd,
	// lang, dom-dir, etc., which the importer can then overwite this
	// also initializes m_indexAP
	m_indexAP = 0xffffffff;
	setAttrProp(NULL);
	
	errorCode = pie->importFile(szFilename);
	delete pie;
	m_bLoading = false;

	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::importFile -- could not import file\n"));
		DELETEP(m_pPieceTable);
		return errorCode;
	}

	setLastOpenedTime(time(NULL));
	
	// get document-wide settings from the AP
	const PP_AttrProp * pAP = getAttrProp();
	
	if(pAP)
	{
		const XML_Char * pA = NULL;

		// TODO this should probably be stored as an attribute of the
		// styles section rather then the whole doc ...
		if(pAP->getAttribute("styles", pA))
		{
			m_bLockedStyles = !(strcmp(pA, "locked"));
		}

		if(pAP->getAttribute("xid-max", pA))
		{
			UT_uint32 i = (UT_uint32)atoi(pA);
			m_pPieceTable->setXIDThreshold(i);
		}
	}

	m_pPieceTable->setPieceTableState(PTS_Editing);
	updateFields();

	if(markClean)
		_setClean();
	else
	  	_setForceDirty(true); // force this to be dirty

	if (strstr(szFilename, "normal.awt") == NULL)
		XAP_App::getApp()->getPrefs()->addRecent(szFilename);


	// show warning if document contains revisions and they are hidden
	// from view ...
	XAP_Frame * pFrame = XAP_App::getApp()->getLastFocussedFrame();

	bool bHidden = (isMarkRevisions() && (getHighestRevisionId() <= getShowRevisionId()));
	bHidden |= (!isMarkRevisions() && !isShowRevisions());

	if(pFrame && bHidden)
	{
		pFrame->showMessageBox(AP_STRING_ID_MSG_HiddenRevisions, 
						       XAP_Dialog_MessageBox::b_O, 
							   XAP_Dialog_MessageBox::a_OK);
	}
	
	return UT_OK;
}

UT_Error PD_Document::createRawDocument(void)
{
	m_pPieceTable = new pt_PieceTable(this);
	if (!m_pPieceTable)
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- could not construct piece table\n"));
		return UT_NOPIECETABLE;
	}

	m_pPieceTable->setPieceTableState(PTS_Loading);

	{
		UT_String template_list[6];
		
		buildTemplateList (template_list, "normal.awt");

		bool success = false;
		int ieft = IEFT_Unknown;
		for (UT_uint32 i = 0; i < 6 && !success; i++)
			success = (importStyles(template_list[i].c_str(), ieft, true) == UT_OK);

		// don't worry if this fails
	}

	// set standard document properties and attributes, such as dtd, lang,
	// dom-dir, etc., which the importer can then overwite
	// this also initializes m_indexAP
	m_indexAP = 0xffffffff;
	setAttrProp(NULL);
	return UT_OK;
}

void PD_Document::finishRawCreation(void)
{
	m_pPieceTable->setPieceTableState(PTS_Editing);
	updateFields();
	_setClean();							// mark the document as not-dirty
}

UT_Error PD_Document::readFromFile(const char * szFilename, int ieft,
								   const char * impProps)
{
	if (!szFilename || !*szFilename)
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- invalid filename\n"));
		return UT_INVALIDFILENAME;
	}

	if ( !UT_isRegularFile(szFilename) )
	{
	  UT_DEBUGMSG (("PD_Document::readFromFile -- file (%s) is not plain file\n",szFilename));
	  return UT_INVALIDFILENAME;
	}

	m_pPieceTable = new pt_PieceTable(this);
	if (!m_pPieceTable)
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- could not construct piece table\n"));
		return UT_NOPIECETABLE;
	}

	m_pPieceTable->setPieceTableState(PTS_Loading);

	{
		UT_String template_list[6];
		
		buildTemplateList (template_list, "normal.awt");

		bool success = false;
		for (UT_uint32 i = 0; i < 6 && !success; i++)
			success = (importStyles(template_list[i].c_str(), ieft, true) == UT_OK);

		// don't worry if this fails
	}

	IE_Imp * pie = NULL;
	UT_Error errorCode;

	errorCode = IE_Imp::constructImporter(this, szFilename, static_cast<IEFileType>(ieft), &pie, &m_lastOpenedType);
	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- could not construct importer\n"));
		return errorCode;
	}
	if (impProps && strlen(impProps))
		pie->setProps (impProps);

	_syncFileTypes(false);

	// set standard document properties and attributes, such as dtd, lang,
	// dom-dir, etc., which the importer can then overwite
	// this also initializes m_indexAP
	m_indexAP = 0xffffffff;
	setAttrProp(NULL);

	errorCode = pie->importFile(szFilename);
	delete pie;

	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- could not import file\n"));
		return errorCode;
	}

	UT_ASSERT_HARMLESS(!getFilename());
	_setFilename(UT_strdup(szFilename));
	
	if (!getFilename())
	{
		UT_DEBUGMSG(("PD_Document::readFromFile -- no memory\n"));
		return UT_IE_NOMEMORY;
	}

	setLastOpenedTime(time(NULL));
	
	// get document-wide settings from the AP
	const PP_AttrProp * pAP = getAttrProp();
	
	if(pAP)
	{
		const XML_Char * pA = NULL;

		if(pAP->getAttribute("styles", pA))
		{
			m_bLockedStyles = !(strcmp(pA, "locked"));
		}

		if(pAP->getAttribute("xid-max", pA))
		{
			UT_uint32 i = (UT_uint32)atoi(pA);
			m_pPieceTable->setXIDThreshold(i);
		}
	}

	m_pPieceTable->setPieceTableState(PTS_Editing);
	updateFields();
	_setClean();							// mark the document as not-dirty

	if (strstr(szFilename, "normal.awt") == NULL)
		XAP_App::getApp()->getPrefs()->addRecent(szFilename);

	// show warning if document contains revisions and they are hidden
	// from view ...
	XAP_Frame * pFrame = XAP_App::getApp()->getLastFocussedFrame();

	bool bHidden = (isMarkRevisions() && (getHighestRevisionId() <= getShowRevisionId()));
	bHidden |= (!isMarkRevisions() && !isShowRevisions());

	if(pFrame && bHidden)
	{
		pFrame->showMessageBox(AP_STRING_ID_MSG_HiddenRevisions, 
						       XAP_Dialog_MessageBox::b_O, 
							   XAP_Dialog_MessageBox::a_OK);
	}

	return UT_OK;
}

UT_Error PD_Document::importStyles(const char * szFilename, int ieft, bool bDocProps)
{
	if (!szFilename || !*szFilename)
	{
		UT_DEBUGMSG(("PD_Document::importStyles -- invalid filename\n"));
		return UT_INVALIDFILENAME;
	}

	if ( !UT_isRegularFile(szFilename) )
	{
	  UT_DEBUGMSG (("PD_Document::importStyles -- file is not plain file\n"));
	  return UT_INVALIDFILENAME;
	}

	if (!m_pPieceTable)
	{
		UT_DEBUGMSG(("PD_Document::importStyles -- could not construct piece table\n"));
		return UT_NOPIECETABLE;
	}

	IE_Imp * pie = NULL;
	UT_Error errorCode;

	errorCode = IE_Imp::constructImporter(this, szFilename, static_cast<IEFileType>(ieft), &pie);
	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::importStyles -- could not construct importer\n"));
		return errorCode;
	}

	if(!pie->supportsLoadStylesOnly())
	{
		UT_DEBUGMSG(("PD_Document::importStyles -- import of styles-only not supported\n"));
		return UT_IE_IMPSTYLEUNSUPPORTED;
	}
	
	pie->setLoadStylesOnly(true);
	pie->setLoadDocProps(bDocProps);
	errorCode = pie->importFile(szFilename);
	delete pie;

	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::importStyles -- could not import file\n"));
		return errorCode;
	}

	// need to update anything that uses styles ...
	//
	// this is rather cumbersome, but did not see a simpler way of
	// doing this (perhaps we should consider some way of invalidating
	// styles: a style could carry a time stamp and each element would
	// also carry a timestamp reflecting when its atributes were last
	// refreshed; in this case if style stamp > element stamp, element
	// would reformat) Tomas, June 7, 2003
	
	UT_GenericVector<PD_Style*> vStyles;
	getAllUsedStyles(&vStyles);
	for(UT_uint32 i = 0; i < vStyles.getItemCount();i++)
	{
		PD_Style * pStyle = vStyles.getNthItem(i);

		if(pStyle)
			updateDocForStyleChange(pStyle->getName(),!pStyle->isCharStyle());
	}

	return UT_OK;	
}

UT_Error PD_Document::newDocument(void)
{
	UT_String template_list[6];

	buildTemplateList(template_list, "normal.awt");

	bool success = false;

	for (UT_uint32 i = 0; i < 6 && !success; i++)
		success = (importFile (template_list[i].c_str(), IEFT_Unknown, true, false) == UT_OK);

	if (!success) {
			m_pPieceTable = new pt_PieceTable(this);
			if (!m_pPieceTable)
				return UT_NOPIECETABLE;

			m_pPieceTable->setPieceTableState(PTS_Loading);

			// add just enough structure to empty document so we can edit
			appendStrux(PTX_Section,NULL);
			appendStrux(PTX_Block, NULL);

			// set standard document properties, such as dtd, lang,
			// dom-dir, etc. (some of the code that used to be here is
			// now in the setAttrProp() function, since it is shared
			// both by new documents and documents being loaded from disk
			// this also initializes m_indexAP
			m_indexAP = 0xffffffff;
			setAttrProp(NULL);

			m_pPieceTable->setPieceTableState(PTS_Editing);
	}

	setDocVersion(0);
	setEditTime(0);
	setLastOpenedTime(time(NULL));

	if(!getDocUUID())
	{
		UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
	}
	else
		_getDocUUID()->makeUUID();
	
	// mark the document as not-dirty
	_setClean();

	return UT_OK;
}

UT_Error PD_Document::_saveAs(const char * szFilename, int ieft,
							 const char * expProps)
{
  return _saveAs(szFilename, ieft, true, expProps);
}

UT_Error PD_Document::_saveAs(const char * szFilename, int ieft, bool cpy,
							 const char * expProps)
{
	if (!szFilename)
		return UT_SAVE_NAMEERROR;

	IE_Exp * pie = NULL;
	UT_Error errorCode;
	IEFileType newFileType;

	errorCode = IE_Exp::constructExporter(this, szFilename, static_cast<IEFileType>(ieft), &pie, &newFileType);
	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::Save -- could not construct exporter\n"));
		return UT_SAVE_EXPORTERROR;
	}
	if (expProps && strlen(expProps))
		pie->setProps (expProps);

	if (cpy)
	{
		m_lastSavedAsType = newFileType;
		_syncFileTypes(true);
	}

	// order of these calls matters
	_adjustHistoryOnSave();

	// see if revisions table is still needed ...
	purgeRevisionTable();
	
	errorCode = pie->writeFile(szFilename);
	delete pie;

	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::Save -- could not write file\n"));
		return (errorCode == UT_SAVE_CANCELLED) ? UT_SAVE_CANCELLED : UT_SAVE_WRITEERROR;
	}

	if (cpy) // we want to make the current settings persistent
	{
	    // no file name currently set - make this filename the filename
	    // stored for the doc
	    char * szFilenameCopy = NULL;
	    if (!UT_cloneString(szFilenameCopy,szFilename))
			return UT_SAVE_OTHERERROR;
	    _setFilename(szFilenameCopy);
	    _setClean(); // only mark as clean if we're saving under a new name
		signalListeners(PD_SIGNAL_DOCNAME_CHANGED);	
	}

	//if (strstr(szFilename, "normal.awt") == NULL)
	XAP_App::getApp()->getPrefs()->addRecent(szFilename);
	return UT_OK;
}

UT_Error PD_Document::_save(void)
{
	if (!getFilename() || !*getFilename())
		return UT_SAVE_NAMEERROR;
	if (m_lastSavedAsType == IEFT_Unknown)
		return UT_EXTENSIONERROR;

	IE_Exp * pie = NULL;
	UT_Error errorCode;

	errorCode = IE_Exp::constructExporter(this,getFilename(),m_lastSavedAsType,&pie);
	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::Save -- could not construct exporter\n"));
		return UT_SAVE_EXPORTERROR;
	}

	_syncFileTypes(true);

	_adjustHistoryOnSave();

	// see if revisions table is still needed ...
	purgeRevisionTable();
	
	errorCode = pie->writeFile(getFilename());
	delete pie;

	if (errorCode)
	{
		UT_DEBUGMSG(("PD_Document::Save -- could not write file\n"));
		return (errorCode == UT_SAVE_CANCELLED) ? UT_SAVE_CANCELLED : UT_SAVE_WRITEERROR;
	}

	_setClean();
	return UT_OK;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

bool PD_Document::isDirty(void) const
{
	return m_pPieceTable->isDirty() || isForcedDirty();
}

void PD_Document::_setClean(void)
{
	m_pPieceTable->setClean();
	m_pPieceTable->getFragments().cleanFrags();
	_setForceDirty(false);
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

bool	PD_Document::insertObject(PT_DocPosition dpos,
								  PTObjectType pto,
								  const XML_Char ** attributes,
								  const XML_Char ** properties)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->insertObject(dpos, pto, attributes, properties);
}

bool	PD_Document::insertObject(PT_DocPosition dpos,
								  PTObjectType pto,
								  const XML_Char ** attributes,
								  const XML_Char ** properties, fd_Field ** pField)
{
	if(isDoingTheDo())
	{
		return false;
	}
	pf_Frag_Object * pfo = NULL;
	bool bres =  m_pPieceTable->insertObject(dpos, pto, attributes, properties, &pfo);
	*pField = pfo->getField();
	return bres;
}

bool PD_Document::insertSpan(PT_DocPosition dpos,
							 const UT_UCSChar * pbuf,
							 UT_uint32 length,
							 PP_AttrProp *p_AttrProp)
{
	if(isDoingTheDo())
	{
		return false;
	}
	if(p_AttrProp)
	{
		m_pPieceTable->insertFmtMark(PTC_AddFmt, dpos, p_AttrProp);
	}
#if DEBUG
#if 0
	UT_uint32 ii = 0;
	UT_String sStr;
	for(ii=0; ii<length;ii++)
	{
		sStr += static_cast<const char>(pbuf[ii]);
	}
	UT_DEBUGMSG(("Insert span %s \n",sStr.c_str()));
#endif
#endif
	// REMOVE UNDESIRABLE CHARACTERS ...
	// we will remove all LRO, RLO, LRE, RLE, and PDF characters
	// * at the moment we do not handle LRE/RLE
	// * we replace LRO/RLO with our dir-override property

	PT_DocPosition cur_pos = dpos;
	PP_AttrProp AP;

	// we want to reset m_iLastDirMarker (which is in a state left
	// over from the last insert/append operation)
	m_iLastDirMarker = 0;
	
	bool result = true;
	const UT_UCS4Char * pStart = pbuf;

	for(const UT_UCS4Char * p = pbuf; p < pbuf + length; p++)
	{
		switch(*p)
		{
			case UCS_LRO:
				if((p - pStart) > 0)
				{
					result &= m_pPieceTable->insertSpan(cur_pos, pStart, p - pStart);
					cur_pos += p - pStart;
				}
				
				AP.setProperty("dir-override", "ltr");
				result &= m_pPieceTable->insertFmtMark(PTC_AddFmt, cur_pos, &AP);
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_RLO:
				if((p - pStart) > 0)
				{
					result &= m_pPieceTable->insertSpan(cur_pos, pStart, p - pStart);
					cur_pos += p - pStart;
				}
				
				AP.setProperty("dir-override", "rtl");
				result &= m_pPieceTable->insertFmtMark(PTC_AddFmt, cur_pos, &AP);
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_PDF:
				if((p - pStart) > 0)
				{
					result &= m_pPieceTable->insertSpan(cur_pos, pStart, p - pStart);
					cur_pos += p - pStart;
				}
				
				if((m_iLastDirMarker == UCS_RLO) || (m_iLastDirMarker == UCS_LRO))
				{
					AP.setProperty("dir-override", "");
					result &= m_pPieceTable->insertFmtMark(PTC_RemoveFmt, cur_pos, &AP);
				}

				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_LRE:
			case UCS_RLE:
				if((p - pStart) > 0)
				{
					result &= m_pPieceTable->insertSpan(cur_pos, pStart, p - pStart);
					cur_pos += p - pStart;
				}
				
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
		}
	}
	
	result &= m_pPieceTable->insertSpan(cur_pos, pStart, length - (pStart - pbuf));
	return result;
}

bool PD_Document::deleteSpan(PT_DocPosition dpos1,
							 PT_DocPosition dpos2,
							 PP_AttrProp *p_AttrProp_Before,
							 UT_uint32 &iRealDeleteCount,
							 bool bDeleteTableStruxes)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->deleteSpanWithTable(dpos1, dpos2, p_AttrProp_Before,iRealDeleteCount, bDeleteTableStruxes );
}

bool PD_Document::changeSpanFmt(PTChangeFmt ptc,
								PT_DocPosition dpos1,
								PT_DocPosition dpos2,
								const XML_Char ** attributes,
								const XML_Char ** properties)
{
	if(isDoingTheDo())
	{
		return false;
	}
	bool f;
	deferNotifications();
	f = m_pPieceTable->changeSpanFmt(ptc,dpos1,dpos2,attributes,properties);
	processDeferredNotifications();
	return f;
}



bool PD_Document::insertStrux(PT_DocPosition dpos,
							  PTStruxType pts, pf_Frag_Strux ** ppfs_ret)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->insertStrux(dpos,pts,ppfs_ret);
}


bool PD_Document::insertStrux(PT_DocPosition dpos,
							  PTStruxType pts,
							  const XML_Char ** attributes,
							  const XML_Char ** properties, pf_Frag_Strux ** ppfs_ret)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->insertStrux(dpos,pts, attributes,properties,ppfs_ret);
}


/*!
 * This method deletes the HdrFtr strux pointed to by sdh
 */
void PD_Document::deleteHdrFtrStrux(PL_StruxDocHandle sdh)
{
	pf_Frag_Strux * pfs_hdrftr = const_cast<pf_Frag_Strux *>(static_cast<const pf_Frag_Strux *>(sdh));
	UT_return_if_fail (pfs_hdrftr->getType()  == pf_Frag::PFT_Strux);
	m_pPieceTable->deleteHdrFtrStrux(pfs_hdrftr);
}

bool PD_Document::changeStruxFmt(PTChangeFmt ptc,
								 PT_DocPosition dpos1,
								 PT_DocPosition dpos2,
								 const XML_Char ** attributes,
								 const XML_Char ** properties,
								 PTStruxType pts)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->changeStruxFmt(ptc,dpos1,dpos2,attributes,properties,pts);
}


bool PD_Document::changeStruxFmtNoUndo(PTChangeFmt ptc,
								 PL_StruxDocHandle sdh,
								 const XML_Char ** attributes,
								 const XML_Char ** properties)
{
	pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(const_cast<void *>(sdh));
	UT_return_val_if_fail (pfs->getType() == pf_Frag::PFT_Strux,false);
	return m_pPieceTable->changeStruxFmtNoUndo(ptc,pfs,attributes,properties);
}


/*!
 * This method changes *all* the strux fragments in the specified region.
 */
bool PD_Document::changeStruxFmt(PTChangeFmt ptc,
								 PT_DocPosition dpos1,
								 PT_DocPosition dpos2,
								 const XML_Char ** attributes,
								 const XML_Char ** properties)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->changeStruxFmt(ptc,dpos1,dpos2,attributes,properties);
}

/*!
 * This Method is used to change just the parentID of each strux in a list
 * without updating the fl_Layouts.
 */
bool PD_Document::changeStruxForLists(PL_StruxDocHandle sdh, const char * pszParentID)
{
	return m_pPieceTable->changeStruxForLists(sdh, pszParentID);
}

bool PD_Document::insertFmtMark(PTChangeFmt ptc, PT_DocPosition dpos, PP_AttrProp *p_AttrProp)
{
	if(isDoingTheDo())
	{
		return false;
	}
	return m_pPieceTable->insertFmtMark(ptc, dpos, p_AttrProp);
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

bool PD_Document::appendStrux(PTStruxType pts, const XML_Char ** attributes, pf_Frag_Strux ** ppfs_ret)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document
//
// Update frames during load.
//
	XAP_Frame * pFrame = m_pApp->getLastFocussedFrame();
	if(pFrame)
		pFrame->nullUpdate();
	return m_pPieceTable->appendStrux(pts,attributes,ppfs_ret);
}

/*!
    appends given fmt to the last strux in document
*/
bool PD_Document::appendLastStruxFmt(PTStruxType pts, const XML_Char ** attributes, const XML_Char ** props,
									 bool bSkipEmbededSections)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	return m_pPieceTable->appendLastStruxFmt(pts,attributes,props,bSkipEmbededSections);
}

bool PD_Document::appendLastStruxFmt(PTStruxType pts, const XML_Char ** attributes, const XML_Char * props,
									 bool bSkipEmbededSections)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	return m_pPieceTable->appendLastStruxFmt(pts,attributes,props,bSkipEmbededSections);
}

bool PD_Document::changeLastStruxFmtNoUndo(PT_DocPosition dpos, PTStruxType pts,
									 const XML_Char ** attributes, const XML_Char ** props,
									 bool bSkipEmbededSections)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	return m_pPieceTable->changeLastStruxFmtNoUndo(dpos, pts,attributes,props,bSkipEmbededSections);
}

bool PD_Document::changeLastStruxFmtNoUndo(PT_DocPosition dpos, PTStruxType pts,
										   const XML_Char ** attributes, const XML_Char * props,
									 bool bSkipEmbededSections)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	return m_pPieceTable->changeLastStruxFmtNoUndo(dpos, pts,attributes,props,bSkipEmbededSections);
}

bool PD_Document::appendStruxFmt(pf_Frag_Strux * pfs, const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	return m_pPieceTable->appendStruxFmt(pfs,attributes);
}

bool PD_Document::appendFmt(const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document
	return m_pPieceTable->appendFmt(attributes);
}

bool PD_Document::appendFmt(const UT_GenericVector<XML_Char*> * pVecAttributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->appendFmt(pVecAttributes);
}

bool PD_Document::appendSpan(const UT_UCSChar * pbuf, UT_uint32 length)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	// REMOVE UNDESIRABLE CHARACTERS ...
	// we will remove all LRO, RLO, LRE, RLE, and PDF characters
	// * at the moment we do not handle LRE/RLE
	// * we replace LRO/RLO with our dir-override property

	const XML_Char * attrs[] = {"props", NULL, NULL};
	UT_String s;
			
	bool result = true;
	const UT_UCS4Char * pStart = pbuf;

	for(const UT_UCS4Char * p = pbuf; p < pbuf + length; p++)
	{
		switch(*p)
		{
			case UCS_LRO:
				if((p - pStart) > 0)
					result &= m_pPieceTable->appendSpan(pStart,p - pStart);

				s = "dir-override:ltr";
				attrs[1] = s.c_str();
				result &= m_pPieceTable->appendFmt(&attrs[0]);
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_RLO:
				if((p - pStart) > 0)
					result &= m_pPieceTable->appendSpan(pStart,p - pStart);

				s = "dir-override:rtl";
				attrs[1] = s.c_str();
				result &= m_pPieceTable->appendFmt(&attrs[0]);
				
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_PDF:
				if((p - pStart) > 0)
					result &= m_pPieceTable->appendSpan(pStart,p - pStart);

				if((m_iLastDirMarker == UCS_RLO) || (m_iLastDirMarker == UCS_LRO))
				{
					s = "dir-override:";
					attrs[1] = s.c_str();
					result &= m_pPieceTable->appendFmt(&attrs[0]);
				}
				
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_LRE:
			case UCS_RLE:
				if((p - pStart) > 0)
					result &= m_pPieceTable->appendSpan(pStart,p - pStart);

				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
		}
	}

	if(length - (pStart-pbuf))
		result &= m_pPieceTable->appendSpan(pStart,length - (pStart-pbuf));
	return result;
}

bool PD_Document::appendObject(PTObjectType pto, const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->appendObject(pto,attributes);
}

bool PD_Document::appendFmtMark(void)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->appendFmtMark();
}

/*!
 * This method returns the value associated with attribute szAttribute
 * at picetable strux given by sdh.
 * NB: attributes and props are view-specific because of revision attributes
 * 
 \param  PL_StruxDocHandle sdh (pf_Frag_Strux) where we want to find the value
 \param  bool bShowRevisions -- revisions setting for the view (FV_View::isShowRevisions())
 \param  UT_uint32 iRevisionLevel -- the revision level of the view (FV_View::getRevisionLevel())
 \param const char * szAttribute the attribute we're looking for.
 \param const char ** pszValue the value of the attribute.
 \returns true if the attribute was present at the sdh

 Don't FREEP *pszRetValue!!!
*/
bool PD_Document::getAttributeFromSDH(PL_StruxDocHandle sdh, bool bShowRevisions, UT_uint32 iRevisionLevel,
									  const char * szAttribute, const char ** pszRetValue)
{
	const pf_Frag_Strux * pfStrux = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex indexAP = pfStrux->getIndexAP();
	const PP_AttrProp * pAP = NULL;
	const XML_Char * pszValue = NULL;

	bool bHiddenRevision = false;
	getAttrProp(indexAP, &pAP,NULL,bShowRevisions,iRevisionLevel,bHiddenRevision);
	
	UT_return_val_if_fail (pAP, false);
	(pAP)->getAttribute(szAttribute, pszValue);
	if(pszValue == NULL)
	{
		*pszRetValue = NULL;
		return false;
	}
	*pszRetValue = pszValue;
	return true;
}

/*!
 * Get API fromthe supplied StruxDocHandle
 *
 * NB: this method does not take into account revisions settings; you either have to further process
 *     the AP at the index using the explodeRevisions() methods or you can retrieve specific props
 *     and attrs using getPropertyFromSDH() and getAttributeFromSDH().
 */
PT_AttrPropIndex PD_Document::getAPIFromSDH( PL_StruxDocHandle sdh)
{
	const pf_Frag_Strux * pfStrux = static_cast<const pf_Frag_Strux *>(sdh);
	return pfStrux->getIndexAP();
}

/*!
 * This method returns the value associated with attribute szProperty
 * at picetable strux given by sdh.
 * NB: attributes and props are view-specific because of revision attributes
 * 
 \param  PL_StruxDocHandle sdh (pf_Frag_Strux) where we want to find the value
 \param  bool bShowRevisions -- revisions setting for the view (FV_View::isShowRevisions())
 \param  UT_uint32 iRevisionLevel -- the revision level of the view (FV_View::getRevisionLevel())
 \param const char * szProperty the Property we're looking for.
 \param const char ** pszValue the value of the property.
 \returns true if the property was present at the sdh

 Don't FREEP *pszRetValue!!!
*/
bool PD_Document::getPropertyFromSDH(PL_StruxDocHandle sdh, bool bShowRevisions, UT_uint32 iRevisionLevel,
									 const char * szProperty, const char ** pszRetValue)
{
	const pf_Frag_Strux * pfStrux = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex indexAP = pfStrux->getIndexAP();
	const PP_AttrProp * pAP = NULL;
	const XML_Char * pszValue = NULL;

	bool bHiddenRevision = false;

	getAttrProp(indexAP, &pAP,NULL,bShowRevisions,iRevisionLevel,bHiddenRevision);
	
	UT_return_val_if_fail (pAP, false);
	(pAP)->getProperty(szProperty, pszValue);
	
	if(pszValue == NULL)
	{
		*pszRetValue = NULL;
		return false;
	}
	*pszRetValue = pszValue;
	return true;
}

/*!
 * This medthod modifies the attributes of a section strux without
 * generating a change record. Use with extreme care!!
 */
bool  PD_Document::changeStruxAttsNoUpdate(PL_StruxDocHandle sdh, const char * attr, const char * attvalue)
{
	pf_Frag_Strux * pfStrux = const_cast<pf_Frag_Strux *>(static_cast<const pf_Frag_Strux *>(sdh));
	UT_return_val_if_fail (pfStrux, false);
	return m_pPieceTable->changeSectionAttsNoUpdate(pfStrux, attr, attvalue);
}

/*!
 * This method inserts a strux of type pts immediately before the sdh given.
 * Attributes of the strux can be optionally passed. This method does not throw
 * a change record and should only be used under exceptional circumstances to 
 * repair the piecetable during loading. It was necessary to import RTF tables.
 */
bool PD_Document::insertStruxNoUpdateBefore(PL_StruxDocHandle sdh, PTStruxType pts,const XML_Char ** attributes )
{
#if 0
	pf_Frag_Strux * pfStrux = const_cast<pf_Frag_Strux *>(static_cast<const pf_Frag_Strux *>(sdh));
	T_ASSERT(pfStrux->getStruxType() != PTX_Section);
#endif
	return m_pPieceTable->insertStruxNoUpdateBefore(sdh, pts, attributes );
}

/*!
 * This method examines the frag immediately before the given sdh and decides
 * if it matches the strux type given.
 */
bool PD_Document::isStruxBeforeThis(PL_StruxDocHandle sdh,  PTStruxType pts)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	pf_Frag * pfb = pfs->getPrev();
	if(pfb->getType() != pf_Frag::PFT_Strux)
		return false;
	pf_Frag_Strux * pfsb = static_cast<pf_Frag_Strux *>(pfb);
	if(pfsb->getStruxType() == pts)
		return true;
	return false;
}

/*!
 * This method deletes a strux without throwing a change record.
 * sdh is the StruxDocHandle that gets deleted..
 * Use with extreme care. Should only be used for document import.
 */
bool PD_Document::deleteStruxNoUpdate(PL_StruxDocHandle sdh)
{
	return m_pPieceTable->deleteStruxNoUpdate(sdh);
}

/*!
 * This method returns the last pf_Frag_Strux as a PL_StruxDocHandle before the end of the piecetable.
 */
PL_StruxDocHandle  PD_Document::getLastSectionSDH(void)
{
	const pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	const pf_Frag_Strux * pfSecLast = NULL;
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
		     const pf_Frag_Strux * pfSec = static_cast<const pf_Frag_Strux *>(currentFrag);
		     if(pfSec->getStruxType() == PTX_Section)
		     {
				 pfSecLast = pfSec;
			 }
		}
		currentFrag = currentFrag->getNext();
	}
	return reinterpret_cast<PL_StruxDocHandle>(pfSecLast);
}


/*!
 * This method returns the last pf_Frag_Strux as a PL_StruxDocHandle 
 * before the end of the piecetable.
 */
PL_StruxDocHandle  PD_Document::getLastStruxOfType(PTStruxType pts )
{
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getLast();
	pf_Frag_Strux * pfSecLast = NULL;
	bool bFound = false;
	UT_sint32 nest = 0;
	if(pts == PTX_SectionTable)
		nest = 1;
	while (!bFound && currentFrag!=m_pPieceTable->getFragments().getFirst())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
		     pf_Frag_Strux * pfSec = static_cast<pf_Frag_Strux *>(currentFrag);
			 if(pts != PTX_EndTable)
			 { 
				 if(pfSec->getStruxType() == PTX_EndTable)
					 nest++;
				 if(pfSec->getStruxType() == PTX_SectionTable)
 					 nest--;
			 }
		     if((pfSec->getStruxType() == pts) && (nest == 0))
		     {
				 pfSecLast = pfSec;
				 bFound = true;
			 }
		}
		currentFrag = currentFrag->getPrev();
	}
	return reinterpret_cast<PL_StruxDocHandle *>(pfSecLast);
}


/*!
 * This method scans the document to check that the id of a header/footer
 *  section actually exists in a section somewhere in the document.
 */
bool PD_Document::verifySectionID(const XML_Char * pszId)
{
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		PT_AttrPropIndex indexAP = 0;
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
		     pf_Frag_Strux * pfSec = static_cast<pf_Frag_Strux *>(currentFrag);
		     if(pfSec->getStruxType() == PTX_Section)
		     {
				 indexAP = static_cast<pf_Frag_Text *>(currentFrag)->getIndexAP();
				 const PP_AttrProp * pAP = NULL;
				 m_pPieceTable->getAttrProp(indexAP,&pAP);
				 UT_return_val_if_fail (pAP,false);
				 const XML_Char * pszIDName = NULL;
				 (pAP)->getAttribute("header", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("header-first", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("header-last", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("header-even", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("footer", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("footer-first", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("footer-last", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;
				 (pAP)->getAttribute("footer-even", pszIDName);
				 if(pszIDName && strcmp(pszIDName,pszId) == 0)
					 return true;

				 // the id could also be hidden in a revision attribute ...
				 const XML_Char * pszRevisionAttr = NULL;
				 
				 if((pAP)->getAttribute("revision", pszRevisionAttr))
				 {
					 PP_RevisionAttr RA(pszRevisionAttr);

					 for(UT_uint32 i = 0; i < RA.getRevisionsCount(); ++i)
					 {
						 const PP_Revision * pRev = RA.getNthRevision(i);
						 if(!pRev)
						 {
							 UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
							 continue;
						 }

						 (pRev)->getAttribute("header", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("header-first", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("header-last", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("header-even", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("footer", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("footer-first", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("footer-last", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
						 (pRev)->getAttribute("footer-even", pszIDName);
						 if(pszIDName && strcmp(pszIDName,pszId) == 0)
							 return true;
					 }
				 }
		     }
		}
//
// Get Next frag in the table.
//
		currentFrag = currentFrag->getNext();
	}
	return false;
}



/*!
 * This method scans the document to look for a HdrFtr strux.
\params const char * pszHdrFtr The particular attribute that identifies the
                               strux as "header" "footer" "header-even" etc.
\params const char * pszHdrFtrID the unique string to match with Docsection.
\returns a PL_StruxDocHandle of the matching frag or NULL if none found.
 */
PL_StruxDocHandle PD_Document::findHdrFtrStrux(const XML_Char * pszHdrFtr,
											const XML_Char * pszHdrFtrID)
{
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		PT_AttrPropIndex indexAP = 0;
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
		     pf_Frag_Strux * pfSec = static_cast<pf_Frag_Strux *>(currentFrag);
		     if(pfSec->getStruxType() == PTX_SectionHdrFtr)
		     {
				 indexAP = pfSec->getIndexAP();
				 const PP_AttrProp * pAP = NULL;
				 m_pPieceTable->getAttrProp(indexAP,&pAP);
				 UT_return_val_if_fail (pAP,false);
				 const XML_Char * pszIDName = NULL;
				 const XML_Char * pszHeaderName = NULL;
				 (pAP)->getAttribute(PT_TYPE_ATTRIBUTE_NAME, pszHeaderName);
				 (pAP)->getAttribute(PT_ID_ATTRIBUTE_NAME, pszIDName);
				 if(pszIDName && pszHeaderName && (strcmp(pszIDName,pszHdrFtrID) == 0) && (strcmp(pszHeaderName,pszHdrFtr) == 0))
					 return static_cast<PL_StruxDocHandle>(pfSec) ;
			 }
		}
//
// Get Next frag in the table.
//
		currentFrag = currentFrag->getNext();
	}
	return NULL;
}


/*!
 * This method returns the offset to a an embedded strux 
 * And a pointer to the embedded strux found.
 * If no emebedded strux is found in the block we return -1 ans NULL
 */ 
UT_sint32 PD_Document::getEmbeddedOffset(PL_StruxDocHandle sdh, PT_DocPosition posoff, PL_StruxDocHandle & sdhEmbedded)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	UT_return_val_if_fail (pfs->getStruxType() == PTX_Block,-1);
	const pf_Frag * pf = static_cast<const pf_Frag *>(pfs);
	pf = pf->getNext();
	PT_DocPosition pos = m_pPieceTable->getStruxPosition(sdh) + posoff;
	while(pf && m_pPieceTable->getFragPosition(pf) + pf->getLength() <= pos)
	{
		pf = pf->getNext();
	}
	if(pf == NULL)
	{
		sdhEmbedded = NULL;
		return -1;
	}
	while(pf && pf->getType() != pf_Frag::PFT_Strux)
	{
		pf = pf ->getNext();
	}
	if(pf == NULL)
	{
		sdhEmbedded = NULL;
		return -1;
	}
	if(!m_pPieceTable->isFootnote(const_cast<pf_Frag *>(pf)))
    {
		sdhEmbedded = NULL;
		return -1;
	}
	const pf_Frag_Strux * pfsNew = static_cast<const pf_Frag_Strux *>(pf);
	pos  = m_pPieceTable->getFragPosition(pf);
	UT_sint32 diff = static_cast<UT_sint32>(pos) - static_cast<UT_sint32>(m_pPieceTable->getFragPosition(static_cast<pf_Frag *>(const_cast<pf_Frag_Strux *>(pfs))));
	sdhEmbedded = static_cast<PL_StruxDocHandle>(pfsNew);
	return diff;
}

/*!
 * This method returns true if there is a Footnote strux at exactly this 
 * position.
 */
bool PD_Document::isFootnoteAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	bool b = m_pPieceTable->isFootnote(pf);
	if(b)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionTOC)
		{
			return false;
		}
	}
	return b;
}


/*!
 * This method returns true if there is a TOC or endTOC strux at exactly this 
 * position.
 */
bool PD_Document::isTOCAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	bool b = m_pPieceTable->isFootnote(pf);
	if(b)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionTOC)
		{
			return true;
		}
	}
	b = m_pPieceTable->isEndFootnote(pf);
	if(b)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_EndTOC)
		{
			return true;
		}
	}
	return false;
}


/*!
 * This method returns true if there is an EndFootnote strux at exactly this 
 * position.
 */
bool PD_Document::isEndFootnoteAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	bool b = m_pPieceTable->isEndFootnote(pf);
	if(b)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_EndTOC)
		{
			return false;
		}
	}
	return b;
}


/*!
 * This method returns true if there is a frame strux at exactly this 
 * position.
 */
bool PD_Document::isFrameAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionFrame)
		{
			return true;
		}
	}
	return false;
}



/*!
 * This method returns true if there is an endFrame strux at exactly this 
 * position.
 */
bool PD_Document::isEndFrameAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_EndFrame)
		{
			return true;
		}
	}
	return false;
}


/*!
 * This method returns true if there is a HdrFtr strux at exactly this 
 * position.
 */
bool PD_Document::isHdrFtrAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionHdrFtr)
		{
			return true;
		}
	}
	return false;
}



/*!
 * This method returns true if there is a Section strux at exactly this 
 * position.
 */
bool PD_Document::isSectionAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_Section)
		{
			return true;
		}
	}
	return false;
}


//============================================================================
// Table Medthods
//===========================================================================

/*!
 * This method returns true if there is a table strux at exactly this 
 * position.
 */
bool PD_Document::isTableAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionTable)
		{
			return true;
		}
	}
	return false;
}


/*!
 * This method returns true if there is an EndTable strux at exactly this 
 * position.
 */
bool PD_Document::isEndTableAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_EndTable)
		{
			return true;
		}
	}
	return false;
}


/*!
 * This method returns true if there is a cell strux at exactly this 
 * position.
 */
bool PD_Document::isCellAtPos(PT_DocPosition pos)
{
	PT_BlockOffset pOffset;
	pf_Frag * pf = NULL;
	/*bool bRes = */m_pPieceTable->getFragFromPosition(pos,&pf,&pOffset);
	while(pf->getLength() == 0)
		pf = pf->getPrev();
	if(pf->getType() == pf_Frag::PFT_Strux)
	{
		pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);
		if(pfs->getStruxType() == PTX_SectionCell)
		{
			return true;
		}
	}
	return false;
}

/*!
 * This method returns the end table strux associated with the table strux tableSDH
 * Returns NULL on failure to find it.
 */
PL_StruxDocHandle PD_Document::getEndTableStruxFromTableSDH(PL_StruxDocHandle tableSDH)
{
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(tableSDH);
	currentFrag = currentFrag->getNext();
	PL_StruxDocHandle EndTableSDH = NULL;
	UT_sint32 depth =0;
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfSec = static_cast<const pf_Frag_Strux *>(currentFrag);
			if(pfSec->getStruxType() == PTX_SectionTable)
				depth++;
			else if(pfSec->getStruxType() == PTX_EndTable)
			{
				if(depth == 0)
				{
					EndTableSDH = static_cast<PL_StruxDocHandle>(pfSec);
					return EndTableSDH;
				}
				else
					depth--;
			}
		}
//
// Get Next frag in the table.
//
		currentFrag = currentFrag->getNext();
	}
	return NULL;
}
/*!
 * This method returns the end cell strux associated with the cell strux cellSDH
 * Returns NULL on failure to find it.
 */
PL_StruxDocHandle PD_Document::getEndCellStruxFromCellSDH(PL_StruxDocHandle cellSDH)
{
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(cellSDH);
	currentFrag = currentFrag->getNext();
	PL_StruxDocHandle EndCellSDH = NULL;
	while (currentFrag && currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfSec = static_cast<const pf_Frag_Strux *>(currentFrag);
			if(pfSec->getStruxType() == PTX_SectionTable)
			{
				PL_StruxDocHandle endTab = getEndTableStruxFromTableSDH(static_cast<PL_StruxDocHandle >(pfSec));
				currentFrag = static_cast<const pf_Frag *>(endTab);
			}
			else if(pfSec->getStruxType() == PTX_EndCell )
			{
				EndCellSDH = static_cast<PL_StruxDocHandle>(pfSec);
				return EndCellSDH;
			}
			else if(pfSec->getStruxType() == PTX_SectionCell)
			{
				return NULL;
			}
			else if(pfSec->getStruxType() == PTX_EndTable)
			{
				return NULL;
			}
		}
//
// Get Next frag in the table.
//
		if(currentFrag)
		{
			currentFrag = currentFrag->getNext();
		}
	}
	return NULL;
}

/*!
 * This method returns the end table strux associated with the table strux tableSDH
 * Returns NULL on failure to find it.
 */
PL_StruxDocHandle PD_Document::getEndTableStruxFromTablePos(PT_DocPosition tablePos)
{
	PL_StruxDocHandle tableSDH = NULL;
	PL_StruxDocHandle EndTableSDH = NULL;
	bool bRes =	getStruxOfTypeFromPosition(tablePos, PTX_SectionTable, &tableSDH);
	if(!bRes)
		return NULL;
	EndTableSDH = getEndTableStruxFromTableSDH(tableSDH);
	return EndTableSDH;
}

/*!
 * The method returns the number of rows and columns in table pointed to by tableSDH
\params PL_StruxDocHandle tableSDH SDH of the table in question
\params UT_sint32 * numRows pointer to the number of rows returned
\params UT_sint32 * numCols pointer to the number of cols returned
*/
bool PD_Document::getRowsColsFromTableSDH(PL_StruxDocHandle tableSDH, bool bShowRevisions, UT_uint32 iRevisionLevel,
										  UT_sint32 * numRows, UT_sint32 * numCols)
{
	UT_sint32 iRight, iBot;
	const char * szRight = NULL;
	const char * szBot = NULL;
	PL_StruxDocHandle cellSDH;
	*numRows = 0;
	*numCols = 0;
//
// Do the scan
//
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(tableSDH);
	currentFrag = currentFrag->getNext();
	while (currentFrag && currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfSec = static_cast<const pf_Frag_Strux *>(currentFrag);
			if(pfSec->getStruxType() == PTX_SectionTable)
			{
//
// skip to the end of this nested table
//
				PL_StruxDocHandle endSDH = getEndTableStruxFromTableSDH(static_cast<PL_StruxDocHandle>(pfSec) );
				pfSec = static_cast<const pf_Frag_Strux *>(endSDH);
			}
			else if(pfSec->getStruxType() == PTX_EndTable)
				return true;
			else if(pfSec->getStruxType() == PTX_SectionCell)
			{
				cellSDH = static_cast<PL_StruxDocHandle>(pfSec);
				bool bres = getPropertyFromSDH(cellSDH,bShowRevisions, iRevisionLevel,"right-attach",&szRight);
				if(szRight && *szRight)
					iRight = atoi(szRight);
				bres = getPropertyFromSDH(cellSDH,bShowRevisions, iRevisionLevel,"bot-attach",&szBot);
				if(szBot && *szBot)
					iBot = atoi(szBot);

				if(*numCols < iRight)
					*numCols = iRight;
				if(*numRows < iBot)
					*numRows = iBot;
			}
			currentFrag = static_cast<const pf_Frag *>(pfSec);
		}
		if(currentFrag)
			currentFrag = currentFrag->getNext();
	}
	return false;
}

void  PD_Document::miniDump(PL_StruxDocHandle sdh, UT_sint32 nstruxes)
{
#ifdef DEBUG
	UT_sint32 i=0;
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	const pf_Frag * pf = static_cast<const pf_Frag *>(pfs);
	for(i=0; pfs && (i< nstruxes); i++)
	{
		pf = pf->getPrev();
		while(pf && pf->getType() != pf_Frag::PFT_Strux)
			pf = pf->getPrev();
		pfs = static_cast<const pf_Frag_Strux *>(pf);
	}
	if(pfs == NULL)
	{
		pf =  m_pPieceTable->getFragments().getFirst();
		while(pf && (pf->getType() != pf_Frag::PFT_Strux))
			pf = pf->getNext();
		if(pf)
			pfs = static_cast<const pf_Frag_Strux *>(pfs);
	}
	for(i=0; pfs && (i< 2*nstruxes); i++)
	{
		pf = static_cast<const pf_Frag *>(pfs);
		pfs = static_cast<const pf_Frag_Strux *>(pf);
		PL_StruxDocHandle sdhTemp = static_cast<PL_StruxDocHandle>(pfs);
		const char * szStrux = NULL;
		if(pfs->getStruxType() == PTX_Block)
			szStrux = "Block";
		else if(pfs->getStruxType() == PTX_SectionTable)
			szStrux = "Table";
		else if(pfs->getStruxType() == PTX_SectionCell)
			szStrux = "Cell";
		else if(pfs->getStruxType() == PTX_EndTable)
			szStrux = "End Table";
		else if(pfs->getStruxType() == PTX_EndCell)
			szStrux = "End Cell";
		else if(pfs->getStruxType() == PTX_SectionFootnote)
			szStrux = "Footnote";
		else if(pfs->getStruxType() == PTX_EndFootnote)
			szStrux = "End Footnote";
		else if(pfs->getStruxType() == PTX_SectionEndnote)
			szStrux = "Endnote";
		else if(pfs->getStruxType() == PTX_EndEndnote)
			szStrux = "End Endnote";
		else if(pfs->getStruxType() == PTX_Section)
			szStrux = "Section";
		else if(pfs->getStruxType() == PTX_SectionFrame)
			szStrux = "Frame";
		else if(pfs->getStruxType() == PTX_EndFrame)
			szStrux = "EndFrame";
		else
			szStrux = "Other Strux";
		if(i< nstruxes)
		{
			UT_DEBUGMSG(("MiniDump Before Frag %x Type %s \n",pfs,szStrux));
		}
		else if(i > nstruxes)
		{
			UT_DEBUGMSG(("MiniDump After Frag %x Type %s \n",pfs,szStrux));
		}
		if(pfs == static_cast<const pf_Frag_Strux *>(sdh))
		{
			UT_DEBUGMSG(("MiniDump Actual Frag %x Type %s \n",pfs,szStrux));
		}
		const char * szLeft=NULL;
		const char * szRight=NULL;
		const char * szTop=NULL;
		const char * szBot = NULL;
		getPropertyFromSDH(sdhTemp,true, PD_MAX_REVISION,"left-attach",&szLeft);
		getPropertyFromSDH(sdhTemp,true, PD_MAX_REVISION,"right-attach",&szRight);
		getPropertyFromSDH(sdhTemp,true, PD_MAX_REVISION,"top-attach",&szTop);
		getPropertyFromSDH(sdhTemp,true, PD_MAX_REVISION,"bot-attach",&szBot);
		if(szLeft != NULL)
		{
			UT_DEBUGMSG(("left-attach %s right-attach %s top-attach %s bot-attach %s \n",szLeft,szRight,szTop,szBot));
		}
		pf = pf->getNext();
		while(pf && pf->getType() != pf_Frag::PFT_Strux)
		{
			UT_DEBUGMSG(("MiniDump: Other Frag %x of Type %d \n",pf,pf->getType()));
			pf = pf->getNext();
		}
		if(pf)
			pfs= static_cast<const pf_Frag_Strux *>(pf);
	}
#endif
}
		

/*!
 * The method returns the SDH of the cell at the location given by (rows,columns) in table 
 * pointed to by tableSDH. Returns NULL if the requested location is not contained in the
 * cell.
\params PL_StruxDocHandle tableSDH SDH of the table in question
\params UT_sint32 row row location.
\params UT_sint32 col column location
*/

PL_StruxDocHandle PD_Document::getCellSDHFromRowCol(PL_StruxDocHandle tableSDH,
													bool bShowRevisions, UT_uint32 iRevisionLevel,
													UT_sint32 row, 
													UT_sint32 col)
{
	UT_sint32 Top,Left,Bot,Right;
	const char * szLeft = NULL;
	const char * szTop = NULL;
	const char * szRight = NULL;
	const char * szBot = NULL;
	PL_StruxDocHandle cellSDH;
//
// Do the scan
//
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(tableSDH);

	UT_return_val_if_fail(currentFrag != NULL, NULL);

	currentFrag = currentFrag->getNext();
	while (currentFrag && currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_val_if_fail (currentFrag,0);
		if(currentFrag->getType() == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfSec = static_cast<const pf_Frag_Strux *>(currentFrag);
			if(pfSec->getStruxType() == PTX_SectionTable)
			{
//
// skip to the end of this nested table
//
				PL_StruxDocHandle endSDH = getEndTableStruxFromTableSDH(static_cast<PL_StruxDocHandle>(pfSec) );
				pfSec = static_cast<const pf_Frag_Strux *>(endSDH);
			}
			else if(pfSec->getStruxType() == PTX_EndTable)
			{
//
// end of table before the requested cell was found. Return NULL
//
				return NULL;
			}
			else if(pfSec->getStruxType() == PTX_SectionCell)
			{
				cellSDH = static_cast<PL_StruxDocHandle>(pfSec);
				Left = -1;
				Top = -1;
				Right = -1;
				Bot = -1;
				bool bres = getPropertyFromSDH(cellSDH,bShowRevisions,iRevisionLevel,"left-attach",&szLeft);
				if(szLeft && *szLeft)
					Left = atoi(szLeft);
				bres = getPropertyFromSDH(cellSDH,bShowRevisions,iRevisionLevel,"top-attach",&szTop);
				if(szTop && *szTop)
					Top = atoi(szTop);
				bres = getPropertyFromSDH(cellSDH,bShowRevisions,iRevisionLevel,"right-attach",&szRight);
				if(szRight && *szRight)
					Right = atoi(szRight);
				bres = getPropertyFromSDH(cellSDH,bShowRevisions,iRevisionLevel,"bot-attach",&szBot);
				if(szBot && *szBot)
					Bot = atoi(szBot);
				if( (Top <= row) && (row < Bot) && (Left <= col) && (Right > col))
				{
					return static_cast<PL_StruxDocHandle>(pfSec);
				}
			}
			currentFrag = static_cast<const pf_Frag *>(pfSec);
		}
		if(currentFrag)
			currentFrag = currentFrag->getNext();
	}
	return NULL;
}

//===================================================================================
/*!
 * This method scans the document for all styles used in the document, including
 * styles in the basedon heiracy and the followedby list
 *
 */
void PD_Document::getAllUsedStyles(UT_GenericVector <PD_Style*>* pVecStyles)
{
	UT_sint32 i = 0;
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	PD_Style * pStyle = NULL;
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		UT_return_if_fail (currentFrag);
//
// get indexAP
// get PT_STYLE_ATTRIBUTE_NAME
// if it matches style name or is contained in a basedon name or followedby
//
//
// All this code is used to find if this frag has a style in it.
//
		PT_AttrPropIndex indexAP = 0;
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
			indexAP = static_cast<pf_Frag_Strux *>(currentFrag)->getIndexAP();
		else if(currentFrag->getType()  == pf_Frag::PFT_Text)
			indexAP = static_cast<pf_Frag_Text *>(currentFrag)->getIndexAP();
		else if(currentFrag->getType()  == pf_Frag::PFT_Object)
			indexAP = static_cast<pf_Frag_Object *>(currentFrag)->getIndexAP();
		else if(currentFrag->getType()  == pf_Frag::PFT_FmtMark)
			indexAP = static_cast<pf_Frag_FmtMark *>(currentFrag)->getIndexAP();
		const PP_AttrProp * pAP = NULL;
		m_pPieceTable->getAttrProp(indexAP,&pAP);
		UT_return_if_fail (pAP);
		const XML_Char * pszStyleName = NULL;
		(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
//
// We've found a style...
//
		if(pszStyleName != NULL)
		{
			m_pPieceTable->getStyle(pszStyleName,&pStyle);
			UT_return_if_fail (pStyle);
			if(pStyle)
			{
				if(pVecStyles->findItem(pStyle) < 0)
					pVecStyles->addItem(pStyle);
				PD_Style * pBasedOn = pStyle->getBasedOn();
				i = 0;
				while(pBasedOn != NULL && i <  pp_BASEDON_DEPTH_LIMIT)
				{
					if(pVecStyles->findItem(pBasedOn) < 0)
						pVecStyles->addItem(pBasedOn);
					i++;
					pBasedOn = pBasedOn->getBasedOn();
				}
				PD_Style * pFollowedBy = pStyle->getFollowedBy();
				if(pFollowedBy && (pVecStyles->findItem(pFollowedBy) < 0))
					pVecStyles->addItem(pFollowedBy);
			}
		}
//
// Get Next frag in the table.
//
		currentFrag = currentFrag->getNext();
	}
//
// Done!
//
}


struct prevStuff
{
private:
	pf_Frag::PFType fragType;
	pf_Frag_Strux * lastFragStrux;
	PT_AttrPropIndex indexAPFrag;
	pf_Frag * thisFrag;
	PT_DocPosition thisPos;
	PT_DocPosition thisStruxPos;
	UT_uint32 fragLength;
	bool bChangeIndexAP;
	friend bool PD_Document::removeStyle(const XML_Char * pszName);
};

/*!
 * This method removes the style of name pszName from the styles definition and removes
 * all instances of it from the document including the basedon heiracy and the
 * followed-by sequences.
 */
bool PD_Document::removeStyle(const XML_Char * pszName)
{
	UT_return_val_if_fail (m_pPieceTable, false);
//
// First replace all occurances of pszName with "Normal"
//
	PD_Style * pNormal = NULL;
	PD_Style * pNuke = NULL;
	m_pPieceTable->getStyle(pszName,&pNuke);
	UT_return_val_if_fail (pNuke, false);
	pNormal = pNuke->getBasedOn();
	const XML_Char * szBack = NULL;
	if(pNormal == NULL)
	{
		m_pPieceTable->getStyle("Normal",&pNormal);
		szBack = "None";
	}
	else
	{
//
// The name of the style is stored in the PT_NAME_ATTRIBUTE_NAME attribute within the
// style
//
		pNormal->getAttribute(PT_NAME_ATTRIBUTE_NAME, szBack);
	}
	UT_return_val_if_fail (szBack, false);
	UT_return_val_if_fail (pNormal, false);
	PT_AttrPropIndex indexNormal = pNormal->getIndexAP();

//
// Now scan through the document finding all instances of pszName as either
// the style or the basedon style or the followed by style. Replace these with
// "normal"
//
	UT_GenericVector<prevStuff *> vFrag;

	PT_DocPosition pos = 0;
	PT_DocPosition posLastStrux = 0;
	pf_Frag_Strux * pfs = NULL;
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	UT_return_val_if_fail (currentFrag,false);
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
//
// get indexAP
// get PT_STYLE_ATTRIBUTE_NAME
// if it matches style name or is contained in a basedon name or followedby
//
//
// All this code is used to find if this strux has our style in it
//
		PT_AttrPropIndex indexAP = 0;
		if(currentFrag->getType()  == pf_Frag::PFT_Strux)
		{
			pfs = static_cast<pf_Frag_Strux *>(currentFrag);
			indexAP = static_cast<pf_Frag_Strux *>(currentFrag)->getIndexAP();
			posLastStrux = pos;
		}
		else if(currentFrag->getType()  == pf_Frag::PFT_Text)
		{
			indexAP = static_cast<pf_Frag_Text *>(currentFrag)->getIndexAP();
		}
		else if(currentFrag->getType()  == pf_Frag::PFT_Object)
		{
			indexAP = static_cast<pf_Frag_Object *>(currentFrag)->getIndexAP();
		}
		else if(currentFrag->getType()  == pf_Frag::PFT_FmtMark)
		{
			indexAP = static_cast<pf_Frag_FmtMark *>(currentFrag)->getIndexAP();
		}
		const PP_AttrProp * pAP = NULL;
		m_pPieceTable->getAttrProp(indexAP,&pAP);
		UT_return_val_if_fail (pAP, false);
		const XML_Char * pszStyleName = NULL;
		(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
//
// It does so remember this frag and set the old indexAP to Normal
//
		if(pszStyleName != NULL && strcmp(pszStyleName,pszName)==0)
		{
			prevStuff *  pStuff = new prevStuff;
			pf_Frag::PFType cType = currentFrag->getType();
			pStuff->fragType = cType;
			pStuff->thisFrag = currentFrag;
			pStuff->indexAPFrag = indexAP;
			pStuff->lastFragStrux = pfs;
			pStuff->thisPos = pos;
			pStuff->thisStruxPos = pos;
			pStuff->fragLength = currentFrag->getLength();
			pStuff->bChangeIndexAP = true;
			vFrag.addItem(pStuff);
//
// OK set this frag's indexAP to that of basedon of our deleted style or
// Normal.
//
			if(pf_Frag::PFT_Strux == cType)
				static_cast<pf_Frag_Strux *>(currentFrag)->setIndexAP(indexNormal);
			else if(pf_Frag::PFT_Text == cType)
				static_cast<pf_Frag_Text *>(currentFrag)->setIndexAP(indexNormal);
			else if(pf_Frag::PFT_Object == cType)
				static_cast<pf_Frag_Object *>(currentFrag)->setIndexAP(indexNormal);
			else if(pf_Frag::PFT_FmtMark == cType)
				static_cast<pf_Frag_FmtMark *>(currentFrag)->setIndexAP(indexNormal);
		}
//
// Now recursively search to see if has our style in the basedon list
//
		else if(pszStyleName != NULL)
		{
			PD_Style * cStyle = NULL;
			m_pPieceTable->getStyle(pszStyleName,&cStyle);
			UT_ASSERT_HARMLESS(cStyle);
			if(!cStyle)
				break;
			PD_Style * pBasedOn = cStyle->getBasedOn();
			PD_Style * pFollowedBy = cStyle->getFollowedBy();
			UT_uint32 i =0;
			for(i=0; (i < pp_BASEDON_DEPTH_LIMIT) && (pBasedOn != NULL) && (pBasedOn!= pNuke); i++)
			{
				pBasedOn = pBasedOn->getBasedOn();
			}
			if(pBasedOn == pNuke)
			{
				prevStuff *  pStuff = new prevStuff;
				pStuff->fragType = currentFrag->getType();
				pStuff->thisFrag = currentFrag;
				pStuff->indexAPFrag = indexAP;
				pStuff->lastFragStrux = pfs;
				pStuff->thisPos = pos;
				pStuff->thisStruxPos = pos;
				pStuff->fragLength = currentFrag->getLength();
				pStuff->bChangeIndexAP = false;
				vFrag.addItem(pStuff);
			}
//
// Look if followedBy points to our style
//
			else if(pFollowedBy == pNuke)
			{
				prevStuff *  pStuff = new prevStuff;
				pStuff->fragType = currentFrag->getType();
				pStuff->thisFrag = currentFrag;
				pStuff->indexAPFrag = indexAP;
				pStuff->lastFragStrux = pfs;
				pStuff->thisPos = pos;
				pStuff->thisStruxPos = pos;
				pStuff->fragLength = currentFrag->getLength();
				pStuff->bChangeIndexAP = false;
				vFrag.addItem(pStuff);
			}
		}
		pos = pos + currentFrag->getLength();
		currentFrag = currentFrag->getNext();
	}
//
// Now replace all pointers to this style in basedon or followedby
// with Normal
//
	UT_uint32 nstyles = getStyleCount();
	const PD_Style * cStyle = NULL;
	const char * szCstyle = NULL;
	UT_uint32 i;
	for(i=0; i< nstyles;i++)
	{
		enumStyles(i, &szCstyle,&cStyle);
		bool bDoBasedOn = false;
		bool bDoFollowedby = false;
		if(const_cast<PD_Style *>(cStyle)->getBasedOn() == pNuke)
		{
			bDoBasedOn = true;
		}
		if(const_cast<PD_Style *>(cStyle)->getFollowedBy() == pNuke)
		{
			bDoFollowedby = true;
		}
		const XML_Char * nAtts[5] ={NULL,NULL,NULL,NULL,NULL};
		if( bDoBasedOn && bDoFollowedby)
		{
			nAtts[0] = "basedon"; nAtts[1] =  szBack;
			nAtts[2]= "followedby";	nAtts[3] = "Current Settings";
			nAtts[4] = NULL;
		}
		else if ( bDoBasedOn && ! bDoFollowedby)
		{
			nAtts[0] = "basedon"; nAtts[1] =  szBack;
			nAtts[2] = NULL;
		}
		else if ( !bDoBasedOn && bDoFollowedby)
		{
			nAtts[0]= "followedby";	nAtts[1] = "Current Settings";
			nAtts[2] = NULL;
		}
		if( bDoBasedOn || bDoFollowedby)
		{
			UT_uint32 i =0;
			for(i=0; nAtts[i] != NULL; i+=2)
			{
				xxx_UT_DEBUGMSG(("SEVIOR New Style Name %s, Value %s \n",nAtts[i],nAtts[i+1]));
			}
			const_cast<PD_Style *>(cStyle)->addAttributes( static_cast<const XML_Char **>(&nAtts[0]));
		}
	}
//
// OK Now remove the style
//
	m_pPieceTable->removeStyle(pszName);
//
// Alright now we replace all the instances of fragSrux using the style to be
// deleted.
//
	UT_sint32 countChanges = vFrag.getItemCount();
	UT_sint32 j;
	pf_Frag * pfsLast = NULL;
	PX_ChangeRecord * pcr = NULL;
	for(j = 0; j<countChanges; j++)
	{
		prevStuff * pStuff = static_cast<prevStuff *>(vFrag.getNthItem(j));
		if(pStuff->fragType == pf_Frag::PFT_Strux)
		{
			pfsLast = pStuff->thisFrag;
			if(pStuff->bChangeIndexAP)
			{
				pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,pStuff->thisPos,indexNormal,
										  pfsLast->getXID());
				notifyListeners(pStuff->lastFragStrux, pcr);
				delete pcr;
			}
			else
			{
				pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,pStuff->thisPos,pStuff->indexAPFrag,
										  pfsLast->getXID());
				notifyListeners(pStuff->lastFragStrux, pcr);
				delete pcr;
			}
		}
		else
		{
			if(pStuff->lastFragStrux != pfsLast)
			{
				pfsLast = pStuff->lastFragStrux;
				if(pStuff->bChangeIndexAP)
				{
					pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,pStuff->thisPos,indexNormal,
											  pfsLast->getXID());
					notifyListeners(pStuff->lastFragStrux, pcr);
					delete pcr;
				}
				else
				{
					PT_AttrPropIndex indexLastAP = static_cast<pf_Frag_Strux *>(pfsLast)->getIndexAP();
					pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,pStuff->thisPos,indexLastAP,
											  pfsLast->getXID());
					notifyListeners(pStuff->lastFragStrux, pcr);
					delete pcr;
				}
			}
		}
	}
//  		else if(bisCharStyle)
//  		{
//  			UT_uint32 blockoffset = (UT_uint32) (pStuff->thisPos - pStuff->thisStruxPos -1);
//  			pf_Frag_Text * pft = static_cast<pf_Frag_Text *>(pStuff->thisFrag);
//  			PX_ChangeRecord_SpanChange * pcr =
//  				new PX_ChangeRecord_SpanChange(PX_ChangeRecord::PXT_ChangeSpan,
//  											   pStuff->thisPos,
//  											   pStuff->indexAPFrag,
//  											   indexNormal,
//  											   m_pPieceTable->getVarSet().getBufIndex(pft->getBufIndex(),0),
//  											   pStuff->fragLength,
//  											   blockoffset);
//  			notifyListeners(pStuff->lastFragStrux, pcr);
//  			delete pcr;
//  		}
//  	}
	if(countChanges > 0)
	{
		UT_VECTOR_PURGEALL(prevStuff *,vFrag);
	}
//
// Now reformat the entire document
//
//	signalListeners(PD_SIGNAL_REFORMAT_LAYOUT);
	return true;
}

bool PD_Document::appendStyle(const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->appendStyle(attributes);
}

size_t PD_Document::getStyleCount(void)
{
  UT_return_val_if_fail (m_pPieceTable, false);

  return m_pPieceTable->getStyleCount();
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

bool PD_Document::tellListener(PL_Listener* pListener)
{
	UT_return_val_if_fail (pListener,false);
	UT_return_val_if_fail (m_pPieceTable,false);

	return m_pPieceTable->tellListener(pListener);
}

bool PD_Document::tellListenerSubset(PL_Listener* pListener, PD_DocumentRange * pDocRange)
{
	UT_return_val_if_fail (pListener, false);
	UT_return_val_if_fail (m_pPieceTable, false);
	UT_return_val_if_fail (pDocRange && pDocRange->m_pDoc==this, false);

	return m_pPieceTable->tellListenerSubset(pListener,pDocRange);
}

bool PD_Document::addListener(PL_Listener * pListener,
								 PL_ListenerId * pListenerId)
{
	UT_uint32 kLimit = m_vecListeners.getItemCount();
	UT_uint32 k=0;

	// see if we can recycle a cell in the vector.

	for (k=0; k<kLimit; k++)
		if (m_vecListeners.getNthItem(k) == 0)
		{
			m_vecListeners.setNthItem(k,pListener,NULL);
			goto ClaimThisK;
		}

	// otherwise, extend the vector for it.

	if (m_vecListeners.addItem(pListener,&k) != 0)
	{
		UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		return false;				// could not add item to vector
	}
  ClaimThisK:

	// propagate the listener to the PieceTable and
	// let it do its thing.
	UT_return_val_if_fail (m_pPieceTable, false);

	// give our vector index back to the caller as a "Listener Id".

	*pListenerId = k;
	UT_return_val_if_fail (pListener, false);
	m_pPieceTable->addListener(pListener,k);
	return true;
}

bool PD_Document::removeListener(PL_ListenerId listenerId)
{
	return (m_vecListeners.setNthItem(listenerId,NULL,NULL) == 0);
}

bool PD_Document::signalListeners(UT_uint32 iSignal) const
{
	PL_ListenerId lid;
	PL_ListenerId lidCount = m_vecListeners.getItemCount();

	// for each listener in our vector, we send a notification.
	// we step over null listners (for listeners which have been
	// removed (views that went away)).

	for (lid=0; lid<lidCount; lid++)
	{
		PL_Listener * pListener = m_vecListeners.getNthItem(lid);
		if (pListener)
		{
			pListener->signal(iSignal);
		}
	}

	return true;
}

bool PD_Document::notifyListeners(const pf_Frag_Strux * pfs, const PX_ChangeRecord * pcr) const
{
	// notify listeners of a change.

#ifdef PT_TEST
	//pcr->__dump();
#endif

	PL_ListenerId lid;
	PL_ListenerId lidCount = m_vecListeners.getItemCount();

	// for each listener in our vector, we send a notification.
	// we step over null listners (for listeners which have been
	// removed (views that went away)).

	for (lid=0; lid<lidCount; lid++)
	{
		PL_Listener * pListener = static_cast<PL_Listener *>(m_vecListeners.getNthItem(lid));
		if (pListener)
		{
			PL_StruxFmtHandle sfh = 0;
			if (pfs)
				sfh = pfs->getFmtHandle(lid);
			pListener->change(sfh,pcr);
		}
	}

	return true;
}

void PD_Document::deferNotifications(void)
{
	// notify listeners to defer notifications.

#ifdef PT_TEST
	//pcr->__dump();
#endif

	PL_ListenerId lid;
	PL_ListenerId lidCount = m_vecListeners.getItemCount();

	// for each listener in our vector, we send a notification.
	// we step over null listeners (for listeners which have been
	// removed (views that went away)).

	for (lid=0; lid<lidCount; lid++)
	{
		PL_Listener * pListener = static_cast<PL_Listener *>(m_vecListeners.getNthItem(lid));
		if (pListener)
		{
			pListener->deferNotifications();
		}
	}
}

void PD_Document::processDeferredNotifications(void)
{
	// notify listeners to process any deferred notifications.

#ifdef PT_TEST
	//pcr->__dump();
#endif

	PL_ListenerId lid;
	PL_ListenerId lidCount = m_vecListeners.getItemCount();

	// for each listener in our vector, we send a notification.
	// we step over null listeners (for listeners which have been
	// removed (views that went away)).

	for (lid=0; lid<lidCount; lid++)
	{
		PL_Listener * pListener = m_vecListeners.getNthItem(lid);
		if (pListener)
		{
			pListener->processDeferredNotifications();
		}
	}
}




PL_StruxFmtHandle PD_Document::getNthFmtHandle(PL_StruxDocHandle sdh, UT_uint32 n)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	UT_uint32 nListen = m_vecListeners.getItemCount();
	if(n >= nListen)
		return NULL;
	PL_ListenerId lid = static_cast<PL_ListenerId>(n);
	PL_StruxFmtHandle sfh = pfs->getFmtHandle(lid);
	return sfh;
}

static void s_BindHandles(PL_StruxDocHandle sdhNew,
						  PL_ListenerId lid,
						  PL_StruxFmtHandle sfhNew)
{
	UT_return_if_fail (sdhNew);
	UT_return_if_fail (sfhNew);

	pf_Frag_Strux * pfsNew = const_cast<pf_Frag_Strux *>(static_cast<const pf_Frag_Strux *>(sdhNew));
	UT_DEBUGMSG(("Set Format handle number %d of strux %x to format %x \n",lid,pfsNew,sfhNew));
	pfsNew->setFmtHandle(lid,sfhNew);
}

bool PD_Document::notifyListeners(const pf_Frag_Strux * pfs,
									 pf_Frag_Strux * pfsNew,
									 const PX_ChangeRecord * pcr) const
{
	// notify listeners of a new strux.  this is slightly
	// different from the other one because we need to exchange
	// handles with the listener for the new strux.

#ifdef PT_TEST
	//pcr->__dump();
#endif

	PL_ListenerId lid;
	PL_ListenerId lidCount = m_vecListeners.getItemCount();

	// for each listener in our vector, we send a notification.
	// we step over null listeners (for listeners which have been
	// removed (views that went away)).

	for (lid=0; lid<lidCount; lid++)
	{
		PL_Listener * pListener = m_vecListeners.getNthItem(lid);
		if (pListener)
		{
			PL_StruxDocHandle sdhNew = static_cast<PL_StruxDocHandle>(pfsNew);
			PL_StruxFmtHandle sfh = pfs->getFmtHandle(lid);
			if (pListener->insertStrux(sfh,pcr,sdhNew,lid,s_BindHandles))
			{
				// verify that the listener used our callback
				UT_ASSERT_HARMLESS(pfsNew->getFmtHandle(lid));
			}
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
/*!
    If the input pAP contains revision attribute, this function
    returns AP in which the revision attribute has been inflated into
    actual properties and attributes (the return AP lives in the PT varset,
    so it is not to be modified, deleted, etc., but the caller)
    
    bShow indicates whether revisions are shown or hidden (view - dependent)
    iId is the id of revision to be shown (view - dependent)

    on return bHiddenRevision indicates if the element associated with
    pAP is to be hidden or visible
*/
const PP_AttrProp * PD_Document::explodeRevisions(PP_RevisionAttr *& pRevisions, const PP_AttrProp * pAP,
												  bool bShow, UT_uint32 iId, bool &bHiddenRevision) const
{
	PP_AttrProp * pNewAP = NULL;
	const XML_Char* pRevision = NULL;
	bHiddenRevision = false;
	
	bool bMark = isMarkRevisions();
	
	if(pAP && pAP->getAttribute("revision", pRevision))
	{
		if(!pRevisions)
			pRevisions = new PP_RevisionAttr(pRevision);

		UT_return_val_if_fail(pRevisions, NULL);
		
		//first we need to ascertain if this revision is visible
		bool bDeleted = false;

		const PP_Revision * pRev;
		UT_uint32 i = 0;
		UT_uint32 iMinId;

		pRev = pRevisions->getLastRevision();
		UT_return_val_if_fail(pRev, NULL);
		
		UT_uint32 iMaxId = pRev->getId();

		if(!bMark && !bShow && iId == 0)
		{
			// revisions are not to be shown, and the document to be
			// shown in the state before the first revision, i.e.,
			// additions are to be hidden, fmt changes ignored, and
			// deletions will be visible

			// see if the first revision is an addition ...
			i = 1;
			do
			{
				pRev = pRevisions->getRevisionWithId(i, iMinId);

				if(!pRev)
				{
					UT_DEBUGMSG(("PD_Document::inflateRevisions: iMinId %d\n", iMinId));
					
					if(iMinId == PD_MAX_REVISION)
					{
						UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
						return NULL;
					}

					// jump directly to the first revision ...
					i = iMinId;
				}
			}
			while(!pRev && i <= iMaxId);
			
				
			if(  (pRev->getType() == PP_REVISION_ADDITION)
			   ||(pRev->getType() == PP_REVISION_ADDITION_AND_FMT))
			{
				bHiddenRevision = true;
				return NULL;
			}

			bHiddenRevision = false;
			return NULL;
		}
		
		if((bMark || !bShow) && iId != 0)
		{
			// revisions not to be shown, but document to be presented
			// as it looks after the revision iId
			// UT_ASSERT( bMark || iId == PD_MAX_REVISION );
			
			UT_uint32 iMyMaxId = bMark ? UT_MIN(iId,iMaxId) : iMaxId;

			// we need to loop through subsequent revisions,
			// working out the their cumulative effect
			i = 1;
			
			for(i = 1; i <= iMyMaxId; i++)
			{
				pRev = pRevisions->getRevisionWithId(i,iMinId);

				if(!pRev)
				{
					if(iMinId == PD_MAX_REVISION)
					{
						UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
						break;
					}

					// advance i so that we do not waste our time, -1
					// because of i++ in loop
					i = iMinId - 1;
					continue;
				}
			
			
				if(  (pRev->getType() == PP_REVISION_FMT_CHANGE && !bDeleted)
					 ||(pRev->getType() == PP_REVISION_ADDITION_AND_FMT))
				{
					// create copy of span AP and then set all props contained
					// in our revision;
					if(!pNewAP)
					{
						pNewAP = new PP_AttrProp;
						UT_return_val_if_fail(pNewAP,NULL);
				
						(*pNewAP) = *pAP;
						(*pNewAP) = *pRev;
					}
					else
					{
						// add fmt to our AP
						pNewAP->setAttributes(pRev->getAttributes());
						pNewAP->setProperties(pRev->getProperties());
					}
				}
				else if(pRev->getType() == PP_REVISION_DELETION)
				{
					// deletion means resetting all previous fmt
					// changes
					if(pNewAP)
					{
						delete pNewAP;
						pNewAP = NULL;
					}

					bDeleted = true;
				}
				else if(pRev->getType() == PP_REVISION_ADDITION)
				{
					bDeleted = false;
				}
			} // for

			if(bDeleted)
			{
				bHiddenRevision = true;
			}
			else
			{
				bHiddenRevision = false;
			}

			if(!bMark || iId == PD_MAX_REVISION)
			{
				if(pNewAP)
				{
					// explode style, prune and store the AP
					pNewAP->explodeStyle(this);
					pNewAP->prune();
					pNewAP->markReadOnly();
					
					PT_AttrPropIndex api;
					UT_return_val_if_fail(getPieceTable()->getVarSet().addIfUniqueAP(pNewAP, &api), NULL);
					pAP->setRevisedIndex(api,iId,bShow,bMark,bHiddenRevision);

					// the above might have resulted in the deletion
					// of pNewAP -- retrieve it by the index
					getAttrProp(api, const_cast<const PP_AttrProp **>(&pNewAP));
				}
				
				return pNewAP;
			}
			
			// if we are in Mark mode, we need to process the last
			// revision ... 
		}
		else if(!pRevisions->isVisible(iId))
		{
			// we are to show revisions with id <= iId
			bHiddenRevision = true;
			UT_ASSERT_HARMLESS(!pNewAP);
			return NULL;
		}

		//next step is to find any fmt changes, layering them as
		//subsequent revisions come
		if(bMark && iId != 0)
		{
			// we are in Mark mode and only interested in the last
			// revision; the loop below will run only once
			i = UT_MIN(iId+1,iMaxId);
		}
		else
		{
			i = 1;
		}
		

		for(i = 1; i <= iMaxId; i++)
		{
			pRev = pRevisions->getRevisionWithId(i,iMinId);

			if(!pRev)
			{
				if(iMinId == PD_MAX_REVISION)
				{
					UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
					break;
				}

				// advance i so that we do not waste our time, -1
				// because of i++ in loop
				i = iMinId - 1;
				continue;
			}
			
			
			if(  (pRev->getType() == PP_REVISION_FMT_CHANGE && !bDeleted)
				 ||(pRev->getType() == PP_REVISION_ADDITION_AND_FMT))
			{
				// create copy of span AP and then set all props contained
				// in our revision;
				if(!pNewAP)
				{
					pNewAP = new PP_AttrProp;
					UT_return_val_if_fail(pNewAP, NULL);
				
					(*pNewAP) = *pAP;
					(*pNewAP) = *pRev;
					bDeleted = false;
				}
				else
				{
					// add fmt to our AP
					pNewAP->setAttributes(pRev->getAttributes());
					pNewAP->setProperties(pRev->getProperties());
					bDeleted = false;
				}
			}
		} // for
	} // if "revision"

	if(pNewAP)
	{
		// explode style, prune and store the AP
		pNewAP->explodeStyle(this);
		pNewAP->prune();
		pNewAP->markReadOnly();
					
		PT_AttrPropIndex api;
		UT_return_val_if_fail(getPieceTable()->getVarSet().addIfUniqueAP(pNewAP, &api), NULL);
		pAP->setRevisedIndex(api,iId,bShow,bMark,bHiddenRevision);

		// the above might have resulted in the deletion
		// of pNewAP -- retrieve it by the index
		getAttrProp(api, const_cast<const PP_AttrProp**>(&pNewAP));
	}
				
	return pNewAP;
}

bool PD_Document::getAttrProp(PT_AttrPropIndex indexAP, const PP_AttrProp ** ppAP) const
{
	return m_pPieceTable->getAttrProp(indexAP,ppAP);
}

const UT_UCSChar * PD_Document::getPointer(PT_BufIndex bi) const
{
	// the pointer that we return is NOT a zero-terminated
	// string.  the caller is responsible for knowing how
	// long the data is within the span/fragment.

	return m_pPieceTable->getPointer(bi);
}

bool PD_Document::getBlockBuf(PL_StruxDocHandle sdh, UT_GrowBuf * pgb) const
{
	return m_pPieceTable->getBlockBuf(sdh,pgb);
}

bool PD_Document::getBounds(bool bEnd, PT_DocPosition & docPos) const
{
	return m_pPieceTable->getBounds(bEnd,docPos);
}

PT_DocPosition PD_Document::getStruxPosition(PL_StruxDocHandle sdh) const
{
	return m_pPieceTable->getStruxPosition(sdh);
}

bool PD_Document::getSpanAttrProp(PL_StruxDocHandle sdh, UT_uint32 offset, bool bLeftSide,
									 const PP_AttrProp ** ppAP) const
{
	return m_pPieceTable->getSpanAttrProp(sdh,offset,bLeftSide,ppAP);
}

/*!
 * Return strux type of the StruxDocHandle
 */
PTStruxType PD_Document::getStruxType(PL_StruxDocHandle sdh) const
{
	UT_return_val_if_fail( sdh,(PTStruxType)0 );
	const pf_Frag * pf = static_cast<const pf_Frag *>(sdh);
	UT_return_val_if_fail (pf->getType() == pf_Frag::PFT_Strux,(PTStruxType)0);
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *> (pf);
	return pfs->getStruxType();
}

pf_Frag * PD_Document::findBookmark(const char * pName, bool bEnd, pf_Frag * pfStart)
{
	if(!pfStart)
	{
		pfStart = getPieceTable()->getFragments().getFirst();
	}

	UT_return_val_if_fail(pfStart, NULL);

	pf_Frag * pf = pfStart;
	while(pf)
	{
		if(pf->getType() == pf_Frag::PFT_Object)
		{
			pf_Frag_Object * po = (pf_Frag_Object*) pf;
			if(po->getObjectType() == PTO_Bookmark)
			{
				po_Bookmark * pb = po->getBookmark();
				if(!pb)
				{
					UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
					pf = pf->getNext();
					continue;
				}

				if(  (!bEnd && pb->getBookmarkType() == po_Bookmark::POBOOKMARK_START)
				   ||( bEnd && pb->getBookmarkType() == po_Bookmark::POBOOKMARK_END))
				{
					if(0 == UT_strcmp(pName, pb->getName()))
					   return pf;
				}
			}
		}

		pf = pf->getNext();
	}

	return NULL;
}


po_Bookmark * PD_Document::getBookmark(PL_StruxDocHandle sdh, UT_uint32 offset)
{
	const pf_Frag * pf = static_cast<const pf_Frag *>(sdh);
	UT_return_val_if_fail (pf->getType() == pf_Frag::PFT_Strux, NULL);
	const pf_Frag_Strux * pfsBlock = static_cast<const pf_Frag_Strux *> (pf);
	UT_return_val_if_fail (pfsBlock->getStruxType() == PTX_Block, NULL);

	UT_uint32 cumOffset = 0;
	pf_Frag_Object * pfo = NULL;
	for (pf_Frag * pfTemp=pfsBlock->getNext(); (pfTemp); pfTemp=pfTemp->getNext())
	{
		cumOffset += pfTemp->getLength();
		if (offset < cumOffset)
		{
			switch (pfTemp->getType())
			{
				case pf_Frag::PFT_Object:
					pfo = static_cast<pf_Frag_Object *> (pfTemp);
					return pfo->getBookmark();
				default:
					return NULL;
			}
		}

	}
	return NULL;
}

bool PD_Document::getField(PL_StruxDocHandle sdh, UT_uint32 offset,
                               fd_Field * & pField)
{

	const pf_Frag * pf = static_cast<const pf_Frag *>(sdh);
	UT_return_val_if_fail (pf->getType() == pf_Frag::PFT_Strux, false);
	const pf_Frag_Strux * pfsBlock = static_cast<const pf_Frag_Strux *> (pf);
	UT_return_val_if_fail (pfsBlock->getStruxType() == PTX_Block, false);

	UT_uint32 cumOffset = 0;
	pf_Frag_Text * pft = NULL;
	for (pf_Frag * pfTemp=pfsBlock->getNext(); (pfTemp); pfTemp=pfTemp->getNext())
	{
		cumOffset += pfTemp->getLength();
		if (offset < cumOffset)
		{
			switch (pfTemp->getType())
			{
			case pf_Frag::PFT_Text:
			case pf_Frag::PFT_Object:
				pft = static_cast<pf_Frag_Text *> (pfTemp);
				pField = pft->getField();
				return true; // break out of loop
				break;
			default:
				return false;
				break;
			}
		}

	}
	return false;
}

bool PD_Document::getStruxFromPosition(PL_ListenerId listenerId,
										  PT_DocPosition docPos,
										  PL_StruxFmtHandle * psfh) const
{
	return m_pPieceTable->getStruxFromPosition(listenerId,docPos,psfh);
}

bool PD_Document::getStruxOfTypeFromPosition(PL_ListenerId listenerId,
												PT_DocPosition docPos,
												PTStruxType pts,
												PL_StruxFmtHandle * psfh) const
{
	return m_pPieceTable->getStruxOfTypeFromPosition(listenerId,docPos,pts,psfh);
}


///
///  return the SDH of the last strux of the given type
/// immediately prior to the given absolute document position.
/// This sdh is actually a (void *) pointer to a pf_Frag_Strux
///
bool PD_Document::getStruxOfTypeFromPosition(PT_DocPosition docPos,
												PTStruxType pts,
												PL_StruxDocHandle * sdh) const
{
	return m_pPieceTable->getStruxOfTypeFromPosition(docPos,pts, sdh);
}

///
/// Return the sdh of type pts immediately prior to sdh
///
bool PD_Document::getPrevStruxOfType(PL_StruxDocHandle sdh,PTStruxType pts,
					PL_StruxDocHandle * prevsdh)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	UT_return_val_if_fail (pfs, false);
	pfs = static_cast<const pf_Frag_Strux *>(pfs->getPrev());
	for (const pf_Frag * pf=pfs; (pf); pf=pf->getPrev())
		if (pf->getType() == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfsTemp = static_cast<const pf_Frag_Strux *>(pf);
			if (pfsTemp->getStruxType() == pts)	// did we find it
			{
				*prevsdh = pfsTemp;
				return true;
			}
		}

	// did not find it.

	return false;
}


///
///get the next strux after the strux given. Skip embedded strux's
///
bool PD_Document::getNextStrux(PL_StruxDocHandle sdh,
							   PL_StruxDocHandle * nextsdh)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	UT_return_val_if_fail (pfs, false);
	pfs = static_cast<pf_Frag_Strux *>(pfs->getNext());
	UT_sint32 iEmbedDepth = 0;
	for (pf_Frag * pf=static_cast<pf_Frag *>(const_cast<pf_Frag_Strux *>(pfs)); (pf); pf=pf->getNext())
	{
		if (pf->getType() == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfsTemp = static_cast<const pf_Frag_Strux *>(pf);
			if(iEmbedDepth <= 0 && !m_pPieceTable->isFootnote(pf) &&
									 !m_pPieceTable->isEndFootnote(pf))
			{
				*nextsdh = pfsTemp;
				return true;
			}
			else if(m_pPieceTable->isFootnote(pf))
			{
				iEmbedDepth++;
			}
			else if(m_pPieceTable->isEndFootnote(pf))
			{
				iEmbedDepth--;
			}
		}
	}
	// did not find it.

	return false;
}

pf_Frag * PD_Document::getFragFromPosition(PT_DocPosition docPos) const
{
	pf_Frag * pf = 0;
	m_pPieceTable->getFragFromPosition(docPos,&pf,0);
	return pf;
}

///
/// Return the sdh of type pts immediately after sdh
///
bool PD_Document::getNextStruxOfType(PL_StruxDocHandle sdh,PTStruxType pts,
					PL_StruxDocHandle * nextsdh)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	UT_return_val_if_fail (pfs, false);
	pfs = static_cast<pf_Frag_Strux *>(pfs->getNext());
	UT_sint32 iNest = 0;
	for (const pf_Frag * pf=pfs; (pf); pf=pf->getNext())
		if (pf->getType() == pf_Frag::PFT_Strux)
		{
			const pf_Frag_Strux * pfsTemp = static_cast<const pf_Frag_Strux *>(pf);
			if((pfsTemp->getStruxType() == PTX_SectionTable) && (pts != PTX_SectionTable))
			{
				iNest++;
				continue;
			}
			if((iNest > 0) && (pfsTemp->getStruxType() == PTX_EndTable))
			{
				iNest--;
				continue;
			}
			if(iNest > 0)
			{
				continue;
			}
			if (pfsTemp->getStruxType() == pts)	// did we find it
			{
				*nextsdh = pfsTemp;
				return true;
			}
		}

	// did not find it.

	return false;
}


//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

void PD_Document::beginUserAtomicGlob(void)
{
	m_pPieceTable->beginUserAtomicGlob();
}

void PD_Document::endUserAtomicGlob(void)
{
	m_pPieceTable->endUserAtomicGlob();
}

UT_uint32 PD_Document::undoCount(bool bUndo) const
{
  return m_pPieceTable->undoCount(bUndo);
}

bool PD_Document::canDo(bool bUndo) const
{
	return m_pPieceTable->canDo(bUndo);
}

bool PD_Document::undoCmd(UT_uint32 repeatCount)
{
	UT_sint32 sRepeatCount = static_cast<UT_uint32>(repeatCount);
	while (sRepeatCount > 0)
	{
		UT_uint32 inCount = undoCount(true);
		if (!m_pPieceTable->undoCmd())
			return false;
		sRepeatCount -= inCount - undoCount(true);
	}
	return true;
}

bool PD_Document::redoCmd(UT_uint32 repeatCount)
{
	while (repeatCount--)
		if (!m_pPieceTable->redoCmd())
			return false;
	return true;
}


bool  PD_Document::isDoingTheDo(void) const
{
	return m_pPieceTable->isDoingTheDo();
}

///////////////////////////////////////////////////////////////////
// DataItems represent opaque (and probably binary) data found in
// the data-section of the document.  These are used, for example,
// to store the actual data of an image.  The inline image tag has
// a reference to a DataItem.

bool PD_Document::createDataItem(const char * szName, bool bBase64, const UT_ByteBuf * pByteBuf,
									const void* pToken,
									void ** ppHandle)
{
	// verify unique name

	if (getDataItemDataByName(szName,NULL,NULL,NULL) == true)
		return false;				// invalid or duplicate name

	// set the actual DataItem's data using the contents of the ByteBuf.
	// we must copy it if we want to keep it.  bBase64 is TRUE if the
	// data is Base64 encoded.

	UT_return_val_if_fail (pByteBuf, false);

	struct _dataItemPair* pPair = NULL;

	UT_ByteBuf * pNew = new UT_ByteBuf();
	if (!pNew)
		return false;

	if (bBase64)
	{
		if (!UT_Base64Decode(pNew,pByteBuf))
			goto Failed;
	}
	else
	{
		if (!pNew->ins(0,pByteBuf->getPointer(0),pByteBuf->getLength()))
			goto Failed;
	}

	pPair = new _dataItemPair();
	if (!pPair)
	{
		goto Failed;
	}

	pPair->pBuf = pNew;
	pPair->pToken = pToken;
	m_hashDataItems.insert(szName, pPair);

	// give them back a handle if they want one

	if (ppHandle)
	{
		const struct _dataItemPair* pHashEntry = m_hashDataItems.pick(szName);
		UT_return_val_if_fail (pHashEntry,false);
		*ppHandle = const_cast<struct _dataItemPair *>(pHashEntry);
	}

	return true;

Failed:
	if (pNew)
	{
		delete pNew;
	}

	// we also have to free the pToken, which was created by UT_strdup
	FREEP(pToken);

	return false;
}

bool PD_Document::getDataItemDataByName(const char * szName,
										   const UT_ByteBuf ** ppByteBuf,
										   const void** ppToken,
										   void ** ppHandle) const
{
	UT_return_val_if_fail (szName && *szName, false);

	const void *pHashEntry = m_hashDataItems.pick(szName);
	if (!pHashEntry)
		return false;

	struct _dataItemPair* pPair = const_cast<struct _dataItemPair*>(static_cast<const struct _dataItemPair*>(pHashEntry));
	UT_return_val_if_fail (pPair, false);

	if (ppByteBuf)
	{
		*ppByteBuf = pPair->pBuf;
	}

	if (ppToken)
	{
		*ppToken = pPair->pToken;
	}

	if (ppHandle)
	{
		*ppHandle = const_cast<void *>(pHashEntry);
	}

	return true;
}

bool PD_Document::setDataItemToken(void * pHandle,
									  void* pToken)
{
	UT_return_val_if_fail (pHandle, false);

	struct _dataItemPair* pPair = static_cast<struct _dataItemPair*>(pHandle);
	UT_return_val_if_fail (pPair, false);

	pPair->pToken = pToken;

	return true;
}

bool PD_Document::getDataItemData(void * pHandle,
									 const char ** pszName,
									 const UT_ByteBuf ** ppByteBuf,
									 const void** ppToken) const
{
	UT_return_val_if_fail (pHandle,false);

	struct _dataItemPair* pPair = static_cast<struct _dataItemPair*>(pHandle);
	UT_return_val_if_fail (pPair, false);

	if (ppByteBuf)
	{
		*ppByteBuf = pPair->pBuf;
	}

	if (ppToken)
	{
		*ppToken = pPair->pToken;
	}

	if (pszName)
	{
		UT_ASSERT_HARMLESS(UT_TODO);
		*pszName = 0;
		//*pszName = pHashEntry->pszLeft;
	}

	return true;
}

bool PD_Document::enumDataItems(UT_uint32 k,
								   void ** ppHandle, const char ** pszName, const UT_ByteBuf ** ppByteBuf, const void** ppToken) const
{
	// return the kth data item.

	UT_uint32 kLimit = m_hashDataItems.size();
	if (k >= kLimit)
		return false;

	UT_GenericStringMap<struct _dataItemPair*>::UT_Cursor c(&m_hashDataItems);
	const struct _dataItemPair* pHashEntry = NULL;
	UT_uint32 i;

	for (i = 0, pHashEntry = c.first();
	     c.is_valid() && i < k; i++, pHashEntry = c.next())
	  {
	    // noop
	  }

	if (ppHandle && c.is_valid())
		*ppHandle = const_cast<struct _dataItemPair*>(pHashEntry);

	const struct _dataItemPair* pPair = pHashEntry;
	UT_return_val_if_fail (pPair, false);

	if (ppByteBuf)
	{
		*ppByteBuf = pPair->pBuf;
	}

	if (ppToken)
	{
		*ppToken = pPair->pToken;
	}

	if (pszName)
	{
		*pszName = c.key().c_str();
	}

	return true;
}

void PD_Document::_destroyDataItemData(void)
{
	if (m_hashDataItems.size() == 0)
		return;

	UT_GenericStringMap<struct _dataItemPair*>::UT_Cursor c(&m_hashDataItems);
	struct _dataItemPair *val = NULL;

	for (val = c.first(); c.is_valid(); val = c.next())
	  {
		xxx_UT_DEBUGMSG(("DOM: destroying data item\n"));
		struct _dataItemPair* pPair = val;
		UT_return_if_fail (pPair);
		UT_String key = c.key();
		m_hashDataItems.remove (key, NULL);
		delete pPair->pBuf;
		FREEP(pPair->pToken);
		delete pPair;
	}
}

/*!
  Synchronize the last opened/last saves filetypes.
 \param bReadLastSavedAsType True to write last opened and read last
           saved type; otherwise, write last saved type from last opened type.

 There are actually two filetypes - one for importers and one for
 exporters.  This function tries to synchronize the one to the other.
*/
bool PD_Document::_syncFileTypes(bool bReadSaveWriteOpen)
{
	const char *szSuffixes;

	// used to operate on description. now operates on suffixes

	if (bReadSaveWriteOpen)
	  szSuffixes = IE_Exp::suffixesForFileType(m_lastSavedAsType);
	else
	  szSuffixes = IE_Imp::suffixesForFileType(m_lastOpenedType);

	if (!szSuffixes)
	  return false;

	IEFileType ieft;
	if (bReadSaveWriteOpen)
	{
		ieft = IE_Imp::fileTypeForSuffixes(szSuffixes);
		m_lastOpenedType = ieft;
	}
	else
	{
		ieft = IE_Exp::fileTypeForSuffixes(szSuffixes);
		m_lastSavedAsType = ieft;
	}

	if (ieft == IEFT_Unknown || ieft == IEFT_Bogus)
	{
		UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
		return false;
	}

	return true;
}

///////////////////////////////////////////////////////////////////
// Styles represent named collections of formatting properties.

bool PD_Document::getStyle(const char * szName, PD_Style ** ppStyle) const
{
	return m_pPieceTable->getStyle(szName, ppStyle);
}

bool PD_Document::enumStyles(UT_uint32 k,
								const char ** pszName, const PD_Style ** ppStyle) const
{
	return m_pPieceTable->enumStyles(k, pszName, ppStyle);
}

bool	PD_Document::addStyleProperty(const char * szStyleName, const char * szPropertyName, const char * szPropertyValue)
{
	PD_Style * pS;
	PD_Style ** ppS = &pS;
	if(!m_pPieceTable->getStyle(szStyleName, ppS))
		return false;

	return (*ppS)->addProperty(szPropertyName, szPropertyValue);
}

bool	PD_Document::addStyleProperties(const XML_Char * szStyleName, const XML_Char ** pProperties)
{
	PD_Style * pS;
	PD_Style ** ppS = &pS;
	if(!m_pPieceTable->getStyle(szStyleName, ppS))
		return false;
	if(!(*ppS)->addProperties(pProperties))
		return false;
	return updateDocForStyleChange(szStyleName,!(*ppS)->isCharStyle());
}

/*!
 * This methods changes the attributes of a style (basedon,followedby)
 *
\param szStyleName the const XML_Char * name of the style
\param pAttribs The list of attributes of the updated style.
*/
bool	PD_Document::addStyleAttributes(const XML_Char * szStyleName, const XML_Char ** pAttribs)
{
	PD_Style * pS;
	PD_Style ** ppS = &pS;
	if(!m_pPieceTable->getStyle(szStyleName, ppS))
		return false;
	if(!(*ppS)->addAttributes(pAttribs))
		return false;
//
// These functions just set the new member variable pointers in the class
//
	(*ppS)->getBasedOn();
	(*ppS)->getFollowedBy();
	return updateDocForStyleChange(szStyleName,!(*ppS)->isCharStyle());
}

/*!
 * The method returns the style defined in a sdh. If there is no style it returns
 * NULL
 */
PD_Style * PD_Document::getStyleFromSDH( PL_StruxDocHandle sdh)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex indexAP = pfs->getIndexAP();
	const PP_AttrProp * pAP = NULL;
	m_pPieceTable->getAttrProp(indexAP,&pAP);
	UT_return_val_if_fail (pAP, NULL);
	const XML_Char * pszStyleName = NULL;
	(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
	if(pszStyleName == NULL  || UT_strcmp(pszStyleName,"Current Settings") == 0 || UT_strcmp(pszStyleName,"None") == 0)
	{
		return NULL;
	}
	PD_Style * pStyle = NULL;
	if(!m_pPieceTable->getStyle(pszStyleName, &pStyle))
	{
		return NULL;
	}
	return pStyle;
}

/*!
 * Find previous style of type numbered heading or basedon numbered heading
\params sdh The StruxDocHandle of the fragment where we start to look from.
\returns PD_Style of the first Numbered Heading, otherwise NULL
*/
PL_StruxDocHandle PD_Document::getPrevNumberedHeadingStyle(PL_StruxDocHandle sdh)
{
	const pf_Frag * pf = static_cast<const pf_Frag_Strux *>(sdh);
	bool bFound = false;
	pf = pf->getPrev();
	PD_Style * pStyle = NULL;
	PL_StruxDocHandle foundSDH = NULL;
	PD_Style * pBasedOn = NULL;
	const char * szStyleName = NULL;
	while(pf && !bFound)
	{
		if(pf->getType() == pf_Frag::PFT_Strux)
		{
			foundSDH = static_cast<PL_StruxDocHandle>(pf);
			pStyle = getStyleFromSDH(foundSDH);
			if(pStyle != NULL)
			{
				szStyleName = pStyle->getName();
				if(strstr(szStyleName,"Numbered Heading") != 0)
				{
					bFound = true;
					break;
				}
				pBasedOn  = pStyle->getBasedOn();
				UT_uint32 i = 0;
				while(pBasedOn != NULL && i < 10 && !bFound)
				{
					if(strstr(pBasedOn->getName(),"Numbered Heading") != 0)
					{
						bFound = true;
					}
					else
					{
						pBasedOn = pBasedOn->getBasedOn();
					}
				}
				if(bFound)
				{
					break;
				}
			}
		}
//
// Should not need the if. It's in for defensive programming.
//
		if(!bFound)
		{
			pf = pf->getPrev();
		}
	}
	if(!bFound)
	{
		return NULL;
	}
	return foundSDH;
}



//
/*!
 * This methods changes the attributes /properties of a style (basedon,followedby)
 * plus the properties. We have to save the indexAP of the pre-existing style
 * and broadcast it out witht e change records.
 *
\param szStyleName the const XML_Char * name of the style
\param pAttribs The list of attributes/properties of the updated style.
*/
bool	PD_Document::setAllStyleAttributes(const XML_Char * szStyleName, const XML_Char ** pAttribs)
{
	PD_Style * pS;
	PD_Style ** ppS = &pS;
	if(!m_pPieceTable->getStyle(szStyleName, ppS))
		return false;
//
// Sevior May need this code
//	PT_AttrPropIndex oldindexAp = (*pss)->getIndexAP();
	if(!(*ppS)->setAllAttributes(pAttribs))
		return false;
//
// These functions just set the new member variable pointers in the class
//
	(*ppS)->getBasedOn();
	(*ppS)->getFollowedBy();
	return updateDocForStyleChange(szStyleName,!(*ppS)->isCharStyle());
}

/*!
 * This method scans the document backwards for a struc with the style name szStyle in it.
\params pStyle a pointer to style to be scanned for.
\params pos the document position to start from.
\return the sdh of the strux found.
*/
PL_StruxDocHandle PD_Document::findPreviousStyleStrux(const XML_Char * szStyle, PT_DocPosition pos)
{
	PL_StruxDocHandle sdh = NULL;
	getStruxOfTypeFromPosition(pos,PTX_Block, &sdh);
	const pf_Frag_Strux * pfs = NULL;
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(sdh);
	bool bFound = false;
    while (currentFrag != m_pPieceTable->getFragments().getFirst() && !bFound)
	{
		if (currentFrag->getType()==pf_Frag::PFT_Strux)
		{
//
// All this code is used to find if this strux has our style in it
//
			pfs = static_cast<const pf_Frag_Strux *> (currentFrag);
			PT_AttrPropIndex indexAP = pfs->getIndexAP();
			const PP_AttrProp * pAP = NULL;
			m_pPieceTable->getAttrProp(indexAP,&pAP);
			UT_return_val_if_fail (pAP,0);
			const XML_Char * pszStyleName = NULL;
			(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
			if(pszStyleName != NULL && strcmp(pszStyleName,szStyle)==0)
			{
				bFound = true;
			}
		}
		if(!bFound)
		{
			currentFrag = currentFrag->getPrev();
		}
	}
	if(bFound)
	{
		sdh = static_cast<PL_StruxDocHandle>(currentFrag);
	}
	else
	{
		sdh = NULL;
	}
	return sdh;
}

/*!
 * This method scans the document forwards for a strux with the style name
 * szStyle in it.
\params pStyle a pointer to style to be scanned for.
\params pos the document position to start from.
\return the sdh of the strux found.
*/
PL_StruxDocHandle PD_Document::findForwardStyleStrux(const XML_Char * szStyle, PT_DocPosition pos)
{
	PL_StruxDocHandle sdh = NULL;
	getStruxOfTypeFromPosition(pos,PTX_Block, &sdh);
	const pf_Frag_Strux * pfs = NULL;
	const pf_Frag * currentFrag = static_cast<const pf_Frag *>(sdh);
	bool bFound = false;
    while (currentFrag != m_pPieceTable->getFragments().getLast() && !bFound)
	{
		if (currentFrag->getType()==pf_Frag::PFT_Strux)
		{
//
// All this code is used to find if this strux has our style in it
//
			pfs = static_cast<const pf_Frag_Strux *> (currentFrag);
			PT_AttrPropIndex indexAP = pfs->getIndexAP();
			const PP_AttrProp * pAP = NULL;
			m_pPieceTable->getAttrProp(indexAP,&pAP);
			UT_return_val_if_fail (pAP, 0);
			const XML_Char * pszStyleName = NULL;
			(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
			if(pszStyleName != NULL && strcmp(pszStyleName,szStyle)==0)
			{
				bFound = true;
			}
		}
		if(!bFound)
		{
			currentFrag = currentFrag->getNext();
		}
	}
	if(bFound)
	{
		sdh = static_cast<PL_StruxDocHandle>(currentFrag);
	}
	else
	{
		sdh = NULL;
	}
	return sdh;
}


/*!
 * This method loops through the entire document updating each location
 * where the style exists.
\param szStyle the name of style that has changed.
\param isParaStyle true if the style is a paragraph type.
*/
bool   PD_Document::updateDocForStyleChange(const XML_Char * szStyle,
											bool isParaStyle)
{
	PT_DocPosition pos = 0;
	PT_DocPosition posLastStrux = 0;
	pf_Frag_Strux * pfs = NULL;
	PD_Style * pStyle = NULL;
	m_pPieceTable->getStyle(szStyle,&pStyle);
	UT_return_val_if_fail (pStyle, false);
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	UT_return_val_if_fail (currentFrag, false);
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
//
// get indexAP
// get PT_STYLE_ATTRIBUTE_NAME
// if it matches style name do a notify listeners call.
		if(isParaStyle)
		{
			if (currentFrag->getType()==pf_Frag::PFT_Strux)
			{
//
// All this code is used to find if this strux has our style in it
//
				pfs = static_cast<pf_Frag_Strux *> (currentFrag);
				PT_AttrPropIndex indexAP = pfs->getIndexAP();
				const PP_AttrProp * pAP = NULL;
				m_pPieceTable->getAttrProp(indexAP,&pAP);
				UT_return_val_if_fail (pAP, false);
				const XML_Char * pszStyleName = NULL;
				(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);
				bool bUpdate = false;
//
// It does so signal all the layouts to update themselves for the new definition
// of the style.
//
				if(pszStyleName != NULL && strcmp(pszStyleName,szStyle)==0)
				{
					bUpdate = true;
				}
				else if(pfs->getStruxType() == 	PTX_SectionTOC)
				{
					bUpdate = true; // FIXME should be more fine grained.
				}
//
// Look if the style in the basedon ancestory is our style
//
				else if (pszStyleName != NULL)
				{
					PD_Style * cStyle = NULL;
					m_pPieceTable->getStyle(pszStyleName,&cStyle);
					UT_ASSERT_HARMLESS(cStyle);
					if(cStyle)
					{
						PD_Style * pBasedOn = cStyle->getBasedOn();
						UT_uint32 i =0;
						for(i=0; (i < pp_BASEDON_DEPTH_LIMIT) && (pBasedOn != NULL) && (pBasedOn!= pStyle); i++)
						{
							pBasedOn = pBasedOn->getBasedOn();
						}
						if(pBasedOn == pStyle)
						{
							bUpdate = true;
						}
					}
				}
				if(bUpdate)
				{
					PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,pos,indexAP,
																pfs->getXID());
					notifyListeners(pfs, pcr);
					delete pcr;
				}
			}
		}
//
// Character type
//
		else
		{
//
// Need the most recent frag_strux to find the block containing our text span
//
			if (currentFrag->getType()==pf_Frag::PFT_Strux)
			{
				pfs = static_cast<pf_Frag_Strux *> (currentFrag);
				posLastStrux = pos;
			}
			if (currentFrag->getType()==pf_Frag::PFT_Text)
			{
//
// All this code is used to find if this Text Frag has our style in it
//
				pf_Frag_Text * pft = static_cast<pf_Frag_Text *> (currentFrag);
				PT_AttrPropIndex indexAP = pft->getIndexAP();
				const PP_AttrProp * pAP = NULL;
				m_pPieceTable->getAttrProp(indexAP,&pAP);
				UT_return_val_if_fail (pAP, false);
				const XML_Char * pszStyleName = NULL;
				(pAP)->getAttribute(PT_STYLE_ATTRIBUTE_NAME, pszStyleName);

//
// It does so signal all the layouts to update themselves for the new definition
// of the style.
//
				if(pszStyleName != NULL && strcmp(pszStyleName,szStyle)==0)
				{
					UT_uint32 blockoffset = (UT_uint32) (pos - posLastStrux -1);
					PX_ChangeRecord_SpanChange * pcr = new PX_ChangeRecord_SpanChange(PX_ChangeRecord::PXT_ChangeSpan,
																					  pos,indexAP,indexAP,
																					  m_pPieceTable->getVarSet().getBufIndex(pft->getBufIndex(),0) ,
																					  currentFrag->getLength(),
																					  blockoffset);
					notifyListeners(pfs, pcr);
					delete pcr;
				}
			}
		}
		pos += currentFrag->getLength();
		currentFrag = currentFrag->getNext();
	}
	return true;
}


/*!
 * This method updates all the layouts associated with the document.
*/
void  PD_Document::updateAllLayoutsInDoc( PL_StruxDocHandle sdh)
{
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex indexAP = pfs->getIndexAP();
	PT_DocPosition pos = getStruxPosition(sdh);
	PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ChangeStrux,
												pos,indexAP,pfs->getXID());
	notifyListeners(pfs, pcr);
	delete pcr;
}

//////////////////////////////////////////////////////////////////

void PD_Document::clearIfAtFmtMark (PT_DocPosition dpos)
{
	m_pPieceTable->clearIfAtFmtMark(dpos);
}

bool PD_Document::updateFields(void)
{
	//
	// Turn off Insertion point motion during this general update
	//
	setDontChangeInsPoint();
	pf_Frag * currentFrag = m_pPieceTable->getFragments().getFirst();
	UT_return_val_if_fail (currentFrag,false);
	while (currentFrag!=m_pPieceTable->getFragments().getLast())
	{
		if (currentFrag->getType()==pf_Frag::PFT_Object)
		{
			pf_Frag_Object * pfo = static_cast<pf_Frag_Object *>
				(currentFrag);
			if (pfo->getObjectType()==PTO_Field)
			{
				UT_return_val_if_fail (pfo->getField(), false);
				pfo->getField()->update();
			}
		}
		currentFrag = currentFrag->getNext();
	}
	//
	// Restore insertion point motion
	//
	allowChangeInsPoint();
	return true;
}

void PD_Document::setDontChangeInsPoint(void)
{
	if(m_bLoading)
	{
		UT_DEBUGMSG(("Illegal request to not change insertion Point!!! \n"));
        m_bAllowInsertPointChange = true;
		return;
	}
	m_bAllowInsertPointChange = false;
}

void PD_Document::allowChangeInsPoint(void)
{
        m_bAllowInsertPointChange = true;
}

bool PD_Document::getAllowChangeInsPoint(void) const
{
        return m_bAllowInsertPointChange;
}

////////////////////////////////////////////////////////////////////////////////
// Step towards full thread safety

void PD_Document::notifyPieceTableChangeStart(void)
{
//
// Wait for all redraws to finish before starting.
//
	UT_uint32 i = 0;
	while(m_bRedrawHappenning && i < 10000)
	{
		UT_usleep(100); // wait 100 microseonds
		i++;
	}
	if(i>0)
	{
		UT_DEBUGMSG(("!!!!Waited %d microseconds for redraw to finish \n",i*100));
	}
	m_bRedrawHappenning = false;
	_setPieceTableChanging(true);
//
// Invalidate visible direction cache variables. PieceTable manipulations of
// any sort can screw these.
//
	m_pVDBl = NULL;
	m_pVDRun = NULL;
	m_iVDLastPos = 0;
}

void PD_Document::notifyPieceTableChangeEnd(void)
{
        _setPieceTableChanging(false);
}

void PD_Document::invalidateCache(void)
{
	m_pVDBl = NULL;
	m_pVDRun = NULL;
	m_iVDLastPos = 0;
}

////////////////////////////////////////////////////////////////
// List Vector Functions



fl_AutoNum * PD_Document::getListByID(UT_uint32 id) const
{
	UT_uint16 i = 0;
	UT_sint32 cnt = 0;
	fl_AutoNum * pAutoNum;

	cnt = m_vecLists.getItemCount();
	if ( cnt <= 0)
		return static_cast<fl_AutoNum *>(NULL);
	UT_return_val_if_fail (m_vecLists.getFirstItem(), NULL);

	while (i<cnt)
	{
		pAutoNum = m_vecLists[i];
		if (pAutoNum->getID() == id)
			return pAutoNum;
		i++;
	}

	return static_cast<fl_AutoNum *>(NULL);
}

bool PD_Document::enumLists(UT_uint32 k, fl_AutoNum ** pAutoNum)
{
	UT_uint32 kLimit = m_vecLists.getItemCount();
	if (k >= kLimit)
		return false;

	if (pAutoNum)
		*pAutoNum = m_vecLists[k];

	return true;
}

fl_AutoNum * PD_Document::getNthList(UT_uint32 i) const
{
	UT_ASSERT_HARMLESS(i >= 0);
	return m_vecLists[i];
}

UT_uint32 PD_Document::getListsCount(void) const
{
	return m_vecLists.getItemCount();
}

void PD_Document::addList(fl_AutoNum * pAutoNum)
{
	UT_uint32 id = pAutoNum->getID();
	UT_uint32 i;
	UT_uint32 numlists = m_vecLists.getItemCount();
	for(i=0; i < numlists; i++)
	{
		fl_AutoNum * pAuto = m_vecLists.getNthItem(i);
		if(pAuto->getID() == id)
			break;
	}
	if(i >= numlists)
		m_vecLists.addItem(pAutoNum);
}

void PD_Document::listUpdate(PL_StruxDocHandle sdh )
{
	//
	// Notify all views of a listupdate
	//
	UT_return_if_fail (sdh);
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex pAppIndex = pfs->getIndexAP();
	PT_DocPosition pos = getStruxPosition(sdh);
#ifndef __MRC__
	const PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ListUpdate,pos,pAppIndex,pfs->getXID());
#else
	PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_ListUpdate,pos,pAppIndex,pfs->getXID());
#endif
	notifyListeners(pfs, pcr);
	delete pcr;
}


void PD_Document::StopList(PL_StruxDocHandle sdh )
{
	//
	// Notify all views of a stoplist
	//
	setHasListStopped(false);
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex pAppIndex = pfs->getIndexAP();
	PT_DocPosition pos = getStruxPosition(sdh);
#ifndef __MRC__
	const PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_StopList,pos,pAppIndex,pfs->getXID());
#else
	PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_StopList,pos,pAppIndex,pfs->getXID());
#endif
	notifyListeners(pfs, pcr);
	delete pcr;
	setHasListStopped(false);
}


bool PD_Document::appendList(const XML_Char ** attributes)
{
	const XML_Char * szID=NULL, * szPid=NULL, * szType=NULL, * szStart=NULL, * szDelim=NULL, *szDec=NULL;
	UT_uint32 id, parent_id, start;
	FL_ListType type;

	for (const XML_Char ** a = attributes; (*a); a++)
	{
		if (strcmp(a[0],"id") == 0)
			szID = a[1];
		else if (strcmp(a[0], "parentid") == 0)
			szPid = a[1];
		else if (strcmp(a[0], "type") == 0)
			szType = a[1];
		else if (strcmp(a[0], "start-value") == 0)
			szStart = a[1];
		else if (strcmp(a[0], "list-delim") == 0)
			szDelim = a[1];
		else if (strcmp(a[0], "list-decimal") == 0)
			szDec = a[1];
	}

	if(!szID)
		return false;
	if(!szPid)
		return false;
	if(!szType)
		return false;
	if(!szStart)
		return false;
	if(!szDelim)
		return false;
	if(!szDec)
		szDec = static_cast<const XML_Char *>(".");
	id = atoi(szID);
	UT_uint32 i;
	UT_uint32 numlists = m_vecLists.getItemCount();
	for(i=0; i < numlists; i++)
	{
		fl_AutoNum * pAuto = m_vecLists.getNthItem(i);
		if(pAuto->getID() == id)
			break;
	}
	if(i < numlists)
		return true; // List is already present
	parent_id = atoi(szPid);
	type = static_cast<FL_ListType>(atoi(szType));
	start = atoi(szStart);

	// this is bad design -- layout items should not be created by the document, only by the view
	// (the props and attrs of layout items are view-specific due to possible revisions settings !!!)
	fl_AutoNum * pAutoNum = new fl_AutoNum(id, parent_id, type, start, szDelim,szDec,this,NULL);
	addList(pAutoNum);

	return true;
}

bool PD_Document::areListUpdatesAllowed(void)
{
        return m_ballowListUpdates;
}

void PD_Document::disableListUpdates(void)
{
        m_ballowListUpdates = false;
}

void PD_Document::enableListUpdates(void)
{
        m_ballowListUpdates = true;
}

void PD_Document::updateDirtyLists(void)
{
	UT_uint32 iNumLists = m_vecLists.getItemCount();
	UT_uint32 i;
	fl_AutoNum * pAutoNum;
	bool bDirtyList = false;
	for(i=0; i< iNumLists; i++)
	{
		pAutoNum = m_vecLists.getNthItem(i);
		if(pAutoNum->isEmpty())
		{
			delete pAutoNum;
			m_vecLists.deleteNthItem(i);
			iNumLists--;
			i--;
		}
	}
	for(i=0; i< iNumLists; i++)
	{
		pAutoNum = m_vecLists.getNthItem(i);
		if(pAutoNum->isDirty() == true)
		{
			pAutoNum->update(0);
			bDirtyList = true;
		}
	}
	if(bDirtyList)
	{
		for(i=0; i< iNumLists; i++)
		{
			pAutoNum = m_vecLists.getNthItem(i);
			pAutoNum->fixHierarchy();
			pAutoNum->findAndSetParentItem();
		}
	}
}


bool PD_Document::fixListHierarchy(void)
{
	UT_uint32 iNumLists = m_vecLists.getItemCount();
	fl_AutoNum * pAutoNum;

	if (iNumLists == 0)
	{
		return false;
	}
	else
	{
		for (UT_uint32 i = 0; i < iNumLists; i++)
		{
			pAutoNum = m_vecLists.getNthItem(i);
			pAutoNum->fixHierarchy();
		}
		return true;
	}
}

void PD_Document::removeList(fl_AutoNum * pAutoNum, PL_StruxDocHandle sdh )
{
	UT_return_if_fail (pAutoNum);
	UT_sint32 ndx = m_vecLists.findItem(pAutoNum);
	UT_return_if_fail (ndx >= 0);
	//
	// Notify all views of a remove List
	//
	const pf_Frag_Strux * pfs = static_cast<const pf_Frag_Strux *>(sdh);
	PT_AttrPropIndex pAppIndex = pfs->getIndexAP();
	PT_DocPosition pos = getStruxPosition(sdh);
#ifndef __MRC__
	const PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_RemoveList,pos,pAppIndex,pfs->getXID());
#else
	PX_ChangeRecord * pcr = new PX_ChangeRecord(PX_ChangeRecord::PXT_RemoveList,pos,pAppIndex,pfs->getXID());
#endif
	notifyListeners(pfs, pcr);
	delete pcr;
	m_vecLists.deleteNthItem(ndx);
}

void  PD_Document::setDoingPaste(void)
{
         m_bDoingPaste = true;
}


void  PD_Document::clearDoingPaste(void)
{
         m_bDoingPaste = false;
}

bool  PD_Document::isDoingPaste(void)
{
         return m_bDoingPaste;
}

bool PD_Document::convertPercentToInches(const char * szPercent, UT_UTF8String & sInches)
{
	double width = m_docPageSize.Width(DIM_IN);
	PL_StruxDocHandle sdhSec = getLastSectionSDH();
	const char * szLeftMargin = NULL;
	const char * szRightMargin = NULL;

	// TODO -- probably needs to get revision settings from some view ...
	getPropertyFromSDH(sdhSec,true,PD_MAX_REVISION,"page-margin-left",&szLeftMargin);
	getPropertyFromSDH(sdhSec,true,PD_MAX_REVISION,"page-margin-right",&szRightMargin);
	if(szLeftMargin == NULL)
	{
		szLeftMargin = "0.5in";
	}
	if(szRightMargin == NULL)
	{
		szRightMargin = "0.5in";
	}
	double dLeft = UT_convertToInches(szLeftMargin);
	double dRight = UT_convertToInches(szRightMargin);
	width = width - dLeft - dRight;
	UT_String sVal = szPercent;
	sInches = UT_convertInchesToDimensionString(DIM_IN,width);
	return true;
}


bool PD_Document:: setPageSizeFromFile(const XML_Char ** attributes)
{
	const XML_Char * szPageSize=NULL, * szOrientation=NULL, * szWidth=NULL, * szHeight=NULL, * szUnits=NULL, * szPageScale=NULL;
	double width=0.0;
	double height=0.0;
	double scale =1.0;
	UT_Dimension u = DIM_IN;

	for (const XML_Char ** a = attributes; (*a); a++)
	{
		if (strcmp(a[0],"pagetype") == 0)
		        szPageSize = a[1];
		else if (strcmp(a[0], "orientation") == 0)
			szOrientation = a[1];
		else if (strcmp(a[0], "width") == 0)
			szWidth = a[1];
		else if (strcmp(a[0], "height") == 0)
			szHeight = a[1];
		else if (strcmp(a[0], "units") == 0)
			szUnits = a[1];
		else if (strcmp(a[0], "page-scale") == 0)
			szPageScale = a[1];
	}

	if(!szPageSize)
		return false;
	if(!szOrientation)
		return false;
	m_docPageSize.Set(static_cast<const char *>(szPageSize));

	if( szWidth && szHeight && szUnits && szPageScale)
	  {
		if(UT_XML_stricmp(szPageSize,"Custom") == 0)
		  {
		    width = UT_convertDimensionless(szWidth);
		    height = UT_convertDimensionless(szHeight);
		    if(strcmp(szUnits,"cm") == 0)
		      u = DIM_CM;
		    else if(strcmp(szUnits,"mm") == 0)
		      u = DIM_MM;
		    else if(strcmp(szUnits,"inch") == 0)
		      u = DIM_IN;
		    m_docPageSize.Set(width,height,u);
		  }

		scale =  UT_convertDimensionless(szPageScale);
		m_docPageSize.setScale(scale);
	  }

	// set portrait by default
	m_docPageSize.setPortrait();
	if( UT_XML_stricmp(szOrientation,"landscape") == 0 )
	{
		width = UT_convertDimensionless(szWidth);
		height = UT_convertDimensionless(szHeight);
		if(strcmp(szUnits,"cm") == 0)
			u = DIM_CM;
		else if(strcmp(szUnits,"mm") == 0)
			u = DIM_MM;
		else if(strcmp(szUnits,"inch") == 0)
			u = DIM_IN;
		m_docPageSize.setLandscape();
		//
		// Setting landscape causes the width and height to be swapped
		// so
		m_docPageSize.Set(height,width,u); // swap them so they out right
	}

	return true;
}

void PD_Document::addBookmark(const XML_Char * pName)
{
	m_vBookmarkNames.addItem(const_cast<void*>(static_cast<const void*>(pName)));
}

void PD_Document::removeBookmark(const XML_Char * pName)
{
	for(UT_uint32 i = 0; i < m_vBookmarkNames.getItemCount(); i++)
	{
		const XML_Char * pBM =  reinterpret_cast<const XML_Char *>(m_vBookmarkNames.getNthItem(i));
		if(!UT_XML_strcmp(pName, pBM))
		{
			m_vBookmarkNames.deleteNthItem(i);
			break;
		}
	}
}

/*! Returns true if pName doesn't correspond to a
 *  currently existing bookmark. */
bool PD_Document::isBookmarkUnique(const XML_Char * pName) const
{
	for(UT_uint32 i = 0; i < m_vBookmarkNames.getItemCount(); i++)
	{
		const XML_Char * pBM =  reinterpret_cast<const XML_Char *>(m_vBookmarkNames.getNthItem(i));
		if(!UT_XML_strcmp(pName, pBM))
			return false;
	}
	return true;
}

/*! Returns true if pName looks like a relative link, rather than a
 *  bookmark.

 *  Current heuristic: if pName contains a ., then it's a rel link;
 * otherwise it's a bookmark. */
bool PD_Document::isBookmarkRelativeLink(const XML_Char * pName) const
{
	UT_ASSERT_HARMLESS(sizeof(char) == sizeof(XML_Char));
	return strchr(static_cast<const char *>(pName), '.') != NULL;
}

//////////////////////////////////////////////////////////////////
// document-level properties

#define VARSET m_pPieceTable->getVarSet()

const PP_AttrProp * PD_Document::getAttrProp() const
{
	return VARSET.getAP(m_indexAP);
}

/*!
    Sets document attributes and properties
    can only be used while loading documents
    
    \param const XML_Char ** ppAttr: array of attribute/value pairs

	    if ppAttr == NULL and m_indexAP == 0xffffffff, the function
    	creates a new AP and sets it to the default values hardcoded
    	in it

        if ppAttr == NULL and m_indexAP != 0xffffffff, the function
        does nothing

        if ppAttr != NULL the function overlays passed attributes over
        the existing attributes (creating a new AP first if necessary)

    When initialising document attributes and props, we need to set
    m_indexAP to 0xffffffff and then call setAttributes(NULL).

    Importers should just call setAttributes(NULL) in the
    initialisation stage, this ensures that default values are set
    without overwriting existing values if those were set by the
    caller of the importer.

    Tomas, Dec 6, 2003
*/
bool PD_Document::setAttrProp(const XML_Char ** ppAttr)
{
	// this method can only be used while loading  ...
	if(m_pPieceTable->getPieceTableState() != PTS_Loading)
	{
		UT_return_val_if_fail(0,false);
	}

	bool bRet = true;
	
	if(m_indexAP == 0xffffffff)
	{
		// AP not initialised, do so and set standard document attrs
		// and properties

		// first create an empty AP by passing NULL to storeAP
		// cast needed to disambiguate function signature
		bRet = VARSET.storeAP(static_cast<const XML_Char **>(0), &m_indexAP);

		if(!bRet)
			return false;

		// now set standard attributes
		UT_uint32 i = 0;
		const UT_uint32 iSize = 21;
		const XML_Char * attr[iSize];

		attr[i++] = "xmlns";
		attr[i++] = "http://www.abisource.com/awml.dtd";

		attr[i++] = "xml:space";
		attr[i++] = "preserve";

		attr[i++] = "xmlns:awml";
		attr[i++] = "http://www.abisource.com/awml.dtd";

		attr[i++] = "xmlns:xlink";
		attr[i++] = "http://www.w3.org/1999/xlink";

		attr[i++] = "xmlns:svg";
		attr[i++] = "http://www.w3.org/2000/svg";

		attr[i++] = "xmlns:fo";
		attr[i++] = "http://www.w3.org/1999/XSL/Format";

		attr[i++] = "xmlns:math";
		attr[i++] = "http://www.w3.org/1998/Math/MathML";

		attr[i++] = "xmlns:dc";
		attr[i++] = "http://purl.org/dc/elements/1.1/";

		attr[i++] = "fileformat";
		attr[i++] = ABIWORD_FILEFORMAT_VERSION;

		if (XAP_App::s_szBuild_Version && XAP_App::s_szBuild_Version[0])
		{
			attr[i++] = "version";
			attr[i++] = XAP_App::s_szBuild_Version;
		}

		attr[i] = NULL;
		UT_return_val_if_fail(i < iSize, false);

		bRet =  setAttributes(attr);

		if(!bRet)
			return false;

		// now set default properties, starting with dominant
		// direction
		const XML_Char r[] = "rtl";
		const XML_Char l[] = "ltr";
		const XML_Char p[] = "dom-dir";
		const XML_Char * props[3] = {p,l,NULL};

		bool bRTL = false;
		XAP_App::getApp()->getPrefs()->getPrefsValueBool(AP_PREF_KEY_DefaultDirectionRtl,&bRTL);

		if(bRTL)
			props[1] = r;

		UT_DEBUGMSG(( "pd_Document::setAttrProp: setting dom-dir to %s\n", props[1]));
		bRet = setProperties(props);

		if(!bRet)
			return false;

		// if there is a default language in the preferences, set it
		UT_LocaleInfo locale;

		UT_UTF8String lang(locale.getLanguage());
		if (locale.getTerritory().size()) {
			lang += "-";
			lang += locale.getTerritory();
		}

		props[0] = "lang";
		props[1] = lang.utf8_str();
		props[2] = 0;
		bRet = setProperties(props);

		if(!bRet)
			return false;
		
		// Yes, we have to set default properties for all document-level items, because
		// some piece of code (exporter, plugin) may want to get the value of that default,
		// not unitialized memory.
		// I'm going to ask Dom the preferred way to make this rather more concise. -MG

			// Endnotes
		props[0] = "document-endnote-type";
		props[1] = "numeric";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-endnote-place-enddoc";
		props[1] = "1";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-endnote-place-endsection";
		props[1] = "0";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-endnote-initial";
		props[1] = "1";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-endnote-restart-section";
		props[1] = "0";
		props[2] = NULL;
		if(!setProperties(props)) return false;
			// Footnotes
		props[0] = "document-footnote-type";
		props[1] = "numeric";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-footnote-initial";
		props[1] = "1";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-footnote-restart-page";
		props[1] = "0";
		props[2] = NULL;
		if(!setProperties(props)) return false;
		props[0] = "document-footnote-restart-section";
		props[1] = "0";
		props[2] = NULL;
		if(!setProperties(props)) return false;
			
		// now overlay the attribs we were passed ...
		bRet = setAttributes(ppAttr);
	}
	else if(ppAttr == NULL)
	{
		// we already have an AP, and have nothing to add to it
		return true;
	}
	else
	{
		// have an AP and given something to add to it
		// first, we need to take care of the top-xid attribute
		const XML_Char * pXID = UT_getAttribute("top-xid", ppAttr);
		if(pXID && *pXID)
		{
			UT_uint32 iXID = atoi(pXID);
			m_pPieceTable->setXIDThreshold(iXID);
		}
		
		bRet = VARSET.mergeAP(PTC_AddFmt, m_indexAP, ppAttr, NULL, &m_indexAP, this);
	}
	
	return bRet;
}

bool PD_Document::setAttributes(const XML_Char ** ppAttr)
{
	return VARSET.mergeAP(PTC_AddFmt, m_indexAP, ppAttr, NULL, &m_indexAP, this);
}


bool PD_Document::setProperties(const XML_Char ** ppProps)
{
	return VARSET.mergeAP(PTC_AddFmt, m_indexAP, NULL, ppProps, &m_indexAP, this);
}

#undef VARSET

void PD_Document::lockStyles(bool b)
{
	const XML_Char *attr[3];
	const XML_Char n[] = "styles";
	const XML_Char v1[] = "locked";
	const XML_Char v2[] = "unlocked";

	attr[0] = n;
	attr[2] = NULL;

	if(b)
		attr[1] = v1;
	else
		attr[1] = v2;

	setAttributes(attr);
	m_bLockedStyles = b;
}

/*!
    Some exporters (RTF) need to know the visual direction at each
    position in the document as it is being exported. The problem is
    that visual direction is a property of the layout not of the
    document itself (I shall not make any comments about badly
    designed file formats here!). Since our document is not directly
    aware of any of its layouts, we have to find a listener for
    FL_DocLayout that is registered with this document (it does not
    matter if there are more FL_DocLayout listeners registered, the
    visual direction will be same for all, so we grab the first one),
    and from the listener we can get access to the layout, down to the
    runs which carry the information that we need.  Tomas, May 3, 2003
 */
bool PD_Document::exportGetVisDirectionAtPos(PT_DocPosition pos, UT_BidiCharType &type)
{
	if(pos == m_iVDLastPos && m_pVDRun)
	{
		// we have all the info we need cached, so just use it
		type = m_pVDRun->getVisDirection();
		return true;
	}
	else if(pos < m_iVDLastPos)
	{
		// this is the worst-case scenario, we have to start from the
		// beginning
		m_iVDLastPos = pos;
		if(!_exportInitVisDirection(pos))
			return false;
	}
	else
	{
		// we can continue from where we left of the last time
		m_iVDLastPos = pos;
		if(!_exportFindVisDirectionRunAtPos(pos))
			return false;
	}
	
	// make sure nothing has gone wrong here ...
	UT_return_val_if_fail(m_pVDRun, false);
	
	type = m_pVDRun->getVisDirection();
	return true;
}

bool PD_Document::_exportInitVisDirection(PT_DocPosition pos)
{
	m_pVDBl = NULL;
	m_pVDRun = NULL;

	// find the first DocLayout listener
	UT_uint32 count = m_vecListeners.getItemCount();
    fl_DocListener* pDocListener = NULL;
	
	for(UT_uint32 i = 0; i < count; i++)
	{
		PL_Listener * pL = (PL_Listener *) m_vecListeners.getNthItem(i);
		if(pL && pL->getType() == PTL_DocLayout)
		{
			pDocListener = (fl_DocListener*) pL;
			break;
		}
	}

	UT_return_val_if_fail(pDocListener, false);

	const FL_DocLayout * pDL = pDocListener->getLayout();
	UT_return_val_if_fail(pDL, false);

	
	m_pVDBl = pDL->findBlockAtPosition(pos);
	UT_return_val_if_fail(m_pVDBl, false);

	UT_uint32 iOffset = pos - m_pVDBl->getPosition();
	m_pVDRun = m_pVDBl->findRunAtOffset(iOffset);
	UT_return_val_if_fail(m_pVDRun, false);
	return true;
}

bool PD_Document::_exportFindVisDirectionRunAtPos(PT_DocPosition pos)
{
	// this is similar to the above, except we will first try to use
	// the cached info

	if(m_pVDBl && m_pVDRun)
	{
		UT_uint32 iOffset = pos - m_pVDBl->getPosition();

		//first see if the cached run matches (this will often be the
		//case since this we typicaly crawl over the document position
		//by position
		if(m_pVDRun->getBlockOffset() <= iOffset
		   && (m_pVDRun->getBlockOffset() + m_pVDRun->getLength()) > iOffset)
		{
			return true;
		}

		// now try to use the present block and any blocks that are
		// chained with it
		const fl_BlockLayout * pBL        = m_pVDBl;
		fp_Run *         pRunResult = NULL;

		while (1)
		{
			UT_sint32 iOffset = pos - pBL->getPosition();

			if(iOffset < 0)
				break;
			
			pRunResult = pBL->findRunAtOffset((UT_uint32)iOffset);

			if(pRunResult)
				break;
			
			const fl_ContainerLayout * pCL = pBL->getNext();
			
			if(pCL && pCL->getContainerType() == FL_CONTAINER_BLOCK)
				pBL = reinterpret_cast<const fl_BlockLayout*>(pCL);
			else
				break;
		}

		if(pRunResult)
		{
			m_pVDRun = pRunResult;
			m_pVDBl = pBL;
			return true;
		}
	}

	// if we got so far the offset is past the present
	// block-chain, i.e., in a different section, we start from
	// the beginning
	return _exportInitVisDirection(pos);
}

bool PD_Document::insertStruxBeforeFrag(pf_Frag * pF, PTStruxType pts,
										const XML_Char ** attributes, pf_Frag_Strux ** ppfs_ret)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document
	//
	// Update frames during load.
	//
	XAP_Frame * pFrame = m_pApp->getLastFocussedFrame();
	if(pFrame)
	{
		pFrame->nullUpdate();
	}
	return m_pPieceTable->insertStruxBeforeFrag(pF,pts,attributes,ppfs_ret);
}

bool PD_Document::insertSpanBeforeFrag(pf_Frag * pF, const UT_UCSChar * pbuf, UT_uint32 length)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	// REMOVE UNDESIRABLE CHARACTERS ...
	// we will remove all LRO, RLO, LRE, RLE, and PDF characters
	// * at the moment we do not handle LRE/RLE
	// * we replace LRO/RLO with our dir-override property

	const XML_Char * attrs[] = {"props", NULL, NULL};
	UT_String s;
			
	bool result = true;
	const UT_UCS4Char * pStart = pbuf;

	for(const UT_UCS4Char * p = pbuf; p < pbuf + length; p++)
	{
		switch(*p)
		{
			case UCS_LRO:
				if((p - pStart) > 0)
					result &= m_pPieceTable->insertSpanBeforeFrag(pF,pStart,p - pStart);

				s = "dir-override:ltr";
				attrs[1] = s.c_str();
				result &= m_pPieceTable->appendFmt(&attrs[0]);
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_RLO:
				if((p - pStart) > 0)
					result &= m_pPieceTable->insertSpanBeforeFrag(pF,pStart,p - pStart);

				s = "dir-override:rtl";
				attrs[1] = s.c_str();
				result &= m_pPieceTable->appendFmt(&attrs[0]);
				
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_PDF:
				if((p - pStart) > 0)
					result &= m_pPieceTable->insertSpanBeforeFrag(pF,pStart,p - pStart);

				if((m_iLastDirMarker == UCS_RLO) || (m_iLastDirMarker == UCS_LRO))
				{
					s = "dir-override:";
					attrs[1] = s.c_str();
					result &= m_pPieceTable->appendFmt(&attrs[0]);
				}
				
				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
				
			case UCS_LRE:
			case UCS_RLE:
				if((p - pStart) > 0)
					result &= m_pPieceTable->insertSpanBeforeFrag(pF,pStart,p - pStart);

				pStart = p + 1;
				m_iLastDirMarker = *p;
				break;
		}
	}
	
	result &= m_pPieceTable->insertSpanBeforeFrag(pF,pStart,length - (pStart-pbuf));
	return result;
}

bool PD_Document::insertObjectBeforeFrag(pf_Frag * pF, PTObjectType pto,
										 const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->insertObjectBeforeFrag(pF,pto,attributes);
}

bool PD_Document::insertFmtMarkBeforeFrag(pf_Frag * pF)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->insertFmtMarkBeforeFrag(pF);
}

bool PD_Document::changeStruxFormatNoUpdate(PTChangeFmt ptc ,PL_StruxDocHandle sdh,const XML_Char ** attributes)
{
	pf_Frag_Strux * pfs = const_cast<pf_Frag_Strux *>(static_cast<const pf_Frag_Strux *>(sdh));
	return m_pPieceTable->changeStruxFormatNoUpdate(ptc ,pfs,attributes);
}

bool PD_Document::insertFmtMarkBeforeFrag(pf_Frag * pF, const XML_Char ** attributes)
{
	UT_return_val_if_fail (m_pPieceTable, false);

	// can only be used while loading the document

	return m_pPieceTable->insertFmtMarkBeforeFrag(pF,attributes);
}

pf_Frag * PD_Document::findFragOfType(pf_Frag::PFType type, UT_sint32 iSubtype, const pf_Frag * pfStart)
{
	UT_return_val_if_fail(m_pPieceTable,NULL);

	pf_Frag * pf = const_cast<pf_Frag *>(pfStart);
	
	if(!pf)
		pf = m_pPieceTable->getFragments().getFirst();

	UT_return_val_if_fail(pf, NULL);

	while(pf)
	{
		bool bBreak = true;
		if(pf->getType() == type)
		{
			if(iSubtype < 0)
				break;

			switch(type)
			{
				// fragments with no subtypes
				case pf_Frag::PFT_Text:
				case pf_Frag::PFT_EndOfDoc:
				case pf_Frag::PFT_FmtMark:
					break;

				case pf_Frag::PFT_Object:
					{
						pf_Frag_Object * pfo = static_cast<pf_Frag_Object*>(pf);
						if((UT_sint32)pfo->getObjectType() != iSubtype)
							bBreak = false;
					}
					break;
					
				case pf_Frag::PFT_Strux:
					{
						pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux*>(pf);
						if((UT_sint32)pfs->getStruxType() != iSubtype)
							bBreak = false;
					}
					break;

				default: UT_ASSERT_HARMLESS(UT_NOT_REACHED);
			}

			if(bBreak)
				break;
		}

		pf = pf->getNext();
	}

	return pf;
}

pf_Frag * PD_Document::getLastFrag() const
{
	UT_return_val_if_fail(m_pPieceTable,NULL);
	return m_pPieceTable->getFragments().getLast();
}


/*!
    force the document into being dirty and signal this to our listeners
*/
void PD_Document::forceDirty()
{
	_setForceDirty(true);

	// now notify listeners ...
	// this is necessary so that the save command is available after
	// operations that only change m_bForcedDirty
	signalListeners(PD_SIGNAL_DOCDIRTY_CHANGED);	
}


/*!
    Returns true if the stylesheets of both documents are identical
*/
bool PD_Document::areDocumentStylesheetsEqual(const AD_Document &D) const
{
	if(D.getType() != ADDOCUMENT_ABIWORD)
		return false;
	
	PD_Document &d = (PD_Document &)D;
	UT_return_val_if_fail(m_pPieceTable || d.m_pPieceTable, false);

	const UT_GenericStringMap<PD_Style*> & hS1 = m_pPieceTable->getAllStyles();
	const UT_GenericStringMap<PD_Style*> & hS2 = d.m_pPieceTable->getAllStyles();

	if(hS1.size() != hS2.size())
		return false;

	UT_StringPtrMap hFmtMap;
	UT_GenericStringMap<PD_Style*>::UT_Cursor c(&hS1);

	const PD_Style * pS1, * pS2;
	for(pS1 = c.first(); pS1 != NULL; pS1 = c.next())
	{
		const UT_String &key = c.key();

		pS2 = hS2.pick(key);

		if(!pS2)
			return false;


		PT_AttrPropIndex ap1 = pS1->getIndexAP();
		PT_AttrPropIndex ap2 = pS2->getIndexAP();

		// because the indexes are into different piecetables, we
		// have to expand them
		const PP_AttrProp * pAP1;
		const PP_AttrProp * pAP2;

		m_pPieceTable->getAttrProp(ap1, &pAP1);
		d.m_pPieceTable->getAttrProp(ap2, &pAP2);

		UT_return_val_if_fail(pAP1 && pAP2, false);

		UT_String s;
		// must print all digits to make this unambigous
		UT_String_sprintf(s,"%08x%08x", ap1, ap2);
		bool bAreSame = hFmtMap.contains(s,NULL);
		
		if(!bAreSame)
		{
			if(!pAP1->isEquivalent(pAP2))
			{
				return false;
			}
			else
			{
				hFmtMap.insert(s,NULL);
			}
		}
	}
	
	return true;
}


/*!
    carries out the actual change in PieceTable; called by
    acceptRejectRevision() and rejectAllHigherRevisions()

    this method operates on a fragment at a time, but if it
    results in deletion from PT, more fragments might be deleted
*/
bool PD_Document::_acceptRejectRevision(bool bReject, UT_uint32 iStart, UT_uint32 iEnd,
										const PP_Revision * pRev,
										PP_RevisionAttr &RevAttr, pf_Frag * pf,
										bool & bDeleted)
{
	UT_return_val_if_fail(pf, false);
	bDeleted = false;

	UT_uint32 iRealDeleteCount;
	const XML_Char * ppAttr[3];
	const XML_Char rev[] = "revision";
	ppAttr[0] = rev;
	ppAttr[1] = NULL;
	ppAttr[2] = NULL;

	const XML_Char ** ppProps = NULL, ** ppAttr2 = NULL;
	bool bDeletePRev = false;
	bool bRet = true;
	UT_uint32 i;

	// if the fragment is a strux that has a corresponding end element
	// and we will be deleting itwe have to expand the deletion to the
	// end of that element
	UT_uint32 iEndDelete = iEnd;
	PP_RevisionType iRevType = pRev->getType();

	if(pf->getType() == pf_Frag::PFT_Strux &&
	   (   (bReject &&  (iRevType == PP_REVISION_ADDITION_AND_FMT || iRevType == PP_REVISION_ADDITION))
		|| (!bReject && (iRevType == PP_REVISION_DELETION))))
	{
		pf_Frag_Strux * pfs = (pf_Frag_Strux*)pf;
		PTStruxType pst = PTX_Block;
		
		switch(pfs->getStruxType())
		{
			case PTX_SectionEndnote:
				pst = PTX_EndEndnote; break;
			case PTX_SectionTable:
				pst = PTX_EndTable; break;
			case PTX_SectionCell:
				pst = PTX_EndCell; break;
			case PTX_SectionFootnote:
				pst = PTX_EndFootnote; break;
		    case PTX_SectionMarginnote:
				pst = PTX_EndMarginnote; break;
			case PTX_SectionFrame:
				pst = PTX_EndFrame; break;
			case PTX_SectionTOC:
				pst = PTX_EndTOC; break;

			default: ; // do nothing 
		}

		if(pst != PTX_Block)
		{
			pf_Frag * pf2 = pf->getNext();
			while(pf2)
			{
				iEndDelete += pf2->getLength();
				if(pf2->getType() == pf_Frag::PFT_Strux)
				{
					pf_Frag_Strux * pfs2 = (pf_Frag_Strux*)pf2;
					if(pfs2->getStruxType() == pst)
						break;
				}

				pf2 = pf2->getNext();
			}
		}
	}
	
	if(bReject)
	{
		switch(iRevType)
		{
			case PP_REVISION_ADDITION:
			case PP_REVISION_ADDITION_AND_FMT:
				{
					// delete this fragment
					bDeleted = true;

					// since we need real delete, we need to step out
					// of rev. marking mode for a moment ...
					bool bMark = isMarkRevisions();
					_setMarkRevisions(false);
					bRet = deleteSpan(iStart,iEndDelete,NULL,iRealDeleteCount);
					_setMarkRevisions(bMark);

					return bRet;
				}
					
			case PP_REVISION_DELETION:
			case PP_REVISION_FMT_CHANGE:
				// remove the revision attribute
				if(pf->getType() == pf_Frag::PFT_Strux)
				{
					pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);

					// the changeStrux function tries to locate the strux which _contains_ the
					// position we pass into it; however, iStart is the doc position of the actual
					// strux, so we have to skip over the strux
					return changeStruxFmt(PTC_RemoveFmt,iStart+1,iEnd,ppAttr,NULL, pfs->getStruxType());
				}
				else
					return changeSpanFmt(PTC_RemoveFmt,iStart,iEnd,ppAttr,NULL);

			default:
				UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
				return false;
		}
	}
	else
	{
		switch(iRevType)
		{
			case PP_REVISION_ADDITION:
				// simply remove the revision attribute
				if(pf->getType() == pf_Frag::PFT_Strux)
				{
					pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);					
					// the changeStrux function tries to locate the strux which _contains_ the
					// position we pass into it; however, iStart is the doc position of the actual
					// strux, so we have to skip over the strux
					return changeStruxFmt(PTC_RemoveFmt,iStart+1,iEnd,ppAttr,NULL, pfs->getStruxType());
				}
				else
					return changeSpanFmt(PTC_RemoveFmt,iStart,iEnd,ppAttr,NULL);

			case PP_REVISION_DELETION:
				{
					// delete this fragment
					bDeleted = true;

					// since we need real delete, we need to step out
					// of rev. marking mode for a moment ...
					bool bMark = isMarkRevisions();
					_setMarkRevisions(false);
					bRet = deleteSpan(iStart,iEndDelete,NULL,iRealDeleteCount);
					_setMarkRevisions(bMark);

					return bRet;
				}
					
			case PP_REVISION_ADDITION_AND_FMT:
				// overlay the formatting and remove the revision attribute
				if(pf->getType() == pf_Frag::PFT_Strux)
				{
					pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);					
					// the changeStrux function tries to locate the strux which _contains_ the
					// position we pass into it; however, iStart is the doc position of the actual
					// strux, so we have to skip over the strux
					return changeStruxFmt(PTC_RemoveFmt,iStart+1,iEnd,ppAttr,NULL, pfs->getStruxType());
				}
				else
					return changeSpanFmt(PTC_RemoveFmt,iStart,iEnd,ppAttr,NULL);

			case PP_REVISION_FMT_CHANGE:
				// overlay the formatting and remove this revision
				// from the revision attribute
				ppProps = new const XML_Char *[2* pRev->getPropertyCount() + 1];
				ppAttr2 = new const XML_Char *[2* pRev->getAttributeCount() + 3];

				for(i = 0; i < pRev->getPropertyCount(); i++)
				{
					pRev->getNthProperty(i, ppProps[2*i],ppProps[2*i + 1]);
				}

				ppProps[2*i] = NULL;

				for(i = 0; i < pRev->getAttributeCount(); i++)
				{
					pRev->getNthAttribute(i, ppAttr2[2*i],ppAttr2[2*i + 1]);
				}

				if(pRev->getType() == PP_REVISION_ADDITION_AND_FMT)
				{
					ppAttr2[2*i] = NULL;
				}
				else
				{
					// need to set a new revision attribute
					// first remove current revision from pRevAttr
					RevAttr.removeRevision(pRev);
					bDeletePRev = true;

					ppAttr2[2*i] = rev;
					ppAttr2[2*i + 1] = RevAttr.getXMLstring();
					ppAttr2[2*i + 2] = NULL;

					if(*ppAttr2[2*i + 1] == 0)
					{
						// no revision attribute left, which means we
						// have to remove it by separate call to changeSpanFmt

						// if this is the only attribute, we just set
						// the whole thing to NULL
						if(i == 0)
						{
							delete ppAttr2;
							ppAttr2 = NULL;
						}
						else
						{
							// OK, there are some other attributes
							// left, so we set the rev name to NULL
							// and remove the formatting by a separate
							// call to changeSpanFmt
							ppAttr2[2*i] = NULL;
						}

						// now we use the ppAttr set to remove the
						// revision attribute
						if(pf->getType() == pf_Frag::PFT_Strux)
						{
							pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);					
							// the changeStrux function tries to locate the strux which _contains_ the
							// position we pass into it; however, iStart is the doc position of the actual
							// strux, so we have to skip over the strux
							bRet &= changeStruxFmt(PTC_RemoveFmt,iStart+1,iEnd,ppAttr,NULL, pfs->getStruxType());
						}
						else
							bRet &= changeSpanFmt(PTC_RemoveFmt,iStart,iEnd,ppAttr,NULL);
					}
				}

				UT_ASSERT_HARMLESS( ppAttr2 || ppProps );

				if(pf->getType() == pf_Frag::PFT_Strux)
				{
					pf_Frag_Strux * pfs = static_cast<pf_Frag_Strux *>(pf);					
					// the changeStrux function tries to locate the strux which _contains_ the
					// position we pass into it; however, iStart is the doc position of the actual
					// strux, so we have to skip over the strux
					bRet &= changeStruxFmt(PTC_AddFmt,iStart+1,iEnd,ppAttr2,ppProps, pfs->getStruxType());
				}
				else
					bRet &= changeSpanFmt(PTC_AddFmt,iStart,iEnd,ppAttr2,ppProps);

				delete ppProps;
				delete ppAttr2;

				if(bDeletePRev)
					delete pRev;
				
				return bRet;

			default:
				UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
		}
	}

	return false;
}

bool PD_Document::acceptAllRevisions()
{
	PD_DocIterator t(*this);
	UT_return_val_if_fail(t.getStatus() == UTIter_OK, false);
	
	notifyPieceTableChangeStart();
	
	beginUserAtomicGlob();	
	while(t.getStatus() == UTIter_OK)
	{
		pf_Frag * pf = const_cast<pf_Frag *>(t.getFrag());

		if(!pf)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}
		
		PT_AttrPropIndex API = pf->getIndexAP();

		const PP_AttrProp * pAP = NULL;
		m_pPieceTable->getAttrProp(API,&pAP);
		if(!pAP)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}
		
		const XML_Char * pszRevision = NULL;
		pAP->getAttribute("revision", pszRevision);
		
		if(pszRevision == NULL)
		{
			// no revisions on this fragment
			t += pf->getLength();
			continue;
		}
			
		PP_RevisionAttr RevAttr(pszRevision);
		RevAttr.pruneForCumulativeResult(this);
		const PP_Revision * pRev = NULL;
		if(RevAttr.getRevisionsCount())
			pRev = RevAttr.getNthRevision(0);
		
		if(!pRev)
		{
			// no revisions
			t += pf->getLength();
			continue;
		}
		
		UT_uint32 iStart = t.getPosition();
		UT_uint32 iEnd   = iStart + pf->getLength();
		bool bDeleted = false;
		
		_acceptRejectRevision(false /*accept*/, iStart, iEnd, pRev, RevAttr, pf, bDeleted);
		
		// advance -- the call to _acceptRejectRevision could have
		// resulted in deletion and/or merging of fragments; we have
		// to reset the iterator
		if(bDeleted)
			t.reset(iStart, NULL);
		else
			t.reset(iEnd, NULL);
	}

	// _acceptRejectRevison() function unfortunately leaves some unwanted fmt marks in the
	// document; we will purge all fmt marks
	purgeFmtMarks();
	
	endUserAtomicGlob();
	notifyPieceTableChangeEnd();
	signalListeners(PD_SIGNAL_UPDATE_LAYOUT);
	return true;
}
	
bool PD_Document::rejectAllHigherRevisions(UT_uint32 iLevel)
{
	PD_DocIterator t(*this);
	UT_return_val_if_fail(t.getStatus() == UTIter_OK, false);
	
	const PP_Revision * pRev;

	notifyPieceTableChangeStart();
	
	beginUserAtomicGlob();	
	while(t.getStatus() == UTIter_OK)
	{
		pf_Frag * pf = const_cast<pf_Frag *>(t.getFrag());

		if(!pf)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}
		
		PT_AttrPropIndex API = pf->getIndexAP();

		const PP_AttrProp * pAP = NULL;
		m_pPieceTable->getAttrProp(API,&pAP);
		if(!pAP)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}
		
		const XML_Char * pszRevision = NULL;
		pAP->getAttribute("revision", pszRevision);
		
		if(pszRevision == NULL)
		{
			// no revisions on this fragment
			t += pf->getLength();
			continue;
		}
			
		PP_RevisionAttr RevAttr(pszRevision);
		pRev = RevAttr.getLowestGreaterOrEqualRevision(iLevel+1);
		if(!pRev)
		{
			// no higher revisions
			t += pf->getLength();
			continue;
		}
		
		UT_uint32 iStart = t.getPosition();
		UT_uint32 iEnd   = iStart + pf->getLength();
		bool bDeleted = false;
		
		_acceptRejectRevision(true /*reject*/, iStart, iEnd, pRev, RevAttr, pf, bDeleted);
		
		// advance -- the call to _acceptRejectRevision could have
		// resulted in deletion and/or merging of fragments; we have
		// to reset the iterator
		if(bDeleted)
			t.reset(iStart, NULL);
		else
			t.reset(iEnd, NULL);
	}

	// _acceptRejectRevison() function unfortunately leaves some unwanted fmt marks in the
	// document; we will purge all fmt marks
	purgeFmtMarks();
	
	endUserAtomicGlob();
	notifyPieceTableChangeEnd();
	signalListeners(PD_SIGNAL_UPDATE_LAYOUT);
	return true;
}

/*!
   accepts or reject top visible revision between document positions
   iStart and iEnd.
   
   \param bReject  true if revisions are to be rejected
   \param iPos1    document position to start at
   \param iPos2     document position to finish at
   \param iLevel   the highest revision level to accept

   \return         true on success
   
   NB: For each fragment this function removes the highest revision <=
       iLevel. For example, if iLevel is 3 and fragment contains
       revisions 1,2, 4, revision 2 will be removed.
*/
bool PD_Document::acceptRejectRevision(bool bReject, UT_uint32 iPos1,
									   UT_uint32 iPos2, UT_uint32 iLevel)
{
	UT_uint32 iPosStart = UT_MIN(iPos1, iPos2);
	UT_uint32 iPosEnd   = UT_MAX(iPos1, iPos2);
	
	PD_DocIterator t(*this, iPosStart);
	UT_return_val_if_fail(t.getStatus() == UTIter_OK, false);
	
	
	const PP_Revision * pSpecial;
	const PP_Revision * pRev;
	UT_uint32 iLenProcessed = 0;
	bool bFirst = true;
	
	notifyPieceTableChangeStart();

	beginUserAtomicGlob();	
	while(t.getStatus() == UTIter_OK && iPosStart + iLenProcessed < iPosEnd)
	{
		pf_Frag * pf = const_cast<pf_Frag *>(t.getFrag());
		if(!pf)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}

		UT_uint32 iFragLen = pf->getLength();

		if(bFirst)
		{
			// we might be working only with a part of the frag
			bFirst = false;
			iFragLen -= (iPosStart - pf->getPos());
		}
		
		iLenProcessed += iFragLen;

		PT_AttrPropIndex API = pf->getIndexAP();

		const PP_AttrProp * pAP = NULL;
		m_pPieceTable->getAttrProp(API,&pAP);
		if(!pAP)
		{
			UT_ASSERT_HARMLESS(UT_SHOULD_NOT_HAPPEN);
			endUserAtomicGlob();
			notifyPieceTableChangeEnd();
			return false;
		}
		
		const XML_Char * pszRevision = NULL;
		pAP->getAttribute("revision", pszRevision);
		
		if(pszRevision == NULL)
		{
			// no revisions on this fragment
			t += iFragLen;
			continue;
		}
			
		PP_RevisionAttr RevAttr(pszRevision);
		pRev = RevAttr.getGreatestLesserOrEqualRevision(iLevel, &pSpecial);
		if(!pRev)
		{
			// no visible revisions
			t += iFragLen;
			continue;
		}
		
		UT_uint32 iStart = t.getPosition();
		UT_uint32 iEnd   = iStart + iFragLen;

		bool bDeleted = false;
		_acceptRejectRevision(bReject, iStart, iEnd, pRev, RevAttr, pf, bDeleted);
		
		// advance -- the call to _acceptRejectRevision could have
		// resulted in deletion and/or merging of fragments; we have
		// to reset the iterator
		if(bDeleted)
			t.reset(iStart, NULL);
		else
			t.reset(iEnd, NULL);
	}

	endUserAtomicGlob();
	notifyPieceTableChangeEnd();
	signalListeners(PD_SIGNAL_UPDATE_LAYOUT);
	return true;
}


/*!
  Clears out the revisions table if no revisions are left in the document
*/
void PD_Document::purgeRevisionTable()
{
	if(getRevisions().getItemCount() == 0)
		return;

	UT_String sAPI;
	UT_StringPtrMap hAPI;
	
	PD_DocIterator t(*this);

	// work our way thought the document looking for frags with
	// revisions attributes ...
	while(t.getStatus() == UTIter_OK)
	{
		const pf_Frag * pf = t.getFrag();
		UT_return_if_fail(pf);

		PT_AttrPropIndex api = pf->getIndexAP();

		UT_String_sprintf(sAPI, "%08x", api);

		if(!hAPI.contains(sAPI, NULL))
		{
			const PP_AttrProp * pAP;
			UT_return_if_fail(getAttrProp(api, &pAP));
			UT_return_if_fail(pAP);

			const XML_Char * pVal;

			if(pAP->getAttribute(PT_REVISION_ATTRIBUTE_NAME, pVal))
				return;

			// cache this api so we do not need to do this again if we
			// come across it
			hAPI.insert(sAPI,NULL);
		}

		t += pf->getLength();
	}
	

	// if we got this far, we have not found any revisions in the
	// whole doc, clear out the table
	UT_DEBUGMSG(("PD_Document::purgeRevisionTable(): clearing\n"));
	_purgeRevisionTable();
}


void PD_Document::diffIntoRevisions(const PD_Document &d)
{
	UT_Vector vDiff;
	diffDocuments(d, vDiff);

	if(vDiff.getItemCount() == 0)
		return;

	// bracket undo
	beginUserAtomicGlob();
	
	bool bMark = isMarkRevisions();
	setMarkRevisions(true);

	// create new revision
	const XAP_StringSet * pSS = XAP_App::getApp()->getStringSet();
	UT_return_if_fail(pSS);
	UT_UCS4String ucs4(pSS->getValue(AP_STRING_ID_MSG_AutoMerge));

	addRevision(getRevisionId()+1, ucs4.ucs4_str(),ucs4.length(),time(NULL),0);

	for(UT_uint32 i = 0; i < vDiff.getItemCount(); ++i)
	{
		PD_DocumentDiff * pDiff = (PD_DocumentDiff *) vDiff.getNthItem(i);
		if(!pDiff)
		{
			UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
			continue;
		}

		if(pDiff->m_bDeleted)
		{
			// this is the easy bit ...
			UT_uint32 iRealDeleteCount;
			deleteSpan(pDiff->m_pos1, pDiff->m_pos1 + pDiff->m_len, NULL, iRealDeleteCount);
		}
		else
		{
			// we need to get the text from doc2 first
			PD_DocIterator t2(d, pDiff->m_pos2);

			UT_UCS4Char * pC = new UT_UCS4Char[pDiff->m_len];
			UT_return_if_fail(pC);
			
			UT_uint32 j = 0;

			for(j = 0; j < pDiff->m_len && t2.getStatus() == UTIter_OK; ++j, ++t2)
				pC[j] = t2.getChar();

			// TODO -- respect fmt
			insertSpan(pDiff->m_pos1, pC, j, NULL);

			delete [] pC;
		}
	}
	
	// clean up ...
	setMarkRevisions(bMark);

	//release undo
	endUserAtomicGlob();
	
	UT_VECTOR_PURGEALL(PD_DocumentDiff*, vDiff);
}


bool PD_Document::diffDocuments(const PD_Document &d, UT_Vector & vDiff) const
{
	PT_DocPosition pos1 = 0;
	UT_sint32 iOffset2 = 0;
	UT_uint32 iKnownLength = 0;

	PT_DocPosition pos1Diff = 0;
	UT_sint32 iOffset2Diff = 0;
	
	vDiff.clear();

	bool bRet = false;

	// the following loop gets terminated when we do not find a
	// similarity or we do not find a difference
	while(true)
	{
		bool bDiff = findFirstDifferenceInContent(pos1, iOffset2,d);

		UT_DEBUGMSG(("PD_Document::diffDocuments: difference found? %d\n", bDiff));
		
		if(!bDiff)
			return bRet;
		else
			bRet = true;
	
		pos1Diff = pos1;
		iOffset2Diff = iOffset2;
	
		bDiff = findWhereSimilarityResumes(pos1Diff, iOffset2Diff, iKnownLength, d);

		UT_DEBUGMSG(("PD_Document::diffDocuments: similarity found? %d\n", bDiff));

		if(!bDiff)
		{
			// no further similarities found
			// deletion if the change in iOffset2 is negative, i.e.,
			// doc2 is shorter
			bool bDel = (iOffset2 - iOffset2Diff < 0);
			UT_uint32 iLen = 0xffffffff; // to the end
	
			PD_DocumentDiff * pDiff = new PD_DocumentDiff(bDel, pos1, pos1 + iOffset2, iLen);
			vDiff.addItem((void*)pDiff);
#ifdef DEBUG
			pDiff->_dump();
#endif
			return true;
		}

		// text is deleted if the extra offset from the similarity is
		// negative, i.e., doc2 is shorter
		bool bDel = (iOffset2Diff - iOffset2 < 0);
		UT_uint32 iLen;

		if(bDel)
		{
			iLen = pos1 > pos1Diff ? pos1 - pos1Diff : pos1Diff - pos1;
		}
		else
		{
			// need to use the coords of the second doc for calculations
			iLen = pos1+iOffset2 > pos1Diff+iOffset2Diff ? pos1+iOffset2-pos1Diff-iOffset2Diff :
				pos1Diff+iOffset2Diff-pos1-iOffset2;
		}
	
		PD_DocumentDiff * pDiff = new PD_DocumentDiff(bDel, pos1, pos1 + iOffset2, iLen);
		vDiff.addItem((void*)pDiff);

#ifdef DEBUG
		pDiff->_dump();
#endif

		// skip over the known length
		pos1 = pos1Diff + iKnownLength;
		iOffset2 = iOffset2Diff;
	}
}


/*!
    Starting to search this document at position pos where the two
    documents are known to become different, attepts to find location
    at which similarities resume

    \param pos: when called, should contain offset in present document
                at which the difference starts; on successfult return
                it will contain offset in present document where
                similarities resume

    \param iOffset2: when called contains offset to be added to
                     position pos in order to correctly position start
                     of the search in document d; on return it
                     contains offset to be add to pos in order to
                     obtain correct location of the resumption of
                     similarites in document d

   \param iKnownLength: on return contains the minium guaranteed length of the similarity

   \param d the document to which this document is to be compared

   \return returns true if it succeeds; if no further similarities are
           found returns false
*/
bool PD_Document::findWhereSimilarityResumes(PT_DocPosition &pos, UT_sint32 &iOffset2,
											 UT_uint32 & iKnownLength,
											 const PD_Document &d) const
{
	UT_return_val_if_fail(m_pPieceTable || d.m_pPieceTable, true);

	if(m_pPieceTable->getFragments().areFragsDirty())
		m_pPieceTable->getFragments().cleanFrags();
	
	if(d.m_pPieceTable->getFragments().areFragsDirty())
		d.m_pPieceTable->getFragments().cleanFrags();
		
	//  scroll through the documents comparing contents
	PD_DocIterator t1(*this, pos);
	PD_DocIterator t2(d, pos + iOffset2);

	// first, let's assume that the difference is an insertion in doc
	// 2; we will take a few chars from doc 1 and try to locate them
	// in doc 2

	// this is a similarity threshold, very arbitrary ...  if we match
	// iTry chars we will be happy if we do not match at least
	// iMinOverlap we will give up. We will use variable step
	UT_sint32 iTry = 128; 
	UT_sint32 iMinOverlap = 3;
	UT_sint32 iStep = 128;
	UT_sint32 i = 0;

	UT_uint32 iFoundPos1 = 0;
	UT_uint32 iFoundPos2 = 0;
	UT_sint32 iFoundOffset1 = 0;
	UT_sint32 iFoundOffset2 = 0;

	for(i = iTry; i >= iMinOverlap; i -= iStep)
	{
		UT_uint32 pos1 = t1.getPosition();
		UT_uint32 pos2 = t2.getPosition();

		UT_uint32 iPos = t2.find(t1,i,true);

		if(t2.getStatus() == UTIter_OK)
		{
			// we found what we were looking for
			iFoundPos1 = pos1;
			iFoundOffset1 = iPos - iFoundPos1;
			break;
		}
		else
		{
			// we did not find our text, reset position ...
			t2.setPosition(pos2);
			t1.setPosition(pos1);
			
			if(iStep > 1)
				iStep /= 2;
		}
	}

	// remember the length we found ...
	UT_sint32 iLen1 = i >= iMinOverlap ? i : 0;

	if(i == iTry)
	{
		// we found the whole iTry chunk, we will stop here ...
		pos = iFoundPos1;
		iOffset2 = iFoundOffset1;
		iKnownLength = iTry;
		return true;
	}
	
	// now do the same, but assuming our text is deleted from doc 2
	t2.setPosition(pos);
	t1.setPosition(pos + iOffset2);
	iStep = 128;
	
	for(i = iTry; i >= iMinOverlap; i -= iStep)
	{
		UT_uint32 pos1 = t1.getPosition();
		UT_uint32 pos2 = t2.getPosition();

		UT_uint32 iPos = t1.find(t2,i,true);

		if(t1.getStatus() == UTIter_OK)
		{
			// we found what we were looking for
			iFoundPos2 = iPos;
			iFoundOffset2 = pos2 - iFoundPos2;
			break;
		}
		else
		{
			// we did not find our text, reset position ...
			t2.setPosition(pos2);
			t1.setPosition(pos1);

			if(iStep > 1)
				iStep /= 2;
		}
	}

	UT_sint32 iLen2 = i >= iMinOverlap ? i : 0;

	if( !iLen1 && !iLen2)
		return false;
	
	// now we will go with whatever is longer
	if(iLen1 >= iLen2)
	{
		pos = iFoundPos1;
		iOffset2 = iFoundOffset1;
		iKnownLength = iLen1;
	}
	else
	{
		pos = iFoundPos2;
		iOffset2 = iFoundOffset2;
		iKnownLength = iLen2;
	}
	
	return true;
}


/*!
    finds the position of the first difference in content between this
    document and document d, starting search at given position

    \param pos when called this variable should contian document
               offset at which to start searching; on success this
               variable will contain offset of the difference in
               present document

    \param iOffset2 when called contains offset to be added to pos to
                    locate identical position in document d

    \param d   the document to compare with

    \return    returns false if no difference was found, true otherwise
*/
bool PD_Document::findFirstDifferenceInContent(PT_DocPosition &pos, UT_sint32 &iOffset2,
											   const PD_Document &d) const
{
	UT_return_val_if_fail(m_pPieceTable || d.m_pPieceTable, true);

	if(m_pPieceTable->getFragments().areFragsDirty())
		m_pPieceTable->getFragments().cleanFrags();
	
	if(d.m_pPieceTable->getFragments().areFragsDirty())
		d.m_pPieceTable->getFragments().cleanFrags();
		
	//  scroll through the documents comparing contents
	PD_DocIterator t1(*this, pos);
	PD_DocIterator t2(d, pos + iOffset2);

	while(t1.getStatus() == UTIter_OK && t2.getStatus() == UTIter_OK)
	{
		const pf_Frag * pf1 = t1.getFrag();
		const pf_Frag * pf2 = t2.getFrag();

		if(!pf1 || !pf2)
		{
			UT_ASSERT_HARMLESS( UT_SHOULD_NOT_HAPPEN );
			return true;
		}
		
		if(pf1->getType() != pf2->getType())
		{
			pos = pf1->getPos();
			return true;
		}
		
		UT_uint32 iFOffset1 = t1.getPosition() - pf1->getPos();
		UT_uint32 iFOffset2 = t2.getPosition() - pf2->getPos();
		
		UT_uint32 iLen1 = pf1->getLength() - iFOffset1;
		UT_uint32 iLen2 = pf2->getLength() - iFOffset2;
		UT_uint32 iLen  = UT_MIN(iLen1, iLen2);

		if(   iLen1 == iLen2 && iFOffset1 == 0 && iFOffset2 == 0
		   && pf1->getType() != pf_Frag::PFT_Text)
		{
			// completely overlapping non-text frags ..
			if(!(pf1->isContentEqual(*pf2)))
			{
				pos = pf1->getPos();
				return true;
			}
		}
		else if(pf1->getType() != pf_Frag::PFT_Text)
		{
			// partially overlapping frags and not text
			pos = pf1->getPos();
			return true;
		}
		else
		{
			// we have two textual frags that overlap
			// work our way along the overlap ...
			for(UT_uint32 i = 0; i < iLen; ++i)
			{
				if(t1.getChar() != t2.getChar())
				{
					pos = t1.getPosition();
					return true;
				}
				
				++t1;
				++t2;
			}

			// we are already past the end of the shorter frag
			continue;
		}

		// advance both iterators by the processed length
		t1 += iLen;
		t2 += iLen;
	}

	if(t1.getStatus() == UTIter_OK && t2.getStatus() != UTIter_OK)
	{
		// document two is shorter ...
		pos = t1.getPosition();
		return true;
	}

	if(t1.getStatus() != UTIter_OK && t2.getStatus() == UTIter_OK)
	{
		// document 1 is shorter
		pos = t2.getPosition() - iOffset2;
		return true;
	}

	// if we got this far, we found no differences at all ...
	return false;
}

void PD_Document::setAutoRevisioning(bool autorev)
{
	AD_Document::setAutoRevisioning(autorev);

	// TODO tell our listeners to redo layout ...
	signalListeners(PD_SIGNAL_REFORMAT_LAYOUT);	
}



/*!
    Returns true if the contents of the two documents are identical
    if the function returns false, pos contains the document position
    at which first difference was encountered
*/
bool PD_Document::areDocumentContentsEqual(const AD_Document &D, UT_uint32 &pos) const
{
	pos = 0;
	if(D.getType() != ADDOCUMENT_ABIWORD)
		return false;
	
	PD_Document &d = (PD_Document &)D;
	UT_return_val_if_fail(m_pPieceTable || d.m_pPieceTable, false);

	if(m_pPieceTable->getFragments().areFragsDirty())
		m_pPieceTable->getFragments().cleanFrags();
	
	if(d.m_pPieceTable->getFragments().areFragsDirty())
		d.m_pPieceTable->getFragments().cleanFrags();
		
	// test the docs for length
	UT_uint32 end1, end2;

	pf_Frag * pf = m_pPieceTable->getFragments().getLast();

	UT_return_val_if_fail(pf,false);
		
	end1 = pf->getPos() + pf->getLength();
	
	pf = d.m_pPieceTable->getFragments().getLast();

	UT_return_val_if_fail(pf,false);
		
	end2 = pf->getPos() + pf->getLength();

	if(end1 != end2)
	{
		pos = UT_MIN(end1, end2);
		return false;
	}
	
	
	//  scroll through the documents comparing contents
	PD_DocIterator t1(*this);
	PD_DocIterator t2(d);

	while(t1.getStatus() == UTIter_OK && t2.getStatus() == UTIter_OK)
	{
		const pf_Frag * pf1 = t1.getFrag();
		const pf_Frag * pf2 = t2.getFrag();

		if(!pf1)
		{
			if(pf2)
				pos = pf2->getPos();
			else
				pos = 0;
			
			return false;
		}

		if(!pf2)
		{
			pos = pf1->getPos();
			return false;
		}
		

		if(pf1->getType() != pf2->getType())
		{
			pos = pf1->getPos();
			return false;
		}
		

		UT_uint32 iFOffset1 = t1.getPosition() - pf1->getPos();
		UT_uint32 iFOffset2 = t2.getPosition() - pf2->getPos();
		
		UT_uint32 iLen1 = pf1->getLength() - iFOffset1;
		UT_uint32 iLen2 = pf2->getLength() - iFOffset2;
		UT_uint32 iLen  = UT_MIN(iLen1, iLen2);

		if(iLen1 == iLen2 && iFOffset1 == 0 && iFOffset2 == 0)
		{
			// these two frags overlap exactly, so we can just use the
			// pf_Frag::isContentEqual() on them
			if(!(pf1->isContentEqual(*pf2)))
			{
				// TODO -- this is not position of the difference, but
				// of the start of the fragment (there difference
				// could be inside)
				pos = pf1->getPos();
				return false;
			}
			
		}
		else if(pf1->getType() != pf_Frag::PFT_Text)
		{
			// partially overlapping frags and not text
			pos = pf1->getPos();
			return false;
		}
		else
		{
			// we have two textual frags that overlap
			// work our way along the overlap ...
			for(UT_uint32 i = 0; i < iLen; ++i)
			{
				if(t1.getChar() != t2.getChar())
				{
					pos = t1.getPosition() + i;
					return false;
				}
				

				++t1;
				++t2;
			}

			// we are already past the end of the shorter frag
			continue;
		}

		// advance both iterators by the processed length
		t1 += iLen;
		t2 += iLen;
	}

	if((t1.getStatus() == UTIter_OK && t2.getStatus() != UTIter_OK))
	{
		// documents are of different length ...
		pos = t1.getPosition();
		return false;
	}

	if((t1.getStatus() != UTIter_OK && t2.getStatus() == UTIter_OK))
	{
		// documents are of different length ...
		pos = t2.getPosition();
		return false;
	}

	return true;
}

/*!
    Compare the format of the this document to another document;
    returns true if document formats are identical
    
    If the function returns false, pos contains the document position
    at which first difference was encountered

    NB: If the document contents are known not to be equal, it makes no
    sense to call this function.
*/
bool PD_Document::areDocumentFormatsEqual(const AD_Document &D, UT_uint32 &pos) const
{
	pos = 0;
	if(D.getType() != ADDOCUMENT_ABIWORD)
		return false;
	
	PD_Document &d = (PD_Document &)D;
	UT_return_val_if_fail(m_pPieceTable || d.m_pPieceTable, false);

	if(m_pPieceTable->getFragments().areFragsDirty())
		m_pPieceTable->getFragments().cleanFrags();
	
	if(d.m_pPieceTable->getFragments().areFragsDirty())
		d.m_pPieceTable->getFragments().cleanFrags();
		
	//  scroll through the documents comparing fmt
	PD_DocIterator t1(*this);
	PD_DocIterator t2(d);
		
	// in order to avoid repeated comparions of AP, we will store
	// record of matching AP's
	UT_StringPtrMap hFmtMap;
	
	while(t1.getStatus() == UTIter_OK && t2.getStatus() == UTIter_OK)
	{
		// need to cmp contents
		const pf_Frag * pf1 = t1.getFrag();
		const pf_Frag * pf2 = t2.getFrag();

		UT_return_val_if_fail(pf1 && pf2, false);

		PT_AttrPropIndex ap1 = pf1->getIndexAP();
		PT_AttrPropIndex ap2 = pf2->getIndexAP();

		// because the indexes are into different piecetables, we
		// have to expand them
		const PP_AttrProp * pAP1;
		const PP_AttrProp * pAP2;

		m_pPieceTable->getAttrProp(ap1, &pAP1);
		d.m_pPieceTable->getAttrProp(ap2, &pAP2);

		UT_return_val_if_fail(pAP1 && pAP2, false);

		UT_String s;
		UT_String_sprintf(s,"%08x%08x", ap1, ap2);
		bool bAreSame = hFmtMap.contains(s,NULL);
		
		if(!bAreSame)
		{
			if(!pAP1->isEquivalent(pAP2))
			{
				pos = t1.getPosition();
				return false;
			}
			else
			{
				hFmtMap.insert(s,NULL);
			}
		}
		
		UT_uint32 iLen = UT_MIN(pf1->getLength(),pf2->getLength());
		t1 += iLen;
		t2 += iLen;
	}

	if((t1.getStatus() == UTIter_OK && t2.getStatus() != UTIter_OK))
	{
		// documents are of different length ...
		pos = t1.getPosition();
		return false;
	}

	if((t1.getStatus() != UTIter_OK && t2.getStatus() == UTIter_OK))
	{
		// documents are of different length ...
		pos = t2.getPosition();
		return false;
	}

	return true;
}

void PD_Document::setMarkRevisions(bool bMark)
{
	if(isMarkRevisions() != bMark)
	{
		AD_Document::setMarkRevisions(bMark);
	 	signalListeners(PD_SIGNAL_REVISION_MODE_CHANGED);
	}
}

/*!
    This function crawls over the entire document and removes all fmt marks. It is principally meant
    to do PT clean up on import (for example, the structure of MS Word documents is such that we end
    up with a myriad of superfluous fmt marks in the document), and should be called by an importer
    at the end of import process.
*/
bool PD_Document::purgeFmtMarks()
{
	return m_pPieceTable->purgeFmtMarks();
}


bool PD_Document::getAttrProp(PT_AttrPropIndex apIndx, const PP_AttrProp ** ppAP, PP_RevisionAttr ** pRevisions,
							  bool bShowRevisions, UT_uint32 iRevisionId, bool &bHiddenRevision) const
{
	bool bRevisionAttrNeeded = pRevisions ? true : false;
	PP_RevisionAttr * pRevAttr = NULL;
	bHiddenRevision = false;

	const PP_AttrProp * pAP = NULL;

	if(!getAttrProp(apIndx,&pAP))
		return false;

	if(   pAP->getRevisedIndex() != 0xffffffff
	   && pAP->getRevisionState().isEqual(iRevisionId, bShowRevisions, isMarkRevisions()))
	{
		// the revision has a valid index to an inflated AP, so we use it
		bHiddenRevision = pAP->getRevisionHidden();

		const XML_Char* pRevision = NULL;

		if(bRevisionAttrNeeded && pAP->getAttribute("revision", pRevision))
		{
			*pRevisions = new PP_RevisionAttr(pRevision);
			UT_return_val_if_fail(pRevisions, false);
		}

		PT_AttrPropIndex revAPI = pAP->getRevisedIndex();

		getAttrProp(revAPI, ppAP);
		return true;
	}
	
	const PP_AttrProp * pNewAP = explodeRevisions(pRevAttr, pAP, bShowRevisions, iRevisionId, bHiddenRevision);

	if(pNewAP)
	{
		*ppAP = pNewAP;
	}
	else
	{
		*ppAP = pAP;
	}
	
	if(bRevisionAttrNeeded)
	{
		*pRevisions = pRevAttr;
	}
	else
	{
		delete pRevAttr;
	}
	
	return true;
}


/*!
    retrieves span AP corresponding to revision settings

    pRevisions : [out] the representation of the rev. attribute associated with the AP; if
    the caller does not need this, the pointer can be set to null
*/
bool PD_Document::getSpanAttrProp(PL_StruxDocHandle sdh, UT_uint32 offset, bool bLeftSide,
								  const PP_AttrProp ** ppAP,
								  PP_RevisionAttr ** pRevisions,
								  bool bShowRevisions, UT_uint32 iRevisionId,
								  bool &bHiddenRevision) const
{
	const PP_AttrProp *pAP = NULL;
	bool bRevisionAttrNeeded = pRevisions ? true : false;
	PP_RevisionAttr * pRevAttr = NULL;
	
	if(!getSpanAttrProp(sdh,offset,bLeftSide,&pAP))
		return false;

	if(   pAP->getRevisedIndex() != 0xffffffff
	   && pAP->getRevisionState().isEqual(iRevisionId, bShowRevisions, isMarkRevisions()))
	{
		// the revision has a valid index to an inflated AP, so we use it
		bHiddenRevision = pAP->getRevisionHidden();

		const XML_Char* pRevision = NULL;

		// only do this if the pRevisions pointer is set to NULL
		if(bRevisionAttrNeeded && pAP->getAttribute("revision", pRevision))
		{
			*pRevisions = new PP_RevisionAttr(pRevision);
			UT_return_val_if_fail(pRevisions, false);
		}
	
		PT_AttrPropIndex revAPI = pAP->getRevisedIndex();

		getAttrProp(revAPI, ppAP);
		return true;
	}
	
	const PP_AttrProp * pNewAP = explodeRevisions(pRevAttr, pAP, bShowRevisions, iRevisionId, bHiddenRevision);

	if(pNewAP)
	{
		*ppAP = pNewAP;
	}
	else
	{
		*ppAP = pAP;
	}

	if(bRevisionAttrNeeded)
	{
		*pRevisions = pRevAttr;
	}
	else
	{
		delete pRevAttr;
	}
	
	
	return true;
}

void PD_Document::_clearUndo()
{
	UT_return_if_fail(m_pPieceTable);
	m_pPieceTable->clearUndo();
}
	
void PD_Document::tellPTDoNotTweakPosition(bool b)
{
	UT_return_if_fail( m_pPieceTable );
	m_pPieceTable->setDoNotTweakPosition(b);
}

UT_uint32 PD_Document::getXID()
{
	return m_pPieceTable->getXID();
}

UT_uint32 PD_Document::getTopXID() const
{
	return m_pPieceTable->getTopXID();
}

void PD_Document::fixMissingXIDs()
{
	m_pPieceTable->fixMissingXIDs();
}

/*!
    This function evaluates the xid value for the given frament and version level.

    The XID is a document-unique identifier of the frag; when we compare documents, we are
    interested not in document uniqueness but global uniqueness. We convert the
    document-unique xid to a globaly unique id by combining the xid with the UUID of
    document version: identical xid's in two documents represent identical elements if,
    and only if, the version UUIDs for the version of the document in which the element
    was created are identical. Therefore, as a part of the version record, we store the
    highest xid used in the document. This way we can determine in which version of the
    document the frag was created, based on its xid.

    Frags that have xid aboved the version threshold need to be treated as frags without xid.
*/
UT_uint32 PD_Document::getFragXIDforVersion(const pf_Frag * pf, UT_uint32 iVersion) const
{
	UT_return_val_if_fail( pf, 0 );

	if(iVersion >= getDocVersion())
	{
		// all xid's valid
		return pf->getXID();
	}
	
	const AD_VersionData * v = findHistoryRecord(iVersion);

	if(!v)
	{
		// if there is no version record for this version, find the nearest lower version
		for(UT_sint32 i = (UT_sint32)iVersion - 1; i > 0; --i)
		{
			v = findHistoryRecord(i);
			if(v)
				break;
		}

		if(!v)
			return 0;
	}
	

	UT_uint32 iXid = pf->getXID();

	if(iXid <= v->getTopXID())
		return iXid;

	// this frag's xid is above the version limit, i.e., this frag was inserted in a later
	// version of the document, and its xid cannot be used in document matching for the
	// given version level
	return 0;
}


#ifdef DEBUG
void PD_DocumentDiff::_dump() const
{
	UT_DEBUGMSG(("PD_DocumentDiff: del=%d, p1=%d, p2=%d, len=%d\n",
				 m_bDeleted, m_pos1, m_pos2, m_len));
}
#endif
