/*
 * Copyright (C) 2005 Martin Sevior <msevior@physics.unimelb.edu.au>
 * Copyright (C) 2017 Jan Tojnar <jtojnar@mail.muni.cz>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include "config.h"
#include "xap_App.h"
#include "ut_locale.h"
#include "ut_string_class.h"
#include "ut_types.h"
#include "ut_debugmsg.h"
#include "ut_vector.h"
#include "ut_string.h"
#include "../xp/AbiGrammarUtil.h"

#include "PatronusWrap.h"

PatronusWrap::PatronusWrap(void)
{
	UT_LocaleTransactor t(LC_ALL, "");
	m_Checker = patronus_create();
	m_Properties = {};
	m_Properties.primary_language = "en";
}

PatronusWrap::~PatronusWrap(void)
{
	if(m_Checker)
		patronus_free(m_Checker);
}

bool PatronusWrap::parseSentence(PieceOfText * pT)
{
	if(!m_Checker)
	{
		UT_DEBUGMSG(("Missing Patronus instance!!\n"));
		return true; // default to no grammar checking.
	}

	PatronusAnnotationArray* annotations = patronus_check(m_Checker, &m_Properties, const_cast<char *>(pT->sText.utf8_str()));

	bool res = annotations->len == 0;

	for (UT_uint32 i=0; i < annotations->len; i++)
	{
		PatronusAnnotation annotation = annotations->data[i];
		AbiGrammarError* pErr = new AbiGrammarError();
		pErr->m_iErrLow = pT->iInLow + annotation.offset;
		pErr->m_iErrHigh = pErr->m_iErrLow + annotation.length - 1;
		pErr->m_sErrorDesc = annotation.message;
		pT->m_vecGrammarErrors.addItem(pErr);
	}

	pT->m_bGrammarChecked = true;
	pT->m_bGrammarOK = res;

	patronus_free_annotations(annotations);

	return res;
}

bool PatronusWrap::clear(void)
{
	return true;
}
