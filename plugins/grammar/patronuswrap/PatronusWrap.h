/* AbiWord
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

#ifndef __PatronusWrap_h__
#define __PatronusWrap_h__

extern "C" {
#include <patronus/patronus.h>
}

class PieceOfText;

class PatronusWrap {
public:
	PatronusWrap(void);
	virtual ~PatronusWrap(void);
	bool parseSentence(PieceOfText * pT);
	bool clear(void);
private:
	Patronus* m_Checker;
	PatronusProperties m_Properties;
};

#endif // __PatronusWrap_h__
