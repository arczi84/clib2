/*
 * $Id: amiga_acrypt.c,v 1.1.1.1 2004-07-26 16:30:16 obarthel Exp $
 *
 * :ts=4
 *
 * Portable ISO 'C' (1994) runtime library for the Amiga computer
 * Copyright (c) 2002-2004 by Olaf Barthel <olsen@sourcery.han.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   - Neither the name of Olaf Barthel nor the names of contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <exec/types.h>
#include <string.h>
#include <clib/alib_protos.h>

/****************************************************************************/

#include "debug.h"

/****************************************************************************/

#define OSIZE 12

/****************************************************************************/

UBYTE *
ACrypt(UBYTE * buffer, const UBYTE * password, const UBYTE * user)
{
	UBYTE * result = NULL;
	LONG buf[OSIZE];
	LONG i,d,k;

	ENTER();

	assert( buffer != NULL && password != NULL && user != NULL );

	SHOWPOINTER(buffer);
	SHOWSTRING(password);
	SHOWSTRING(user);

	if(buffer == NULL || password == NULL || user == NULL)
	{
		SHOWMSG("invalid parameters");
		goto out;
	}

	for(i = 0 ; i < OSIZE ; i++)
	{
		if((*password) != '\0')
			d = (*password++);
		else
			d = i;

		if((*user) != '\0')
			d += (*user++);
		else
			d += i;

		buf[i] = 'A' + d;
	}

	for(i = 0 ; i < OSIZE ; i++)
	{
		for(k = 0 ; k < OSIZE ; k++)
			buf[i] = (buf[i] + buf[OSIZE - k - 1]) % 53;

		buffer[i] = buf[i] + 'A';
	}

	buffer[OSIZE-1] = '\0';

	SHOWSTRING(buffer);

	result = buffer;

 out:

	RETURN(result);
	return(result);
}