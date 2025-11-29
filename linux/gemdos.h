/*
 * gemdos.h - GEMDOS drive implementation
 *
 * Copyright (c) 2025 Francois Galea <fgalea at free.fr>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __GEMDOS_H__
#define __GEMDOS_H__

void gemdos_acsi_cmd(void);

void gemdos_stub_call(void);

void gemdos_init(void);

void gemdos_exit(void);

#endif
