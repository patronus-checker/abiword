/* AbiWord
 * Copyright (C) Ben Martin 2012.
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

#ifndef AP_RDFLOCATION_H
#define AP_RDFLOCATION_H

#include "xap_Types.h"
#include "pd_DocumentRDF.h"

class ABI_EXPORT AP_RDFLocation : public PD_RDFLocation
{
  public:
    AP_RDFLocation( PD_DocumentRDFHandle rdf, PD_ResultBindings_t::iterator& it, bool isGeo84 = false );
    virtual ~AP_RDFLocation();
};


#endif
