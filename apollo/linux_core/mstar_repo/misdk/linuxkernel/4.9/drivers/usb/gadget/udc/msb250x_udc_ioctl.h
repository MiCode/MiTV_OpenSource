///////////////////////////////////////////////////////////////////////////////////////////////////
//
// * Copyright (c) 2006 - 2017 MStar Semiconductor, Inc.
// This program is free software.
// You can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program;
// if not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
///////////////////////////////////////////////////////////////////////////////////////////////////

/*------------------------------------------------------------------------------
    PROJECT: MSB250x Linux BSP
    DESCRIPTION:
          MSB250x dual role USB device controllers


    HISTORY:
         6/11/2010     Calvin Hung    First Revision

-------------------------------------------------------------------------------*/
#ifndef _MSB250X_UDC_IOCTL_H
#define _MSB250X_UDC_IOCTL_H
/*
 * Ioctl definitions
 */

/* Use 'C' as magic number */
#define MSB250X_UDC_IOC_MAGIC  'C'

#define MSB250X_UDC_CONN_CHG _IOR(MSB250X_UDC_IOC_MAGIC, 0, int) /* Get connection change status. */
#define MSB250X_UDC_SET_CONN _IOW(MSB250X_UDC_IOC_MAGIC, 1, int)
#define MSB250X_UDC_GET_CONN _IOR(MSB250X_UDC_IOC_MAGIC, 2, int)
#define MSB250X_UDC_GET_LINESTAT _IOR(MSB250X_UDC_IOC_MAGIC, 3, int)

#define MSB250X_UDC_IOC_MAXNR 14

#endif /* _MSB250X_UDC_IOCTL_H */
