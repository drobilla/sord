/*
  Copyright 2011 David Robillard <http://drobilla.net>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
  AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef SORD_SORD_INTERNAL_H_
#define SORD_SORD_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include "sord/sord.h"

typedef intptr_t SordCount;  ///< Count of nodes or triples

/** Node */
struct _SordNode {
	SordNodeType type;      ///< SordNodeType
	size_t       n_bytes;   ///< Length of data in bytes (including terminator)
	SordCount    refs;      ///< Reference count (i.e. number of containing quads)
	SordNode     datatype;  ///< Literal data type (ID of a URI node, or 0)
	const char*  lang;      ///< Literal language (interned string)
	uint8_t*     buf;       ///< Value (string)
};

#endif /* SORD_SORD_INTERNAL_H_ */
