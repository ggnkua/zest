/*
 * acsi.h - ACSI interface and hard disk drive emulation (software part)
 *
 * Copyright (c) 2023-2025 Francois Galea <fgalea at free.fr>
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

#ifndef __ACSI_H__
#define __ACSI_H__

extern uint8_t acsi_command[10];

void acsi_init(volatile uint32_t *parmreg);

void acsi_exit(void);

void acsi_interrupt(void);

void hdd_changeimg(int acsi_id, const char *full_pathname);

#endif
