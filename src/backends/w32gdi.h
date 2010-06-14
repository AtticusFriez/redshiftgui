/* w32gdi.h -- Windows GDI gamma adjustment header
   This file is part of Redshift.

   Redshift is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Redshift is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

   Copyright (c) 2010  Jon Lund Steffensen <jonlst@gmail.com>
*/

#ifndef _REDSHIFT_W32GDI_H
#define _REDSHIFT_W32GDI_H

#define WINVER  0x0500
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wingdi.h>

typedef struct {
	HDC hDC;
	WORD *saved_ramps;
} w32gdi_state_t;

int w32gdi_init(w32gdi_state_t *state);
void w32gdi_free(w32gdi_state_t *state);
void w32gdi_restore(w32gdi_state_t *state);
int w32gdi_set_temperature(w32gdi_state_t *state, int temp, gamma_s gamma);

#endif /* ! _REDSHIFT_W32GDI_H */
