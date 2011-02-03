/* Sord, a lightweight RDF model library.
 * Copyright 2010-2011 David Robillard <d@drobilla.net>
 *
 * Sord is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sord is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SORD_INTERNAL_H
#define SORD_INTERNAL_H

#include "sord/sord.h"

/** Node */
struct _SordNode {
	SordNodeType type;       ///< SordNodeType
	size_t       n_bytes;    ///< Length of data in bytes (including terminator)
	SordCount    refs;       ///< Reference count (i.e. number of containing tuples)
	void*        user_data;  ///< Opaque user data
	SordNode     datatype;   ///< Literal data type (ID of a URI node, or 0)
	const char*  lang;       ///< Literal language (interned string)
	char*        buf;        ///< Value (string)
};

#endif // SORD_INTERNAL_H
