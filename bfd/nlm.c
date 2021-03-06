/* NLM (NetWare Loadable Module) executable support for BFD.
   Copyright (C) 1993 Free Software Foundation, Inc.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "bfd.h"
#include "sysdep.h"
#include "libbfd.h"
#include "libnlm.h"

/* Make an NLM object.  We just need to allocate the backend
   information.  */

boolean
nlm_mkobject (abfd)
     bfd * abfd;
{
  nlm_tdata (abfd) =
    (struct nlm_obj_tdata *) bfd_zalloc (abfd, sizeof (struct nlm_obj_tdata));
  if (nlm_tdata (abfd) == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      return (false);
    }

  if (nlm_architecture (abfd) != bfd_arch_unknown)
    bfd_default_set_arch_mach (abfd, nlm_architecture (abfd),
			       nlm_machine (abfd));

  /* since everything is done at close time, do we need any initialization? */
  return (true);
}

/* Set the architecture and machine for an NLM object.  */

boolean
nlm_set_arch_mach (abfd, arch, machine)
     bfd * abfd;
     enum bfd_architecture arch;
     unsigned long machine;
{
  bfd_default_set_arch_mach (abfd, arch, machine);
  return arch == nlm_architecture (abfd);
}
