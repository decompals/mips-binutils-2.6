/* ELF linker support.
   Copyright 1995 Free Software Foundation, Inc.

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

/* ELF linker code.  */

static boolean elf_link_add_object_symbols
  PARAMS ((bfd *, struct bfd_link_info *));
static boolean elf_link_add_archive_symbols
  PARAMS ((bfd *, struct bfd_link_info *));
static Elf_Internal_Rela *elf_link_read_relocs
  PARAMS ((bfd *, asection *, PTR, Elf_Internal_Rela *, boolean));
static boolean elf_export_symbol
  PARAMS ((struct elf_link_hash_entry *, PTR));
static boolean elf_adjust_dynamic_symbol
  PARAMS ((struct elf_link_hash_entry *, PTR));

/* This struct is used to pass information to routines called via
   elf_link_hash_traverse which must return failure.  */

struct elf_info_failed
{
  boolean failed;
  struct bfd_link_info *info;
};  

/* Given an ELF BFD, add symbols to the global hash table as
   appropriate.  */

boolean
elf_bfd_link_add_symbols (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  switch (bfd_get_format (abfd))
    {
    case bfd_object:
      return elf_link_add_object_symbols (abfd, info);
    case bfd_archive:
      return elf_link_add_archive_symbols (abfd, info);
    default:
      bfd_set_error (bfd_error_wrong_format);
      return false;
    }
}

/* Add symbols from an ELF archive file to the linker hash table.  We
   don't use _bfd_generic_link_add_archive_symbols because of a
   problem which arises on UnixWare.  The UnixWare libc.so is an
   archive which includes an entry libc.so.1 which defines a bunch of
   symbols.  The libc.so archive also includes a number of other
   object files, which also define symbols, some of which are the same
   as those defined in libc.so.1.  Correct linking requires that we
   consider each object file in turn, and include it if it defines any
   symbols we need.  _bfd_generic_link_add_archive_symbols does not do
   this; it looks through the list of undefined symbols, and includes
   any object file which defines them.  When this algorithm is used on
   UnixWare, it winds up pulling in libc.so.1 early and defining a
   bunch of symbols.  This means that some of the other objects in the
   archive are not included in the link, which is incorrect since they
   precede libc.so.1 in the archive.

   Fortunately, ELF archive handling is simpler than that done by
   _bfd_generic_link_add_archive_symbols, which has to allow for a.out
   oddities.  In ELF, if we find a symbol in the archive map, and the
   symbol is currently undefined, we know that we must pull in that
   object file.

   Unfortunately, we do have to make multiple passes over the symbol
   table until nothing further is resolved.  */

static boolean
elf_link_add_archive_symbols (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  symindex c;
  boolean *defined = NULL;
  boolean *included = NULL;
  carsym *symdefs;
  boolean loop;

  if (! bfd_has_map (abfd))
    {
      /* An empty archive is a special case.  */
      if (bfd_openr_next_archived_file (abfd, (bfd *) NULL) == NULL)
	return true;
      bfd_set_error (bfd_error_no_armap);
      return false;
    }

  /* Keep track of all symbols we know to be already defined, and all
     files we know to be already included.  This is to speed up the
     second and subsequent passes.  */
  c = bfd_ardata (abfd)->symdef_count;
  if (c == 0)
    return true;
  defined = (boolean *) malloc (c * sizeof (boolean));
  included = (boolean *) malloc (c * sizeof (boolean));
  if (defined == (boolean *) NULL || included == (boolean *) NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      goto error_return;
    }
  memset (defined, 0, c * sizeof (boolean));
  memset (included, 0, c * sizeof (boolean));

  symdefs = bfd_ardata (abfd)->symdefs;

  do
    {
      file_ptr last;
      symindex i;
      carsym *symdef;
      carsym *symdefend;

      loop = false;
      last = -1;

      symdef = symdefs;
      symdefend = symdef + c;
      for (i = 0; symdef < symdefend; symdef++, i++)
	{
	  struct elf_link_hash_entry *h;
	  bfd *element;
	  struct bfd_link_hash_entry *undefs_tail;
	  symindex mark;

	  if (defined[i] || included[i])
	    continue;
	  if (symdef->file_offset == last)
	    {
	      included[i] = true;
	      continue;
	    }

	  h = elf_link_hash_lookup (elf_hash_table (info), symdef->name,
				    false, false, false);
	  if (h == (struct elf_link_hash_entry *) NULL)
	    continue;
	  if (h->root.type != bfd_link_hash_undefined)
	    {
	      if (h->root.type != bfd_link_hash_undefweak)
		defined[i] = true;
	      continue;
	    }

	  /* We need to include this archive member.  */

	  element = _bfd_get_elt_at_filepos (abfd, symdef->file_offset);
	  if (element == (bfd *) NULL)
	    goto error_return;

	  if (! bfd_check_format (element, bfd_object))
	    goto error_return;

	  /* Doublecheck that we have not included this object
	     already--it should be impossible, but there may be
	     something wrong with the archive.  */
	  if (element->archive_pass != 0)
	    {
	      bfd_set_error (bfd_error_bad_value);
	      goto error_return;
	    }
	  element->archive_pass = 1;

	  undefs_tail = info->hash->undefs_tail;

	  if (! (*info->callbacks->add_archive_element) (info, element,
							 symdef->name))
	    goto error_return;
	  if (! elf_link_add_object_symbols (element, info))
	    goto error_return;

	  /* If there are any new undefined symbols, we need to make
	     another pass through the archive in order to see whether
	     they can be defined.  FIXME: This isn't perfect, because
	     common symbols wind up on undefs_tail and because an
	     undefined symbol which is defined later on in this pass
	     does not require another pass.  This isn't a bug, but it
	     does make the code less efficient than it could be.  */
	  if (undefs_tail != info->hash->undefs_tail)
	    loop = true;

	  /* Look backward to mark all symbols from this object file
	     which we have already seen in this pass.  */
	  mark = i;
	  do
	    {
	      included[mark] = true;
	      if (mark == 0)
		break;
	      --mark;
	    }
	  while (symdefs[mark].file_offset == symdef->file_offset);

	  /* We mark subsequent symbols from this object file as we go
	     on through the loop.  */
	  last = symdef->file_offset;
	}
    }
  while (loop);

  free (defined);
  free (included);

  return true;

 error_return:
  if (defined != (boolean *) NULL)
    free (defined);
  if (included != (boolean *) NULL)
    free (included);
  return false;
}

/* Add symbols from an ELF object file to the linker hash table.  */

static boolean
elf_link_add_object_symbols (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  boolean (*add_symbol_hook) PARAMS ((bfd *, struct bfd_link_info *,
				      const Elf_Internal_Sym *,
				      const char **, flagword *,
				      asection **, bfd_vma *));
  boolean (*check_relocs) PARAMS ((bfd *, struct bfd_link_info *,
				   asection *, const Elf_Internal_Rela *));
  boolean collect;
  Elf_Internal_Shdr *hdr;
  size_t symcount;
  size_t extsymcount;
  size_t extsymoff;
  Elf_External_Sym *buf = NULL;
  struct elf_link_hash_entry **sym_hash;
  boolean dynamic;
  Elf_External_Dyn *dynbuf = NULL;
  struct elf_link_hash_entry *weaks;
  Elf_External_Sym *esym;
  Elf_External_Sym *esymend;

  add_symbol_hook = get_elf_backend_data (abfd)->elf_add_symbol_hook;
  collect = get_elf_backend_data (abfd)->collect;

  /* As a GNU extension, any input sections which are named
     .gnu.warning.SYMBOL are treated as warning symbols for the given
     symbol.  This differs from .gnu.warning sections, which generate
     warnings when they are included in an output file.  */
  if (! info->shared)
    {
      asection *s;

      for (s = abfd->sections; s != NULL; s = s->next)
	{
	  const char *name;

	  name = bfd_get_section_name (abfd, s);
	  if (strncmp (name, ".gnu.warning.", sizeof ".gnu.warning." - 1) == 0)
	    {
	      char *msg;
	      bfd_size_type sz;

	      sz = bfd_section_size (abfd, s);
	      msg = (char *) bfd_alloc (abfd, sz);
	      if (msg == NULL)
		{
		  bfd_set_error (bfd_error_no_memory);
		  goto error_return;
		}

	      if (! bfd_get_section_contents (abfd, s, msg, (file_ptr) 0, sz))
		goto error_return;

	      if (! (_bfd_generic_link_add_one_symbol
		     (info, abfd, 
		      name + sizeof ".gnu.warning." - 1,
		      BSF_WARNING, s, (bfd_vma) 0, msg, false, collect,
		      (struct bfd_link_hash_entry **) NULL)))
		goto error_return;

	      if (! info->relocateable)
		{
		  /* Clobber the section size so that the warning does
                     not get copied into the output file.  */
		  s->_raw_size = 0;
		}
	    }
	}
    }

  /* A stripped shared library might only have a dynamic symbol table,
     not a regular symbol table.  In that case we can still go ahead
     and link using the dynamic symbol table.  */
  if (elf_onesymtab (abfd) == 0
      && elf_dynsymtab (abfd) != 0)
    {
      elf_onesymtab (abfd) = elf_dynsymtab (abfd);
      elf_tdata (abfd)->symtab_hdr = elf_tdata (abfd)->dynsymtab_hdr;
    }

  hdr = &elf_tdata (abfd)->symtab_hdr;
  symcount = hdr->sh_size / sizeof (Elf_External_Sym);

  /* The sh_info field of the symtab header tells us where the
     external symbols start.  We don't care about the local symbols at
     this point.  */
  if (elf_bad_symtab (abfd))
    {
      extsymcount = symcount;
      extsymoff = 0;
    }
  else
    {
      extsymcount = symcount - hdr->sh_info;
      extsymoff = hdr->sh_info;
    }

  buf = (Elf_External_Sym *) malloc (extsymcount * sizeof (Elf_External_Sym));
  if (buf == NULL && extsymcount != 0)
    {
      bfd_set_error (bfd_error_no_memory);
      goto error_return;
    }

  /* We store a pointer to the hash table entry for each external
     symbol.  */
  sym_hash = ((struct elf_link_hash_entry **)
	      bfd_alloc (abfd,
			 extsymcount * sizeof (struct elf_link_hash_entry *)));
  if (sym_hash == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      goto error_return;
    }
  elf_sym_hashes (abfd) = sym_hash;

  if (elf_elfheader (abfd)->e_type != ET_DYN)
    {
      dynamic = false;

      /* If we are creating a shared library, create all the dynamic
         sections immediately.  We need to attach them to something,
         so we attach them to this BFD, provided it is the right
         format.  FIXME: If there are no input BFD's of the same
         format as the output, we can't make a shared library.  */
      if (info->shared
	  && ! elf_hash_table (info)->dynamic_sections_created
	  && abfd->xvec == info->hash->creator)
	{
	  if (! elf_link_create_dynamic_sections (abfd, info))
	    goto error_return;
	}
    }
  else
    {
      asection *s;
      boolean add_needed;
      const char *name;
      bfd_size_type oldsize;
      bfd_size_type strindex;

      dynamic = true;

      /* You can't use -r against a dynamic object.  Also, there's no
	 hope of using a dynamic object which does not exactly match
	 the format of the output file.  */
      if (info->relocateable
	  || info->hash->creator != abfd->xvec)
	{
	  bfd_set_error (bfd_error_invalid_operation);
	  goto error_return;
	}

      /* Find the name to use in a DT_NEEDED entry that refers to this
	 object.  If the object has a DT_SONAME entry, we use it.
	 Otherwise, if the generic linker stuck something in
	 elf_dt_needed_name, we use that.  Otherwise, we just use the
	 file name.  If the generic linker put a null string into
	 elf_dt_needed_name, we don't make a DT_NEEDED entry at all,
	 even if there is a DT_SONAME entry.  */
      add_needed = true;
      name = bfd_get_filename (abfd);
      if (elf_dt_needed_name (abfd) != NULL)
	{
	  name = elf_dt_needed_name (abfd);
	  if (*name == '\0')
	    add_needed = false;
	}
      s = bfd_get_section_by_name (abfd, ".dynamic");
      if (s != NULL)
	{
	  Elf_External_Dyn *extdyn;
	  Elf_External_Dyn *extdynend;
	  int elfsec;
	  unsigned long link;

	  dynbuf = (Elf_External_Dyn *) malloc ((size_t) s->_raw_size);
	  if (dynbuf == NULL)
	    {
	      bfd_set_error (bfd_error_no_memory);
	      goto error_return;
	    }

	  if (! bfd_get_section_contents (abfd, s, (PTR) dynbuf,
					  (file_ptr) 0, s->_raw_size))
	    goto error_return;

	  elfsec = _bfd_elf_section_from_bfd_section (abfd, s);
	  if (elfsec == -1)
	    goto error_return;
	  link = elf_elfsections (abfd)[elfsec]->sh_link;

	  extdyn = dynbuf;
	  extdynend = extdyn + s->_raw_size / sizeof (Elf_External_Dyn);
	  for (; extdyn < extdynend; extdyn++)
	    {
	      Elf_Internal_Dyn dyn;

	      elf_swap_dyn_in (abfd, extdyn, &dyn);
	      if (add_needed && dyn.d_tag == DT_SONAME)
		{
		  name = bfd_elf_string_from_elf_section (abfd, link,
							  dyn.d_un.d_val);
		  if (name == NULL)
		    goto error_return;
		}
	      if (dyn.d_tag == DT_NEEDED)
		{
		  struct bfd_link_needed_list *n, **pn;
		  char *fnm, *anm;

		  n = ((struct bfd_link_needed_list *)
		       bfd_alloc (abfd, sizeof (struct bfd_link_needed_list)));
		  fnm = bfd_elf_string_from_elf_section (abfd, link,
							 dyn.d_un.d_val);
		  if (n == NULL || fnm == NULL)
		    goto error_return;
		  anm = bfd_alloc (abfd, strlen (fnm) + 1);
		  if (anm == NULL)
		    goto error_return;
		  strcpy (anm, fnm);
		  n->name = anm;
		  n->by = abfd;
		  n->next = NULL;
		  for (pn = &elf_hash_table (info)->needed;
		       *pn != NULL;
		       pn = &(*pn)->next)
		    ;
		  *pn = n;
		}
	    }

	  free (dynbuf);
	  dynbuf = NULL;
	}

      /* We do not want to include any of the sections in a dynamic
	 object in the output file.  We hack by simply clobbering the
	 list of sections in the BFD.  This could be handled more
	 cleanly by, say, a new section flag; the existing
	 SEC_NEVER_LOAD flag is not the one we want, because that one
	 still implies that the section takes up space in the output
	 file.  */
      abfd->sections = NULL;

      /* If this is the first dynamic object found in the link, create
	 the special sections required for dynamic linking.  */
      if (! elf_hash_table (info)->dynamic_sections_created)
	{
	  if (! elf_link_create_dynamic_sections (abfd, info))
	    goto error_return;
	}

      if (add_needed)
	{
	  /* Add a DT_NEEDED entry for this dynamic object.  */
	  oldsize = _bfd_stringtab_size (elf_hash_table (info)->dynstr);
	  strindex = _bfd_stringtab_add (elf_hash_table (info)->dynstr, name,
					 true, false);
	  if (strindex == (bfd_size_type) -1)
	    goto error_return;

	  if (oldsize == _bfd_stringtab_size (elf_hash_table (info)->dynstr))
	    {
	      asection *sdyn;
	      Elf_External_Dyn *dyncon, *dynconend;

	      /* The hash table size did not change, which means that
		 the dynamic object name was already entered.  If we
		 have already included this dynamic object in the
		 link, just ignore it.  There is no reason to include
		 a particular dynamic object more than once.  */
	      sdyn = bfd_get_section_by_name (elf_hash_table (info)->dynobj,
					      ".dynamic");
	      BFD_ASSERT (sdyn != NULL);

	      dyncon = (Elf_External_Dyn *) sdyn->contents;
	      dynconend = (Elf_External_Dyn *) (sdyn->contents +
						sdyn->_raw_size);
	      for (; dyncon < dynconend; dyncon++)
		{
		  Elf_Internal_Dyn dyn;

		  elf_swap_dyn_in (elf_hash_table (info)->dynobj, dyncon,
				   &dyn);
		  if (dyn.d_tag == DT_NEEDED
		      && dyn.d_un.d_val == strindex)
		    {
		      if (buf != NULL)
			free (buf);
		      return true;
		    }
		}
	    }

	  if (! elf_add_dynamic_entry (info, DT_NEEDED, strindex))
	    goto error_return;
	}
    }

  if (bfd_seek (abfd,
		hdr->sh_offset + extsymoff * sizeof (Elf_External_Sym),
		SEEK_SET) != 0
      || (bfd_read ((PTR) buf, sizeof (Elf_External_Sym), extsymcount, abfd)
	  != extsymcount * sizeof (Elf_External_Sym)))
    goto error_return;

  weaks = NULL;

  esymend = buf + extsymcount;
  for (esym = buf; esym < esymend; esym++, sym_hash++)
    {
      Elf_Internal_Sym sym;
      int bind;
      bfd_vma value;
      asection *sec;
      flagword flags;
      const char *name;
      struct elf_link_hash_entry *h;
      boolean definition;
      boolean size_change_ok, type_change_ok;
      boolean new_weakdef;

      elf_swap_symbol_in (abfd, esym, &sym);

      flags = BSF_NO_FLAGS;
      sec = NULL;
      value = sym.st_value;
      *sym_hash = NULL;

      bind = ELF_ST_BIND (sym.st_info);
      if (bind == STB_LOCAL)
	{
	  /* This should be impossible, since ELF requires that all
	     global symbols follow all local symbols, and that sh_info
	     point to the first global symbol.  Unfortunatealy, Irix 5
	     screws this up.  */
	  continue;
	}
      else if (bind == STB_GLOBAL)
	{
	  if (sym.st_shndx != SHN_UNDEF
	      && sym.st_shndx != SHN_COMMON)
	    flags = BSF_GLOBAL;
	  else
	    flags = 0;
	}
      else if (bind == STB_WEAK)
	flags = BSF_WEAK;
      else
	{
	  /* Leave it up to the processor backend.  */
	}

      if (sym.st_shndx == SHN_UNDEF)
	sec = bfd_und_section_ptr;
      else if (sym.st_shndx > 0 && sym.st_shndx < SHN_LORESERVE)
	{
	  sec = section_from_elf_index (abfd, sym.st_shndx);
	  if (sec != NULL)
	    value -= sec->vma;
	  else
	    sec = bfd_abs_section_ptr;
	}
      else if (sym.st_shndx == SHN_ABS)
	sec = bfd_abs_section_ptr;
      else if (sym.st_shndx == SHN_COMMON)
	{
	  sec = bfd_com_section_ptr;
	  /* What ELF calls the size we call the value.  What ELF
	     calls the value we call the alignment.  */
	  value = sym.st_size;
	}
      else
	{
	  /* Leave it up to the processor backend.  */
	}

      name = bfd_elf_string_from_elf_section (abfd, hdr->sh_link, sym.st_name);
      if (name == (const char *) NULL)
	goto error_return;

      if (add_symbol_hook)
	{
	  if (! (*add_symbol_hook) (abfd, info, &sym, &name, &flags, &sec,
				    &value))
	    goto error_return;

	  /* The hook function sets the name to NULL if this symbol
	     should be skipped for some reason.  */
	  if (name == (const char *) NULL)
	    continue;
	}

      /* Sanity check that all possibilities were handled.  */
      if (sec == (asection *) NULL)
	{
	  bfd_set_error (bfd_error_bad_value);
	  goto error_return;
	}

      if (bfd_is_und_section (sec)
	  || bfd_is_com_section (sec))
	definition = false;
      else
	definition = true;

      size_change_ok = false;
      type_change_ok = false;
      if (info->hash->creator->flavour == bfd_target_elf_flavour)
	{
	  /* We need to look up the symbol now in order to get some of
	     the dynamic object handling right.  We pass the hash
	     table entry in to _bfd_generic_link_add_one_symbol so
	     that it does not have to look it up again.  */
	  h = elf_link_hash_lookup (elf_hash_table (info), name,
				    true, false, false);
	  if (h == NULL)
	    goto error_return;
	  *sym_hash = h;

	  while (h->root.type == bfd_link_hash_indirect
		 || h->root.type == bfd_link_hash_warning)
	    h = (struct elf_link_hash_entry *) h->root.u.i.link;

	  /* It's OK to change the type if it used to be a weak
             definition.  */
	  type_change_ok = (h->root.type == bfd_link_hash_defweak
			    || h->root.type == bfd_link_hash_undefweak);

	  /* It's OK to change the size if it used to be a weak
	     definition, or if it used to be undefined, or if we will
	     be overriding an old definition.
	     */
	  size_change_ok = (type_change_ok
			    || h->root.type == bfd_link_hash_undefined);

	  /* If we are looking at a dynamic object, and this is a
	     definition, we need to see if it has already been defined
	     by some other object.  If it has, we want to use the
	     existing definition, and we do not want to report a
	     multiple symbol definition error; we do this by
	     clobbering sec to be bfd_und_section_ptr.  */
	  if (dynamic && definition)
	    {
	      if (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak
		  || (h->root.type == bfd_link_hash_common
		      && bind == STB_WEAK))
		{
		  sec = bfd_und_section_ptr;
		  definition = false;
		  size_change_ok = true;
		}
	    }

	  /* Similarly, if we are not looking at a dynamic object, and
	     we have a definition, we want to override any definition
	     we may have from a dynamic object.  Symbols from regular
	     files always take precedence over symbols from dynamic
	     objects, even if they are defined after the dynamic
	     object in the link.  */
	  if (! dynamic
	      && definition
	      && (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak)
	      && (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_DYNAMIC) != 0
	      && (bfd_get_flavour (h->root.u.def.section->owner)
		  == bfd_target_elf_flavour)
	      && (elf_elfheader (h->root.u.def.section->owner)->e_type
		  == ET_DYN))
	    {
	      /* Change the hash table entry to undefined, and let
		 _bfd_generic_link_add_one_symbol do the right thing
		 with the new definition.  */
	      h->root.type = bfd_link_hash_undefined;
	      h->root.u.undef.abfd = h->root.u.def.section->owner;
	      size_change_ok = true;
	    }
	}

      if (! (_bfd_generic_link_add_one_symbol
	     (info, abfd, name, flags, sec, value, (const char *) NULL,
	      false, collect, (struct bfd_link_hash_entry **) sym_hash)))
	goto error_return;

      h = *sym_hash;
      while (h->root.type == bfd_link_hash_indirect
	     || h->root.type == bfd_link_hash_warning)
	h = (struct elf_link_hash_entry *) h->root.u.i.link;
      *sym_hash = h;

      new_weakdef = false;
      if (dynamic
	  && definition
	  && (flags & BSF_WEAK) != 0
	  && ELF_ST_TYPE (sym.st_info) != STT_FUNC
	  && info->hash->creator->flavour == bfd_target_elf_flavour
	  && h->weakdef == NULL)
	{
	  /* Keep a list of all weak defined non function symbols from
	     a dynamic object, using the weakdef field.  Later in this
	     function we will set the weakdef field to the correct
	     value.  We only put non-function symbols from dynamic
	     objects on this list, because that happens to be the only
	     time we need to know the normal symbol corresponding to a
	     weak symbol, and the information is time consuming to
	     figure out.  If the weakdef field is not already NULL,
	     then this symbol was already defined by some previous
	     dynamic object, and we will be using that previous
	     definition anyhow.  */

	  h->weakdef = weaks;
	  weaks = h;
	  new_weakdef = true;
	}

      /* Get the alignment of a common symbol.  */
      if (sym.st_shndx == SHN_COMMON
	  && h->root.type == bfd_link_hash_common)
	h->root.u.c.p->alignment_power = bfd_log2 (sym.st_value);

      if (info->hash->creator->flavour == bfd_target_elf_flavour)
	{
	  int old_flags;
	  boolean dynsym;
	  int new_flag;

	  /* Remember the symbol size and type.  */
	  if (sym.st_size != 0
	      && (definition || h->size == 0))
	    {
	      if (h->size != 0 && h->size != sym.st_size && ! size_change_ok)
		(*_bfd_error_handler)
		  ("Warning: size of symbol `%s' changed from %lu to %lu in %s",
		   name, (unsigned long) h->size, (unsigned long) sym.st_size,
		   bfd_get_filename (abfd));

	      h->size = sym.st_size;
	    }
	  if (ELF_ST_TYPE (sym.st_info) != STT_NOTYPE
	      && (definition || h->type == STT_NOTYPE))
	    {
	      if (h->type != STT_NOTYPE
		  && h->type != ELF_ST_TYPE (sym.st_info)
		  && ! type_change_ok)
		(*_bfd_error_handler)
		  ("Warning: type of symbol `%s' changed from %d to %d in %s",
		   name, h->type, ELF_ST_TYPE (sym.st_info),
		   bfd_get_filename (abfd));

	      h->type = ELF_ST_TYPE (sym.st_info);
	    }

	  /* Set a flag in the hash table entry indicating the type of
	     reference or definition we just found.  Keep a count of
	     the number of dynamic symbols we find.  A dynamic symbol
	     is one which is referenced or defined by both a regular
	     object and a shared object, or one which is referenced or
	     defined by more than one shared object.  */
	  old_flags = h->elf_link_hash_flags;
	  dynsym = false;
	  if (! dynamic)
	    {
	      if (! definition)
		new_flag = ELF_LINK_HASH_REF_REGULAR;
	      else
		new_flag = ELF_LINK_HASH_DEF_REGULAR;
	      if (info->shared
		  || (old_flags & (ELF_LINK_HASH_DEF_DYNAMIC
				   | ELF_LINK_HASH_REF_DYNAMIC)) != 0)
		dynsym = true;
	    }
	  else
	    {
	      if (! definition)
		new_flag = ELF_LINK_HASH_REF_DYNAMIC;
	      else
		new_flag = ELF_LINK_HASH_DEF_DYNAMIC;
	      if ((old_flags & new_flag) != 0
		  || (old_flags & (ELF_LINK_HASH_DEF_REGULAR
				   | ELF_LINK_HASH_REF_REGULAR)) != 0
		  || (h->weakdef != NULL
		      && (old_flags & (ELF_LINK_HASH_DEF_DYNAMIC
				       | ELF_LINK_HASH_REF_DYNAMIC)) != 0))
		dynsym = true;
	    }

	  h->elf_link_hash_flags |= new_flag;
	  if (dynsym && h->dynindx == -1)
	    {
	      if (! _bfd_elf_link_record_dynamic_symbol (info, h))
		goto error_return;
	      if (h->weakdef != NULL
		  && ! new_weakdef
		  && h->weakdef->dynindx == -1)
		{
		  if (! _bfd_elf_link_record_dynamic_symbol (info,
							     h->weakdef))
		    goto error_return;
		}
	    }
	}
    }

  /* Now set the weakdefs field correctly for all the weak defined
     symbols we found.  The only way to do this is to search all the
     symbols.  Since we only need the information for non functions in
     dynamic objects, that's the only time we actually put anything on
     the list WEAKS.  We need this information so that if a regular
     object refers to a symbol defined weakly in a dynamic object, the
     real symbol in the dynamic object is also put in the dynamic
     symbols; we also must arrange for both symbols to point to the
     same memory location.  We could handle the general case of symbol
     aliasing, but a general symbol alias can only be generated in
     assembler code, handling it correctly would be very time
     consuming, and other ELF linkers don't handle general aliasing
     either.  */
  while (weaks != NULL)
    {
      struct elf_link_hash_entry *hlook;
      asection *slook;
      bfd_vma vlook;
      struct elf_link_hash_entry **hpp;
      struct elf_link_hash_entry **hppend;

      hlook = weaks;
      weaks = hlook->weakdef;
      hlook->weakdef = NULL;

      BFD_ASSERT (hlook->root.type == bfd_link_hash_defined
		  || hlook->root.type == bfd_link_hash_defweak
		  || hlook->root.type == bfd_link_hash_common
		  || hlook->root.type == bfd_link_hash_indirect);
      slook = hlook->root.u.def.section;
      vlook = hlook->root.u.def.value;

      hpp = elf_sym_hashes (abfd);
      hppend = hpp + extsymcount;
      for (; hpp < hppend; hpp++)
	{
	  struct elf_link_hash_entry *h;

	  h = *hpp;
	  if (h != NULL && h != hlook
	      && (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak)
	      && h->root.u.def.section == slook
	      && h->root.u.def.value == vlook)
	    {
	      hlook->weakdef = h;

	      /* If the weak definition is in the list of dynamic
		 symbols, make sure the real definition is put there
		 as well.  */
	      if (hlook->dynindx != -1
		  && h->dynindx == -1)
		{
		  if (! _bfd_elf_link_record_dynamic_symbol (info, h))
		    goto error_return;
		}

	      break;
	    }
	}
    }

  if (buf != NULL)
    {
      free (buf);
      buf = NULL;
    }

  /* If this object is the same format as the output object, and it is
     not a shared library, then let the backend look through the
     relocs.

     This is required to build global offset table entries and to
     arrange for dynamic relocs.  It is not required for the
     particular common case of linking non PIC code, even when linking
     against shared libraries, but unfortunately there is no way of
     knowing whether an object file has been compiled PIC or not.
     Looking through the relocs is not particularly time consuming.
     The problem is that we must either (1) keep the relocs in memory,
     which causes the linker to require additional runtime memory or
     (2) read the relocs twice from the input file, which wastes time.
     This would be a good case for using mmap.

     I have no idea how to handle linking PIC code into a file of a
     different format.  It probably can't be done.  */
  check_relocs = get_elf_backend_data (abfd)->check_relocs;
  if (! dynamic
      && abfd->xvec == info->hash->creator
      && check_relocs != NULL)
    {
      asection *o;

      for (o = abfd->sections; o != NULL; o = o->next)
	{
	  Elf_Internal_Rela *internal_relocs;
	  boolean ok;

	  if ((o->flags & SEC_RELOC) == 0
	      || o->reloc_count == 0)
	    continue;

	  /* I believe we can ignore the relocs for any section which
             does not form part of the final process image, such as a
             debugging section.  */
	  if ((o->flags & SEC_ALLOC) == 0)
	    continue;

	  internal_relocs = elf_link_read_relocs (abfd, o, (PTR) NULL,
						  (Elf_Internal_Rela *) NULL,
						  info->keep_memory);
	  if (internal_relocs == NULL)
	    goto error_return;

	  ok = (*check_relocs) (abfd, info, o, internal_relocs);

	  if (! info->keep_memory)
	    free (internal_relocs);

	  if (! ok)
	    goto error_return;
	}
    }

  return true;

 error_return:
  if (buf != NULL)
    free (buf);
  if (dynbuf != NULL)
    free (dynbuf);
  return false;
}

/* Create some sections which will be filled in with dynamic linking
   information.  ABFD is an input file which requires dynamic sections
   to be created.  The dynamic sections take up virtual memory space
   when the final executable is run, so we need to create them before
   addresses are assigned to the output sections.  We work out the
   actual contents and size of these sections later.  */

boolean
elf_link_create_dynamic_sections (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  flagword flags;
  register asection *s;
  struct elf_link_hash_entry *h;
  struct elf_backend_data *bed;

  if (elf_hash_table (info)->dynamic_sections_created)
    return true;

  /* Make sure that all dynamic sections use the same input BFD.  */
  if (elf_hash_table (info)->dynobj == NULL)
    elf_hash_table (info)->dynobj = abfd;
  else
    abfd = elf_hash_table (info)->dynobj;

  /* Note that we set the SEC_IN_MEMORY flag for all of these
     sections.  */
  flags = SEC_ALLOC | SEC_LOAD | SEC_HAS_CONTENTS | SEC_IN_MEMORY;

  /* A dynamically linked executable has a .interp section, but a
     shared library does not.  */
  if (! info->shared)
    {
      s = bfd_make_section (abfd, ".interp");
      if (s == NULL
	  || ! bfd_set_section_flags (abfd, s, flags | SEC_READONLY))
	return false;
    }

  s = bfd_make_section (abfd, ".dynsym");
  if (s == NULL
      || ! bfd_set_section_flags (abfd, s, flags | SEC_READONLY)
      || ! bfd_set_section_alignment (abfd, s, LOG_FILE_ALIGN))
    return false;

  s = bfd_make_section (abfd, ".dynstr");
  if (s == NULL
      || ! bfd_set_section_flags (abfd, s, flags | SEC_READONLY))
    return false;

  /* Create a strtab to hold the dynamic symbol names.  */
  if (elf_hash_table (info)->dynstr == NULL)
    {
      elf_hash_table (info)->dynstr = elf_stringtab_init ();
      if (elf_hash_table (info)->dynstr == NULL)
	return false;
    }

  s = bfd_make_section (abfd, ".dynamic");
  if (s == NULL
      || ! bfd_set_section_flags (abfd, s, flags)
      || ! bfd_set_section_alignment (abfd, s, LOG_FILE_ALIGN))
    return false;

  /* The special symbol _DYNAMIC is always set to the start of the
     .dynamic section.  This call occurs before we have processed the
     symbols for any dynamic object, so we don't have to worry about
     overriding a dynamic definition.  We could set _DYNAMIC in a
     linker script, but we only want to define it if we are, in fact,
     creating a .dynamic section.  We don't want to define it if there
     is no .dynamic section, since on some ELF platforms the start up
     code examines it to decide how to initialize the process.  */
  h = NULL;
  if (! (_bfd_generic_link_add_one_symbol
	 (info, abfd, "_DYNAMIC", BSF_GLOBAL, s, (bfd_vma) 0,
	  (const char *) NULL, false, get_elf_backend_data (abfd)->collect,
	  (struct bfd_link_hash_entry **) &h)))
    return false;
  h->elf_link_hash_flags |= ELF_LINK_HASH_DEF_REGULAR;
  h->type = STT_OBJECT;

  if (info->shared
      && ! _bfd_elf_link_record_dynamic_symbol (info, h))
    return false;

  s = bfd_make_section (abfd, ".hash");
  if (s == NULL
      || ! bfd_set_section_flags (abfd, s, flags | SEC_READONLY)
      || ! bfd_set_section_alignment (abfd, s, LOG_FILE_ALIGN))
    return false;

  /* Let the backend create the rest of the sections.  This lets the
     backend set the right flags.  The backend will normally create
     the .got and .plt sections.  */
  bed = get_elf_backend_data (abfd);
  if (! (*bed->elf_backend_create_dynamic_sections) (abfd, info))
    return false;

  elf_hash_table (info)->dynamic_sections_created = true;

  return true;
}

/* Add an entry to the .dynamic table.  */

boolean
elf_add_dynamic_entry (info, tag, val)
     struct bfd_link_info *info;
     bfd_vma tag;
     bfd_vma val;
{
  Elf_Internal_Dyn dyn;
  bfd *dynobj;
  asection *s;
  size_t newsize;
  bfd_byte *newcontents;

  dynobj = elf_hash_table (info)->dynobj;

  s = bfd_get_section_by_name (dynobj, ".dynamic");
  BFD_ASSERT (s != NULL);

  newsize = s->_raw_size + sizeof (Elf_External_Dyn);
  if (s->contents == NULL)
    newcontents = (bfd_byte *) malloc (newsize);
  else
    newcontents = (bfd_byte *) realloc (s->contents, newsize);
  if (newcontents == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      return false;
    }

  dyn.d_tag = tag;
  dyn.d_un.d_val = val;
  elf_swap_dyn_out (dynobj, &dyn,
		    (Elf_External_Dyn *) (newcontents + s->_raw_size));

  s->_raw_size = newsize;
  s->contents = newcontents;

  return true;
}

/* Read and swap the relocs for a section.  They may have been cached.
   If the EXTERNAL_RELOCS and INTERNAL_RELOCS arguments are not NULL,
   they are used as buffers to read into.  They are known to be large
   enough.  If the INTERNAL_RELOCS relocs argument is NULL, the return
   value is allocated using either malloc or bfd_alloc, according to
   the KEEP_MEMORY argument.  */

static Elf_Internal_Rela *
elf_link_read_relocs (abfd, o, external_relocs, internal_relocs, keep_memory)
     bfd *abfd;
     asection *o;
     PTR external_relocs;
     Elf_Internal_Rela *internal_relocs;
     boolean keep_memory;
{
  Elf_Internal_Shdr *rel_hdr;
  PTR alloc1 = NULL;
  Elf_Internal_Rela *alloc2 = NULL;

  if (elf_section_data (o)->relocs != NULL)
    return elf_section_data (o)->relocs;

  if (o->reloc_count == 0)
    return NULL;

  rel_hdr = &elf_section_data (o)->rel_hdr;

  if (internal_relocs == NULL)
    {
      size_t size;

      size = o->reloc_count * sizeof (Elf_Internal_Rela);
      if (keep_memory)
	internal_relocs = (Elf_Internal_Rela *) bfd_alloc (abfd, size);
      else
	internal_relocs = alloc2 = (Elf_Internal_Rela *) malloc (size);
      if (internal_relocs == NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  goto error_return;
	}
    }

  if (external_relocs == NULL)
    {
      alloc1 = (PTR) malloc ((size_t) rel_hdr->sh_size);
      if (alloc1 == NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  goto error_return;
	}
      external_relocs = alloc1;
    }

  if ((bfd_seek (abfd, rel_hdr->sh_offset, SEEK_SET) != 0)
      || (bfd_read (external_relocs, 1, rel_hdr->sh_size, abfd)
	  != rel_hdr->sh_size))
    goto error_return;

  /* Swap in the relocs.  For convenience, we always produce an
     Elf_Internal_Rela array; if the relocs are Rel, we set the addend
     to 0.  */
  if (rel_hdr->sh_entsize == sizeof (Elf_External_Rel))
    {
      Elf_External_Rel *erel;
      Elf_External_Rel *erelend;
      Elf_Internal_Rela *irela;

      erel = (Elf_External_Rel *) external_relocs;
      erelend = erel + o->reloc_count;
      irela = internal_relocs;
      for (; erel < erelend; erel++, irela++)
	{
	  Elf_Internal_Rel irel;

	  elf_swap_reloc_in (abfd, erel, &irel);
	  irela->r_offset = irel.r_offset;
	  irela->r_info = irel.r_info;
	  irela->r_addend = 0;
	}
    }
  else
    {
      Elf_External_Rela *erela;
      Elf_External_Rela *erelaend;
      Elf_Internal_Rela *irela;

      BFD_ASSERT (rel_hdr->sh_entsize == sizeof (Elf_External_Rela));

      erela = (Elf_External_Rela *) external_relocs;
      erelaend = erela + o->reloc_count;
      irela = internal_relocs;
      for (; erela < erelaend; erela++, irela++)
	elf_swap_reloca_in (abfd, erela, irela);
    }

  /* Cache the results for next time, if we can.  */
  if (keep_memory)
    elf_section_data (o)->relocs = internal_relocs;
		 
  if (alloc1 != NULL)
    free (alloc1);

  /* Don't free alloc2, since if it was allocated we are passing it
     back (under the name of internal_relocs).  */

  return internal_relocs;

 error_return:
  if (alloc1 != NULL)
    free (alloc1);
  if (alloc2 != NULL)
    free (alloc2);
  return NULL;
}

/* Record an assignment to a symbol made by a linker script.  We need
   this in case some dynamic object refers to this symbol.  */

/*ARGSUSED*/
boolean
NAME(bfd_elf,record_link_assignment) (output_bfd, info, name, provide)
     bfd *output_bfd;
     struct bfd_link_info *info;
     const char *name;
     boolean provide;
{
  struct elf_link_hash_entry *h;

  if (info->hash->creator->flavour != bfd_target_elf_flavour)
    return true;

  h = elf_link_hash_lookup (elf_hash_table (info), name, true, true, false);
  if (h == NULL)
    return false;

  /* If this symbol is being provided by the linker script, and it is
     currently defined by a dynamic object, but not by a regular
     object, then mark it as undefined so that the generic linker will
     force the correct value.  */
  if (provide
      && (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_DYNAMIC) != 0
      && (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) == 0)
    h->root.type = bfd_link_hash_undefined;

  h->elf_link_hash_flags |= ELF_LINK_HASH_DEF_REGULAR;
  h->type = STT_OBJECT;

  if (((h->elf_link_hash_flags & (ELF_LINK_HASH_DEF_DYNAMIC
				  | ELF_LINK_HASH_REF_DYNAMIC)) != 0
       || info->shared)
      && h->dynindx == -1)
    {
      if (! _bfd_elf_link_record_dynamic_symbol (info, h))
	return false;

      /* If this is a weak defined symbol, and we know a corresponding
	 real symbol from the same dynamic object, make sure the real
	 symbol is also made into a dynamic symbol.  */
      if (h->weakdef != NULL
	  && h->weakdef->dynindx == -1)
	{
	  if (! _bfd_elf_link_record_dynamic_symbol (info, h->weakdef))
	    return false;
	}
    }

  return true;
}

/* Array used to determine the number of hash table buckets to use
   based on the number of symbols there are.  If there are fewer than
   3 symbols we use 1 bucket, fewer than 17 symbols we use 3 buckets,
   fewer than 37 we use 17 buckets, and so forth.  We never use more
   than 521 buckets.  */

static const size_t elf_buckets[] =
{
  1, 3, 17, 37, 67, 97, 131, 197, 263, 521, 0
};

/* Set up the sizes and contents of the ELF dynamic sections.  This is
   called by the ELF linker emulation before_allocation routine.  We
   must set the sizes of the sections before the linker sets the
   addresses of the various sections.  */

boolean
NAME(bfd_elf,size_dynamic_sections) (output_bfd, soname, rpath,
				     export_dynamic, info, sinterpptr)
     bfd *output_bfd;
     const char *soname;
     const char *rpath;
     boolean export_dynamic;
     struct bfd_link_info *info;
     asection **sinterpptr;
{
  bfd *dynobj;
  struct elf_backend_data *bed;

  *sinterpptr = NULL;

  if (info->hash->creator->flavour != bfd_target_elf_flavour)
    return true;

  dynobj = elf_hash_table (info)->dynobj;

  /* If there were no dynamic objects in the link, there is nothing to
     do here.  */
  if (dynobj == NULL)
    return true;

  /* If we are supposed to export all symbols into the dynamic symbol
     table (this is not the normal case), then do so.  */
  if (export_dynamic)
    {
      struct elf_info_failed eif;

      eif.failed = false;
      eif.info = info;
      elf_link_hash_traverse (elf_hash_table (info), elf_export_symbol,
			      (PTR) &eif);
      if (eif.failed)
	return false;
    }

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      struct elf_info_failed eif;
      bfd_size_type strsize;

      *sinterpptr = bfd_get_section_by_name (dynobj, ".interp");
      BFD_ASSERT (*sinterpptr != NULL || info->shared);

      if (soname != NULL)
	{
	  bfd_size_type indx;

	  indx = _bfd_stringtab_add (elf_hash_table (info)->dynstr, soname,
				     true, true);
	  if (indx == (bfd_size_type) -1
	      || ! elf_add_dynamic_entry (info, DT_SONAME, indx))
	    return false;
	}      

      if (info->symbolic)
	{
	  if (! elf_add_dynamic_entry (info, DT_SYMBOLIC, 0))
	    return false;
	}

      if (rpath != NULL)
	{
	  bfd_size_type indx;

	  indx = _bfd_stringtab_add (elf_hash_table (info)->dynstr, rpath,
				     true, true);
	  if (indx == (bfd_size_type) -1
	      || ! elf_add_dynamic_entry (info, DT_RPATH, indx))
	    return false;
	}

      /* Find all symbols which were defined in a dynamic object and make
	 the backend pick a reasonable value for them.  */
      eif.failed = false;
      eif.info = info;
      elf_link_hash_traverse (elf_hash_table (info),
			      elf_adjust_dynamic_symbol,
			      (PTR) &eif);
      if (eif.failed)
	return false;

      /* Add some entries to the .dynamic section.  We fill in some of the
	 values later, in elf_bfd_final_link, but we must add the entries
	 now so that we know the final size of the .dynamic section.  */
      if (elf_link_hash_lookup (elf_hash_table (info), "_init", false,
				false, false) != NULL)
	{
	  if (! elf_add_dynamic_entry (info, DT_INIT, 0))
	    return false;
	}
      if (elf_link_hash_lookup (elf_hash_table (info), "_fini", false,
				false, false) != NULL)
	{
	  if (! elf_add_dynamic_entry (info, DT_FINI, 0))
	    return false;
	}
      strsize = _bfd_stringtab_size (elf_hash_table (info)->dynstr);
      if (! elf_add_dynamic_entry (info, DT_HASH, 0)
	  || ! elf_add_dynamic_entry (info, DT_STRTAB, 0)
	  || ! elf_add_dynamic_entry (info, DT_SYMTAB, 0)
	  || ! elf_add_dynamic_entry (info, DT_STRSZ, strsize)
	  || ! elf_add_dynamic_entry (info, DT_SYMENT,
				      sizeof (Elf_External_Sym)))
	return false;
    }

  /* The backend must work out the sizes of all the other dynamic
     sections.  */
  bed = get_elf_backend_data (output_bfd);
  if (! (*bed->elf_backend_size_dynamic_sections) (output_bfd, info))
    return false;

  if (elf_hash_table (info)->dynamic_sections_created)
    {
      size_t dynsymcount;
      asection *s;
      size_t i;
      size_t bucketcount = 0;
      Elf_Internal_Sym isym;

      /* Set the size of the .dynsym and .hash sections.  We counted
	 the number of dynamic symbols in elf_link_add_object_symbols.
	 We will build the contents of .dynsym and .hash when we build
	 the final symbol table, because until then we do not know the
	 correct value to give the symbols.  We built the .dynstr
	 section as we went along in elf_link_add_object_symbols.  */
      dynsymcount = elf_hash_table (info)->dynsymcount;
      s = bfd_get_section_by_name (dynobj, ".dynsym");
      BFD_ASSERT (s != NULL);
      s->_raw_size = dynsymcount * sizeof (Elf_External_Sym);
      s->contents = (bfd_byte *) bfd_alloc (output_bfd, s->_raw_size);
      if (s->contents == NULL && s->_raw_size != 0)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return false;
	}

      /* The first entry in .dynsym is a dummy symbol.  */
      isym.st_value = 0;
      isym.st_size = 0;
      isym.st_name = 0;
      isym.st_info = 0;
      isym.st_other = 0;
      isym.st_shndx = 0;
      elf_swap_symbol_out (output_bfd, &isym,
			   (PTR) (Elf_External_Sym *) s->contents);

      for (i = 0; elf_buckets[i] != 0; i++)
	{
	  bucketcount = elf_buckets[i];
	  if (dynsymcount < elf_buckets[i + 1])
	    break;
	}

      s = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (s != NULL);
      s->_raw_size = (2 + bucketcount + dynsymcount) * (ARCH_SIZE / 8);
      s->contents = (bfd_byte *) bfd_alloc (output_bfd, s->_raw_size);
      if (s->contents == NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return false;
	}
      memset (s->contents, 0, (size_t) s->_raw_size);

      put_word (output_bfd, bucketcount, s->contents);
      put_word (output_bfd, dynsymcount, s->contents + (ARCH_SIZE / 8));

      elf_hash_table (info)->bucketcount = bucketcount;

      s = bfd_get_section_by_name (dynobj, ".dynstr");
      BFD_ASSERT (s != NULL);
      s->_raw_size = _bfd_stringtab_size (elf_hash_table (info)->dynstr);

      if (! elf_add_dynamic_entry (info, DT_NULL, 0))
	return false;
    }

  return true;
}

/* This routine is used to export all defined symbols into the dynamic
   symbol table.  It is called via elf_link_hash_traverse.  */

static boolean
elf_export_symbol (h, data)
     struct elf_link_hash_entry *h;
     PTR data;
{
  struct elf_info_failed *eif = (struct elf_info_failed *) data;

  if (h->dynindx == -1
      && (h->elf_link_hash_flags
	  & (ELF_LINK_HASH_DEF_REGULAR | ELF_LINK_HASH_REF_REGULAR)) != 0)
    {
      if (! _bfd_elf_link_record_dynamic_symbol (eif->info, h))
	{
	  eif->failed = true;
	  return false;
	}
    }

  return true;
}

/* Make the backend pick a good value for a dynamic symbol.  This is
   called via elf_link_hash_traverse, and also calls itself
   recursively.  */

static boolean
elf_adjust_dynamic_symbol (h, data)
     struct elf_link_hash_entry *h;
     PTR data;
{
  struct elf_info_failed *eif = (struct elf_info_failed *) data;
  bfd *dynobj;
  struct elf_backend_data *bed;

  /* If -Bsymbolic was used (which means to bind references to global
     symbols to the definition within the shared object), and this
     symbol was defined in a regular object, then it actually doesn't
     need a PLT entry.  */
  if ((h->elf_link_hash_flags & ELF_LINK_HASH_NEEDS_PLT) != 0
      && eif->info->shared
      && eif->info->symbolic
      && (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) != 0)
    h->elf_link_hash_flags &=~ ELF_LINK_HASH_NEEDS_PLT;

  /* If this symbol does not require a PLT entry, and it is not
     defined by a dynamic object, or is not referenced by a regular
     object, ignore it.  We do have to handle a weak defined symbol,
     even if no regular object refers to it, if we decided to add it
     to the dynamic symbol table.  FIXME: Do we normally need to worry
     about symbols which are defined by one dynamic object and
     referenced by another one?  */
  if ((h->elf_link_hash_flags & ELF_LINK_HASH_NEEDS_PLT) == 0
      && ((h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) != 0
	  || (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_DYNAMIC) == 0
	  || ((h->elf_link_hash_flags & ELF_LINK_HASH_REF_REGULAR) == 0
	      && (h->weakdef == NULL || h->weakdef->dynindx == -1))))
    return true;

  /* If we've already adjusted this symbol, don't do it again.  This
     can happen via a recursive call.  */
  if ((h->elf_link_hash_flags & ELF_LINK_HASH_DYNAMIC_ADJUSTED) != 0)
    return true;

  /* Don't look at this symbol again.  Note that we must set this
     after checking the above conditions, because we may look at a
     symbol once, decide not to do anything, and then get called
     recursively later after REF_REGULAR is set below.  */
  h->elf_link_hash_flags |= ELF_LINK_HASH_DYNAMIC_ADJUSTED;

  /* If this is a weak definition, and we know a real definition, and
     the real symbol is not itself defined by a regular object file,
     then get a good value for the real definition.  We handle the
     real symbol first, for the convenience of the backend routine.

     Note that there is a confusing case here.  If the real definition
     is defined by a regular object file, we don't get the real symbol
     from the dynamic object, but we do get the weak symbol.  If the
     processor backend uses a COPY reloc, then if some routine in the
     dynamic object changes the real symbol, we will not see that
     change in the corresponding weak symbol.  This is the way other
     ELF linkers work as well, and seems to be a result of the shared
     library model.

     I will clarify this issue.  Most SVR4 shared libraries define the
     variable _timezone and define timezone as a weak synonym.  The
     tzset call changes _timezone.  If you write
       extern int timezone;
       int _timezone = 5;
       int main () { tzset (); printf ("%d %d\n", timezone, _timezone); }
     you might expect that, since timezone is a synonym for _timezone,
     the same number will print both times.  However, if the processor
     backend uses a COPY reloc, then actually timezone will be copied
     into your process image, and, since you define _timezone
     yourself, _timezone will not.  Thus timezone and _timezone will
     wind up at different memory locations.  The tzset call will set
     _timezone, leaving timezone unchanged.  */

  if (h->weakdef != NULL)
    {
      struct elf_link_hash_entry *weakdef;

      BFD_ASSERT (h->root.type == bfd_link_hash_defined
		  || h->root.type == bfd_link_hash_defweak);
      weakdef = h->weakdef;
      BFD_ASSERT (weakdef->root.type == bfd_link_hash_defined
		  || weakdef->root.type == bfd_link_hash_defweak);
      BFD_ASSERT (weakdef->elf_link_hash_flags & ELF_LINK_HASH_DEF_DYNAMIC);
      if ((weakdef->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) != 0)
	{
	  /* This symbol is defined by a regular object file, so we
	     will not do anything special.  Clear weakdef for the
	     convenience of the processor backend.  */
	  h->weakdef = NULL;
	}
      else
	{
	  /* There is an implicit reference by a regular object file
	     via the weak symbol.  */
	  weakdef->elf_link_hash_flags |= ELF_LINK_HASH_REF_REGULAR;
	  if (! elf_adjust_dynamic_symbol (weakdef, (PTR) eif))
	    return false;
	}
    }

  dynobj = elf_hash_table (eif->info)->dynobj;
  bed = get_elf_backend_data (dynobj);
  if (! (*bed->elf_backend_adjust_dynamic_symbol) (eif->info, h))
    {
      eif->failed = true;
      return false;
    }

  return true;
}

/* Final phase of ELF linker.  */

/* A structure we use to avoid passing large numbers of arguments.  */

struct elf_final_link_info
{
  /* General link information.  */
  struct bfd_link_info *info;
  /* Output BFD.  */
  bfd *output_bfd;
  /* Symbol string table.  */
  struct bfd_strtab_hash *symstrtab;
  /* .dynsym section.  */
  asection *dynsym_sec;
  /* .hash section.  */
  asection *hash_sec;
  /* Buffer large enough to hold contents of any section.  */
  bfd_byte *contents;
  /* Buffer large enough to hold external relocs of any section.  */
  PTR external_relocs;
  /* Buffer large enough to hold internal relocs of any section.  */
  Elf_Internal_Rela *internal_relocs;
  /* Buffer large enough to hold external local symbols of any input
     BFD.  */
  Elf_External_Sym *external_syms;
  /* Buffer large enough to hold internal local symbols of any input
     BFD.  */
  Elf_Internal_Sym *internal_syms;
  /* Array large enough to hold a symbol index for each local symbol
     of any input BFD.  */
  long *indices;
  /* Array large enough to hold a section pointer for each local
     symbol of any input BFD.  */
  asection **sections;
  /* Buffer to hold swapped out symbols.  */
  Elf_External_Sym *symbuf;
  /* Number of swapped out symbols in buffer.  */
  size_t symbuf_count;
  /* Number of symbols which fit in symbuf.  */
  size_t symbuf_size;
};

static boolean elf_link_output_sym
  PARAMS ((struct elf_final_link_info *, const char *,
	   Elf_Internal_Sym *, asection *));
static boolean elf_link_flush_output_syms
  PARAMS ((struct elf_final_link_info *));
static boolean elf_link_output_extsym
  PARAMS ((struct elf_link_hash_entry *, PTR));
static boolean elf_link_input_bfd
  PARAMS ((struct elf_final_link_info *, bfd *));
static boolean elf_reloc_link_order
  PARAMS ((bfd *, struct bfd_link_info *, asection *,
	   struct bfd_link_order *));

/* This struct is used to pass information to routines called via
   elf_link_hash_traverse which must return failure.  */

struct elf_finfo_failed
{
  boolean failed;
  struct elf_final_link_info *finfo;
};  

/* Do the final step of an ELF link.  */

boolean
elf_bfd_final_link (abfd, info)
     bfd *abfd;
     struct bfd_link_info *info;
{
  boolean dynamic;
  bfd *dynobj;
  struct elf_final_link_info finfo;
  register asection *o;
  register struct bfd_link_order *p;
  register bfd *sub;
  size_t max_contents_size;
  size_t max_external_reloc_size;
  size_t max_internal_reloc_count;
  size_t max_sym_count;
  file_ptr off;
  Elf_Internal_Sym elfsym;
  unsigned int i;
  Elf_Internal_Shdr *symtab_hdr;
  Elf_Internal_Shdr *symstrtab_hdr;
  struct elf_backend_data *bed = get_elf_backend_data (abfd);
  struct elf_finfo_failed eif;

  if (info->shared)
    abfd->flags |= DYNAMIC;

  dynamic = elf_hash_table (info)->dynamic_sections_created;
  dynobj = elf_hash_table (info)->dynobj;

  finfo.info = info;
  finfo.output_bfd = abfd;
  finfo.symstrtab = elf_stringtab_init ();
  if (finfo.symstrtab == NULL)
    return false;
  if (! dynamic)
    {
      finfo.dynsym_sec = NULL;
      finfo.hash_sec = NULL;
    }
  else
    {
      finfo.dynsym_sec = bfd_get_section_by_name (dynobj, ".dynsym");
      finfo.hash_sec = bfd_get_section_by_name (dynobj, ".hash");
      BFD_ASSERT (finfo.dynsym_sec != NULL && finfo.hash_sec != NULL);
    }
  finfo.contents = NULL;
  finfo.external_relocs = NULL;
  finfo.internal_relocs = NULL;
  finfo.external_syms = NULL;
  finfo.internal_syms = NULL;
  finfo.indices = NULL;
  finfo.sections = NULL;
  finfo.symbuf = NULL;
  finfo.symbuf_count = 0;

  /* Count up the number of relocations we will output for each output
     section, so that we know the sizes of the reloc sections.  We
     also figure out some maximum sizes.  */
  max_contents_size = 0;
  max_external_reloc_size = 0;
  max_internal_reloc_count = 0;
  max_sym_count = 0;
  for (o = abfd->sections; o != (asection *) NULL; o = o->next)
    {
      o->reloc_count = 0;

      for (p = o->link_order_head; p != NULL; p = p->next)
	{
	  if (p->type == bfd_section_reloc_link_order
	      || p->type == bfd_symbol_reloc_link_order)
	    ++o->reloc_count;
	  else if (p->type == bfd_indirect_link_order)
	    {
	      asection *sec;

	      sec = p->u.indirect.section;

	      if (info->relocateable)
		o->reloc_count += sec->reloc_count;

	      if (sec->_raw_size > max_contents_size)
		max_contents_size = sec->_raw_size;
	      if (sec->_cooked_size > max_contents_size)
		max_contents_size = sec->_cooked_size;

	      /* We are interested in just local symbols, not all
		 symbols.  */
	      if (bfd_get_flavour (sec->owner) == bfd_target_elf_flavour)
		{
		  size_t sym_count;

		  if (elf_bad_symtab (sec->owner))
		    sym_count = (elf_tdata (sec->owner)->symtab_hdr.sh_size
				 / sizeof (Elf_External_Sym));
		  else
		    sym_count = elf_tdata (sec->owner)->symtab_hdr.sh_info;

		  if (sym_count > max_sym_count)
		    max_sym_count = sym_count;

		  if ((sec->flags & SEC_RELOC) != 0)
		    {
		      size_t ext_size;

		      ext_size = elf_section_data (sec)->rel_hdr.sh_size;
		      if (ext_size > max_external_reloc_size)
			max_external_reloc_size = ext_size;
		      if (sec->reloc_count > max_internal_reloc_count)
			max_internal_reloc_count = sec->reloc_count;
		    }
		}
	    }
	}

      if (o->reloc_count > 0)
	o->flags |= SEC_RELOC;
      else
	{
	  /* Explicitly clear the SEC_RELOC flag.  The linker tends to
	     set it (this is probably a bug) and if it is set
	     assign_section_numbers will create a reloc section.  */
	  o->flags &=~ SEC_RELOC;
	}

      /* If the SEC_ALLOC flag is not set, force the section VMA to
	 zero.  This is done in elf_fake_sections as well, but forcing
	 the VMA to 0 here will ensure that relocs against these
	 sections are handled correctly.  */
      if ((o->flags & SEC_ALLOC) == 0)
	o->vma = 0;
    }

  /* Figure out the file positions for everything but the symbol table
     and the relocs.  We set symcount to force assign_section_numbers
     to create a symbol table.  */
  abfd->symcount = info->strip == strip_all ? 0 : 1;
  BFD_ASSERT (! abfd->output_has_begun);
  if (! _bfd_elf_compute_section_file_positions (abfd, info))
    goto error_return;

  /* That created the reloc sections.  Set their sizes, and assign
     them file positions, and allocate some buffers.  */
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0)
	{
	  Elf_Internal_Shdr *rel_hdr;
	  register struct elf_link_hash_entry **p, **pend;

	  rel_hdr = &elf_section_data (o)->rel_hdr;

	  rel_hdr->sh_size = rel_hdr->sh_entsize * o->reloc_count;

	  /* The contents field must last into write_object_contents,
	     so we allocate it with bfd_alloc rather than malloc.  */
	  rel_hdr->contents = (PTR) bfd_alloc (abfd, rel_hdr->sh_size);
	  if (rel_hdr->contents == NULL && rel_hdr->sh_size != 0)
	    {
	      bfd_set_error (bfd_error_no_memory);
	      goto error_return;
	    }

	  p = ((struct elf_link_hash_entry **)
	       malloc (o->reloc_count
		       * sizeof (struct elf_link_hash_entry *)));
	  if (p == NULL && o->reloc_count != 0)
	    {
	      bfd_set_error (bfd_error_no_memory);
	      goto error_return;
	    }
	  elf_section_data (o)->rel_hashes = p;
	  pend = p + o->reloc_count;
	  for (; p < pend; p++)
	    *p = NULL;

	  /* Use the reloc_count field as an index when outputting the
	     relocs.  */
	  o->reloc_count = 0;
	}
    }

  _bfd_elf_assign_file_positions_for_relocs (abfd);

  /* We have now assigned file positions for all the sections except
     .symtab and .strtab.  We start the .symtab section at the current
     file position, and write directly to it.  We build the .strtab
     section in memory.  */
  abfd->symcount = 0;
  symtab_hdr = &elf_tdata (abfd)->symtab_hdr;
  /* sh_name is set in prep_headers.  */
  symtab_hdr->sh_type = SHT_SYMTAB;
  symtab_hdr->sh_flags = 0;
  symtab_hdr->sh_addr = 0;
  symtab_hdr->sh_size = 0;
  symtab_hdr->sh_entsize = sizeof (Elf_External_Sym);
  /* sh_link is set in assign_section_numbers.  */
  /* sh_info is set below.  */
  /* sh_offset is set just below.  */
  symtab_hdr->sh_addralign = 4;  /* FIXME: system dependent?  */

  off = elf_tdata (abfd)->next_file_pos;
  off = _bfd_elf_assign_file_position_for_section (symtab_hdr, off, true);

  /* Note that at this point elf_tdata (abfd)->next_file_pos is
     incorrect.  We do not yet know the size of the .symtab section.
     We correct next_file_pos below, after we do know the size.  */

  /* Allocate a buffer to hold swapped out symbols.  This is to avoid
     continuously seeking to the right position in the file.  */
  if (! info->keep_memory || max_sym_count < 20)
    finfo.symbuf_size = 20;
  else
    finfo.symbuf_size = max_sym_count;
  finfo.symbuf = ((Elf_External_Sym *)
		  malloc (finfo.symbuf_size * sizeof (Elf_External_Sym)));
  if (finfo.symbuf == NULL)
    {
      bfd_set_error (bfd_error_no_memory);
      goto error_return;
    }

  /* Start writing out the symbol table.  The first symbol is always a
     dummy symbol.  */
  elfsym.st_value = 0;
  elfsym.st_size = 0;
  elfsym.st_info = 0;
  elfsym.st_other = 0;
  elfsym.st_shndx = SHN_UNDEF;
  if (! elf_link_output_sym (&finfo, (const char *) NULL,
			     &elfsym, bfd_und_section_ptr))
    goto error_return;

#if 0
  /* Some standard ELF linkers do this, but we don't because it causes
     bootstrap comparison failures.  */
  /* Output a file symbol for the output file as the second symbol.
     We output this even if we are discarding local symbols, although
     I'm not sure if this is correct.  */
  elfsym.st_value = 0;
  elfsym.st_size = 0;
  elfsym.st_info = ELF_ST_INFO (STB_LOCAL, STT_FILE);
  elfsym.st_other = 0;
  elfsym.st_shndx = SHN_ABS;
  if (! elf_link_output_sym (&finfo, bfd_get_filename (abfd),
			     &elfsym, bfd_abs_section_ptr))
    goto error_return;
#endif

  /* Output a symbol for each section.  We output these even if we are
     discarding local symbols, since they are used for relocs.  These
     symbols have no names.  We store the index of each one in the
     index field of the section, so that we can find it again when
     outputting relocs.  */
  elfsym.st_value = 0;
  elfsym.st_size = 0;
  elfsym.st_info = ELF_ST_INFO (STB_LOCAL, STT_SECTION);
  elfsym.st_other = 0;
  for (i = 1; i < elf_elfheader (abfd)->e_shnum; i++)
    {
      o = section_from_elf_index (abfd, i);
      if (o != NULL)
	o->target_index = abfd->symcount;
      elfsym.st_shndx = i;
      if (! elf_link_output_sym (&finfo, (const char *) NULL,
				 &elfsym, o))
	goto error_return;
    }

  /* Allocate some memory to hold information read in from the input
     files.  */
  finfo.contents = (bfd_byte *) malloc (max_contents_size);
  finfo.external_relocs = (PTR) malloc (max_external_reloc_size);
  finfo.internal_relocs = ((Elf_Internal_Rela *)
			   malloc (max_internal_reloc_count
				   * sizeof (Elf_Internal_Rela)));
  finfo.external_syms = ((Elf_External_Sym *)
			 malloc (max_sym_count * sizeof (Elf_External_Sym)));
  finfo.internal_syms = ((Elf_Internal_Sym *)
			 malloc (max_sym_count * sizeof (Elf_Internal_Sym)));
  finfo.indices = (long *) malloc (max_sym_count * sizeof (long));
  finfo.sections = (asection **) malloc (max_sym_count * sizeof (asection *));
  if ((finfo.contents == NULL && max_contents_size != 0)
      || (finfo.external_relocs == NULL && max_external_reloc_size != 0)
      || (finfo.internal_relocs == NULL && max_internal_reloc_count != 0)
      || (finfo.external_syms == NULL && max_sym_count != 0)
      || (finfo.internal_syms == NULL && max_sym_count != 0)
      || (finfo.indices == NULL && max_sym_count != 0)
      || (finfo.sections == NULL && max_sym_count != 0))
    {
      bfd_set_error (bfd_error_no_memory);
      goto error_return;
    }

  /* Since ELF permits relocations to be against local symbols, we
     must have the local symbols available when we do the relocations.
     Since we would rather only read the local symbols once, and we
     would rather not keep them in memory, we handle all the
     relocations for a single input file at the same time.

     Unfortunately, there is no way to know the total number of local
     symbols until we have seen all of them, and the local symbol
     indices precede the global symbol indices.  This means that when
     we are generating relocateable output, and we see a reloc against
     a global symbol, we can not know the symbol index until we have
     finished examining all the local symbols to see which ones we are
     going to output.  To deal with this, we keep the relocations in
     memory, and don't output them until the end of the link.  This is
     an unfortunate waste of memory, but I don't see a good way around
     it.  Fortunately, it only happens when performing a relocateable
     link, which is not the common case.  FIXME: If keep_memory is set
     we could write the relocs out and then read them again; I don't
     know how bad the memory loss will be.  */

  for (sub = info->input_bfds; sub != NULL; sub = sub->next)
    sub->output_has_begun = false;
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      for (p = o->link_order_head; p != NULL; p = p->next)
	{
	  if (p->type == bfd_indirect_link_order
	      && (bfd_get_flavour (p->u.indirect.section->owner)
		  == bfd_target_elf_flavour))
	    {
	      sub = p->u.indirect.section->owner;
	      if (! sub->output_has_begun)
		{
		  if (! elf_link_input_bfd (&finfo, sub))
		    goto error_return;
		  sub->output_has_begun = true;
		}
	    }
	  else if (p->type == bfd_section_reloc_link_order
		   || p->type == bfd_symbol_reloc_link_order)
	    {
	      if (! elf_reloc_link_order (abfd, info, o, p))
		goto error_return;
	    }
	  else
	    {
	      if (! _bfd_default_link_order (abfd, info, o, p))
		goto error_return;
	    }
	}
    }

  /* That wrote out all the local symbols.  Finish up the symbol table
     with the global symbols.  */

  /* The sh_info field records the index of the first non local
     symbol.  */
  symtab_hdr->sh_info = abfd->symcount;
  if (dynamic)
    elf_section_data (finfo.dynsym_sec->output_section)->this_hdr.sh_info = 1;

  /* We get the global symbols from the hash table.  */
  eif.failed = false;
  eif.finfo = &finfo;
  elf_link_hash_traverse (elf_hash_table (info), elf_link_output_extsym,
			  (PTR) &eif);
  if (eif.failed)
    return false;

  /* Flush all symbols to the file.  */
  if (! elf_link_flush_output_syms (&finfo))
    return false;

  /* Now we know the size of the symtab section.  */
  off += symtab_hdr->sh_size;

  /* Finish up and write out the symbol string table (.strtab)
     section.  */
  symstrtab_hdr = &elf_tdata (abfd)->strtab_hdr;
  /* sh_name was set in prep_headers.  */
  symstrtab_hdr->sh_type = SHT_STRTAB;
  symstrtab_hdr->sh_flags = 0;
  symstrtab_hdr->sh_addr = 0;
  symstrtab_hdr->sh_size = _bfd_stringtab_size (finfo.symstrtab);
  symstrtab_hdr->sh_entsize = 0;
  symstrtab_hdr->sh_link = 0;
  symstrtab_hdr->sh_info = 0;
  /* sh_offset is set just below.  */
  symstrtab_hdr->sh_addralign = 1;

  off = _bfd_elf_assign_file_position_for_section (symstrtab_hdr, off, true);
  elf_tdata (abfd)->next_file_pos = off;

  if (bfd_seek (abfd, symstrtab_hdr->sh_offset, SEEK_SET) != 0
      || ! _bfd_stringtab_emit (abfd, finfo.symstrtab))
    return false;

  /* Adjust the relocs to have the correct symbol indices.  */
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      struct elf_link_hash_entry **rel_hash;
      Elf_Internal_Shdr *rel_hdr;

      if ((o->flags & SEC_RELOC) == 0)
	continue;

      rel_hash = elf_section_data (o)->rel_hashes;
      rel_hdr = &elf_section_data (o)->rel_hdr;
      for (i = 0; i < o->reloc_count; i++, rel_hash++)
	{
	  if (*rel_hash == NULL)
	    continue;
	      
	  BFD_ASSERT ((*rel_hash)->indx >= 0);

	  if (rel_hdr->sh_entsize == sizeof (Elf_External_Rel))
	    {
	      Elf_External_Rel *erel;
	      Elf_Internal_Rel irel;

	      erel = (Elf_External_Rel *) rel_hdr->contents + i;
	      elf_swap_reloc_in (abfd, erel, &irel);
	      irel.r_info = ELF_R_INFO ((*rel_hash)->indx,
					ELF_R_TYPE (irel.r_info));
	      elf_swap_reloc_out (abfd, &irel, erel);
	    }
	  else
	    {
	      Elf_External_Rela *erela;
	      Elf_Internal_Rela irela;

	      BFD_ASSERT (rel_hdr->sh_entsize
			  == sizeof (Elf_External_Rela));

	      erela = (Elf_External_Rela *) rel_hdr->contents + i;
	      elf_swap_reloca_in (abfd, erela, &irela);
	      irela.r_info = ELF_R_INFO ((*rel_hash)->indx,
					 ELF_R_TYPE (irela.r_info));
	      elf_swap_reloca_out (abfd, &irela, erela);
	    }
	}

      /* Set the reloc_count field to 0 to prevent write_relocs from
	 trying to swap the relocs out itself.  */
      o->reloc_count = 0;
    }

  /* If we are linking against a dynamic object, or generating a
     shared library, finish up the dynamic linking information.  */
  if (dynamic)
    {
      Elf_External_Dyn *dyncon, *dynconend;

      /* Fix up .dynamic entries.  */
      o = bfd_get_section_by_name (dynobj, ".dynamic");
      BFD_ASSERT (o != NULL);

      dyncon = (Elf_External_Dyn *) o->contents;
      dynconend = (Elf_External_Dyn *) (o->contents + o->_raw_size);
      for (; dyncon < dynconend; dyncon++)
	{
	  Elf_Internal_Dyn dyn;
	  const char *name;
	  unsigned int type;

	  elf_swap_dyn_in (dynobj, dyncon, &dyn);

	  switch (dyn.d_tag)
	    {
	    default:
	      break;

	      /* SVR4 linkers seem to set DT_INIT and DT_FINI based on
                 magic _init and _fini symbols.  This is pretty ugly,
                 but we are compatible.  */
	    case DT_INIT:
	      name = "_init";
	      goto get_sym;
	    case DT_FINI:
	      name = "_fini";
	    get_sym:
	      {
		struct elf_link_hash_entry *h;

		h = elf_link_hash_lookup (elf_hash_table (info), name,
					  false, false, true);
		if (h != NULL
		    && (h->root.type == bfd_link_hash_defined
			|| h->root.type == bfd_link_hash_defweak))
		  {
		    dyn.d_un.d_val = h->root.u.def.value;
		    o = h->root.u.def.section;
		    if (o->output_section != NULL)
		      dyn.d_un.d_val += (o->output_section->vma
					 + o->output_offset);
		    else
		      {
			/* The symbol is imported from another shared
			   library and does not apply to this one.  */
			dyn.d_un.d_val = 0;
		      }

		    elf_swap_dyn_out (dynobj, &dyn, dyncon);
		  }
	      }
	      break;

	    case DT_HASH:
	      name = ".hash";
	      goto get_vma;
	    case DT_STRTAB:
	      name = ".dynstr";
	      goto get_vma;
	    case DT_SYMTAB:
	      name = ".dynsym";
	    get_vma:
	      o = bfd_get_section_by_name (abfd, name);
	      BFD_ASSERT (o != NULL);
	      dyn.d_un.d_ptr = o->vma;
	      elf_swap_dyn_out (dynobj, &dyn, dyncon);
	      break;

	    case DT_REL:
	    case DT_RELA:
	    case DT_RELSZ:
	    case DT_RELASZ:
	      if (dyn.d_tag == DT_REL || dyn.d_tag == DT_RELSZ)
		type = SHT_REL;
	      else
		type = SHT_RELA;
	      dyn.d_un.d_val = 0;
	      for (i = 1; i < elf_elfheader (abfd)->e_shnum; i++)
		{
		  Elf_Internal_Shdr *hdr;

		  hdr = elf_elfsections (abfd)[i];
		  if (hdr->sh_type == type
		      && (hdr->sh_flags & SHF_ALLOC) != 0)
		    {
		      if (dyn.d_tag == DT_RELSZ || dyn.d_tag == DT_RELASZ)
			dyn.d_un.d_val += hdr->sh_size;
		      else
			{
			  if (dyn.d_un.d_val == 0
			      || hdr->sh_addr < dyn.d_un.d_val)
			    dyn.d_un.d_val = hdr->sh_addr;
			}
		    }
		}
	      elf_swap_dyn_out (dynobj, &dyn, dyncon);
	      break;
	    }
	}
    }

  /* If we have created any dynamic sections, then output them.  */
  if (dynobj != NULL)
    {
      if (! (*bed->elf_backend_finish_dynamic_sections) (abfd, info))
	goto error_return;

      for (o = dynobj->sections; o != NULL; o = o->next)
	{
	  if ((o->flags & SEC_HAS_CONTENTS) == 0
	      || o->_raw_size == 0)
	    continue;
	  if ((o->flags & SEC_IN_MEMORY) == 0)
	    {
	      /* At this point, we are only interested in sections
                 created by elf_link_create_dynamic_sections.  FIXME:
                 This test is fragile.  */
	      continue;
	    }
	  if ((elf_section_data (o->output_section)->this_hdr.sh_type
	       != SHT_STRTAB)
	      || strcmp (bfd_get_section_name (abfd, o), ".dynstr") != 0)
	    {
	      if (! bfd_set_section_contents (abfd, o->output_section,
					      o->contents, o->output_offset,
					      o->_raw_size))
		goto error_return;
	    }
	  else
	    {
	      file_ptr off;

	      /* The contents of the .dynstr section are actually in a
                 stringtab.  */
	      off = elf_section_data (o->output_section)->this_hdr.sh_offset;
	      if (bfd_seek (abfd, off, SEEK_SET) != 0
		  || ! _bfd_stringtab_emit (abfd,
					    elf_hash_table (info)->dynstr))
		goto error_return;
	    }
	}
    }

  if (finfo.symstrtab != NULL)
    _bfd_stringtab_free (finfo.symstrtab);
  if (finfo.contents != NULL)
    free (finfo.contents);
  if (finfo.external_relocs != NULL)
    free (finfo.external_relocs);
  if (finfo.internal_relocs != NULL)
    free (finfo.internal_relocs);
  if (finfo.external_syms != NULL)
    free (finfo.external_syms);
  if (finfo.internal_syms != NULL)
    free (finfo.internal_syms);
  if (finfo.indices != NULL)
    free (finfo.indices);
  if (finfo.sections != NULL)
    free (finfo.sections);
  if (finfo.symbuf != NULL)
    free (finfo.symbuf);
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0
	  && elf_section_data (o)->rel_hashes != NULL)
	free (elf_section_data (o)->rel_hashes);
    }

  elf_tdata (abfd)->linker = true;

  return true;

 error_return:
  if (finfo.symstrtab != NULL)
    _bfd_stringtab_free (finfo.symstrtab);
  if (finfo.contents != NULL)
    free (finfo.contents);
  if (finfo.external_relocs != NULL)
    free (finfo.external_relocs);
  if (finfo.internal_relocs != NULL)
    free (finfo.internal_relocs);
  if (finfo.external_syms != NULL)
    free (finfo.external_syms);
  if (finfo.internal_syms != NULL)
    free (finfo.internal_syms);
  if (finfo.indices != NULL)
    free (finfo.indices);
  if (finfo.sections != NULL)
    free (finfo.sections);
  if (finfo.symbuf != NULL)
    free (finfo.symbuf);
  for (o = abfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_RELOC) != 0
	  && elf_section_data (o)->rel_hashes != NULL)
	free (elf_section_data (o)->rel_hashes);
    }

  return false;
}

/* Add a symbol to the output symbol table.  */

static boolean
elf_link_output_sym (finfo, name, elfsym, input_sec)
     struct elf_final_link_info *finfo;
     const char *name;
     Elf_Internal_Sym *elfsym;
     asection *input_sec;
{
  boolean (*output_symbol_hook) PARAMS ((bfd *,
					 struct bfd_link_info *info,
					 const char *,
					 Elf_Internal_Sym *,
					 asection *));

  output_symbol_hook = get_elf_backend_data (finfo->output_bfd)->
    elf_backend_link_output_symbol_hook;
  if (output_symbol_hook != NULL)
    {
      if (! ((*output_symbol_hook)
	     (finfo->output_bfd, finfo->info, name, elfsym, input_sec)))
	return false;
    }

  if (name == (const char *) NULL || *name == '\0')
    elfsym->st_name = 0;
  else
    {
      elfsym->st_name = (unsigned long) _bfd_stringtab_add (finfo->symstrtab,
							    name, true,
							    false);
      if (elfsym->st_name == (unsigned long) -1)
	return false;
    }

  if (finfo->symbuf_count >= finfo->symbuf_size)
    {
      if (! elf_link_flush_output_syms (finfo))
	return false;
    }

  elf_swap_symbol_out (finfo->output_bfd, elfsym,
		       (PTR) (finfo->symbuf + finfo->symbuf_count));
  ++finfo->symbuf_count;

  ++finfo->output_bfd->symcount;

  return true;
}

/* Flush the output symbols to the file.  */

static boolean
elf_link_flush_output_syms (finfo)
     struct elf_final_link_info *finfo;
{
  Elf_Internal_Shdr *symtab;

  symtab = &elf_tdata (finfo->output_bfd)->symtab_hdr;

  if (bfd_seek (finfo->output_bfd, symtab->sh_offset + symtab->sh_size,
		SEEK_SET) != 0
      || (bfd_write ((PTR) finfo->symbuf, finfo->symbuf_count,
		     sizeof (Elf_External_Sym), finfo->output_bfd)
	  != finfo->symbuf_count * sizeof (Elf_External_Sym)))
    return false;

  symtab->sh_size += finfo->symbuf_count * sizeof (Elf_External_Sym);

  finfo->symbuf_count = 0;

  return true;
}

/* Add an external symbol to the symbol table.  This is called from
   the hash table traversal routine.  */

static boolean
elf_link_output_extsym (h, data)
     struct elf_link_hash_entry *h;
     PTR data;
{
  struct elf_finfo_failed *eif = (struct elf_finfo_failed *) data;
  struct elf_final_link_info *finfo = eif->finfo;
  boolean strip;
  Elf_Internal_Sym sym;
  asection *input_sec;

  /* If we are not creating a shared library, and this symbol is
     referenced by a shared library but is not defined anywhere, then
     warn that it is undefined.  If we do not do this, the runtime
     linker will complain that the symbol is undefined when the
     program is run.  We don't have to worry about symbols that are
     referenced by regular files, because we will already have issued
     warnings for them.  */
  if (! finfo->info->relocateable
      && ! finfo->info->shared
      && h->root.type == bfd_link_hash_undefined
      && (h->elf_link_hash_flags & ELF_LINK_HASH_REF_DYNAMIC) != 0
      && (h->elf_link_hash_flags & ELF_LINK_HASH_REF_REGULAR) == 0)
    {
      if (! ((*finfo->info->callbacks->undefined_symbol)
	     (finfo->info, h->root.root.string, h->root.u.undef.abfd,
	      (asection *) NULL, 0)))
	{
	  eif->failed = true;
	  return false;
	}
    }

  /* We don't want to output symbols that have never been mentioned by
     a regular file, or that we have been told to strip.  However, if
     h->indx is set to -2, the symbol is used by a reloc and we must
     output it.  */
  if (h->indx == -2)
    strip = false;
  else if (((h->elf_link_hash_flags & ELF_LINK_HASH_DEF_DYNAMIC) != 0
	    || (h->elf_link_hash_flags & ELF_LINK_HASH_REF_DYNAMIC) != 0)
	   && (h->elf_link_hash_flags & ELF_LINK_HASH_DEF_REGULAR) == 0
	   && (h->elf_link_hash_flags & ELF_LINK_HASH_REF_REGULAR) == 0)
    strip = true;
  else if (finfo->info->strip == strip_all
	   || (finfo->info->strip == strip_some
	       && bfd_hash_lookup (finfo->info->keep_hash,
				   h->root.root.string,
				   false, false) == NULL))
    strip = true;
  else
    strip = false;

  /* If we're stripping it, and it's not a dynamic symbol, there's
     nothing else to do.  */
  if (strip && h->dynindx == -1)
    return true;

  sym.st_value = 0;
  sym.st_size = h->size;
  sym.st_other = 0;
  if (h->root.type == bfd_link_hash_undefweak
      || h->root.type == bfd_link_hash_defweak)
    sym.st_info = ELF_ST_INFO (STB_WEAK, h->type);
  else
    sym.st_info = ELF_ST_INFO (STB_GLOBAL, h->type);

  switch (h->root.type)
    {
    default:
    case bfd_link_hash_new:
      abort ();
      return false;

    case bfd_link_hash_undefined:
      input_sec = bfd_und_section_ptr;
      sym.st_shndx = SHN_UNDEF;
      break;

    case bfd_link_hash_undefweak:
      input_sec = bfd_und_section_ptr;
      sym.st_shndx = SHN_UNDEF;
      break;

    case bfd_link_hash_defined:
    case bfd_link_hash_defweak:
      {
	input_sec = h->root.u.def.section;
	if (input_sec->output_section != NULL)
	  {
	    sym.st_shndx =
	      _bfd_elf_section_from_bfd_section (finfo->output_bfd,
						 input_sec->output_section);
	    if (sym.st_shndx == (unsigned short) -1)
	      {
		eif->failed = true;
		return false;
	      }

	    /* ELF symbols in relocateable files are section relative,
	       but in nonrelocateable files they are virtual
	       addresses.  */
	    sym.st_value = h->root.u.def.value + input_sec->output_offset;
	    if (! finfo->info->relocateable)
	      sym.st_value += input_sec->output_section->vma;
	  }
	else
	  {
	    BFD_ASSERT ((bfd_get_flavour (input_sec->owner)
			 == bfd_target_elf_flavour)
			&& elf_elfheader (input_sec->owner)->e_type == ET_DYN);
	    sym.st_shndx = SHN_UNDEF;
	    input_sec = bfd_und_section_ptr;
	  }
      }
      break;

    case bfd_link_hash_common:
      input_sec = bfd_com_section_ptr;
      sym.st_shndx = SHN_COMMON;
      sym.st_value = 1 << h->root.u.c.p->alignment_power;
      break;

    case bfd_link_hash_indirect:
    case bfd_link_hash_warning:
      return (elf_link_output_extsym
	      ((struct elf_link_hash_entry *) h->root.u.i.link, data));
    }

  /* If this symbol should be put in the .dynsym section, then put it
     there now.  We have already know the symbol index.  We also fill
     in the entry in the .hash section.  */
  if (h->dynindx != -1
      && elf_hash_table (finfo->info)->dynamic_sections_created)
    {
      struct elf_backend_data *bed;
      size_t bucketcount;
      size_t bucket;
      bfd_byte *bucketpos;
      bfd_vma chain;

      sym.st_name = h->dynstr_index;

      /* Give the processor backend a chance to tweak the symbol
	 value, and also to finish up anything that needs to be done
	 for this symbol.  */
      bed = get_elf_backend_data (finfo->output_bfd);
      if (! ((*bed->elf_backend_finish_dynamic_symbol)
	     (finfo->output_bfd, finfo->info, h, &sym)))
	{
	  eif->failed = true;
	  return false;
	}

      elf_swap_symbol_out (finfo->output_bfd, &sym,
			   (PTR) (((Elf_External_Sym *)
				   finfo->dynsym_sec->contents)
				  + h->dynindx));

      bucketcount = elf_hash_table (finfo->info)->bucketcount;
      bucket = (bfd_elf_hash ((const unsigned char *) h->root.root.string)
		% bucketcount);
      bucketpos = ((bfd_byte *) finfo->hash_sec->contents
		   + (bucket + 2) * (ARCH_SIZE / 8));
      chain = get_word (finfo->output_bfd, bucketpos);
      put_word (finfo->output_bfd, h->dynindx, bucketpos);
      put_word (finfo->output_bfd, chain,
		((bfd_byte *) finfo->hash_sec->contents
		 + (bucketcount + 2 + h->dynindx) * (ARCH_SIZE / 8)));
    }

  /* If we're stripping it, then it was just a dynamic symbol, and
     there's nothing else to do.  */
  if (strip)
    return true;

  h->indx = finfo->output_bfd->symcount;

  if (! elf_link_output_sym (finfo, h->root.root.string, &sym, input_sec))
    {
      eif->failed = true;
      return false;
    }

  return true;
}

/* Link an input file into the linker output file.  This function
   handles all the sections and relocations of the input file at once.
   This is so that we only have to read the local symbols once, and
   don't have to keep them in memory.  */

static boolean
elf_link_input_bfd (finfo, input_bfd)
     struct elf_final_link_info *finfo;
     bfd *input_bfd;
{
  boolean (*relocate_section) PARAMS ((bfd *, struct bfd_link_info *,
				       bfd *, asection *, bfd_byte *,
				       Elf_Internal_Rela *,
				       Elf_Internal_Sym *, asection **));
  bfd *output_bfd;
  Elf_Internal_Shdr *symtab_hdr;
  size_t locsymcount;
  size_t extsymoff;
  Elf_External_Sym *esym;
  Elf_External_Sym *esymend;
  Elf_Internal_Sym *isym;
  long *pindex;
  asection **ppsection;
  asection *o;

  output_bfd = finfo->output_bfd;
  relocate_section =
    get_elf_backend_data (output_bfd)->elf_backend_relocate_section;

  /* If this is a dynamic object, we don't want to do anything here:
     we don't want the local symbols, and we don't want the section
     contents.  */
  if (elf_elfheader (input_bfd)->e_type == ET_DYN)
    return true;

  symtab_hdr = &elf_tdata (input_bfd)->symtab_hdr;
  if (elf_bad_symtab (input_bfd))
    {
      locsymcount = symtab_hdr->sh_size / sizeof (Elf_External_Sym);
      extsymoff = 0;
    }
  else
    {
      locsymcount = symtab_hdr->sh_info;
      extsymoff = symtab_hdr->sh_info;
    }

  /* Read the local symbols.  */
  if (locsymcount > 0
      && (bfd_seek (input_bfd, symtab_hdr->sh_offset, SEEK_SET) != 0
      	  || (bfd_read (finfo->external_syms, sizeof (Elf_External_Sym),
			locsymcount, input_bfd)
	      != locsymcount * sizeof (Elf_External_Sym))))
    return false;

  /* Swap in the local symbols and write out the ones which we know
     are going into the output file.  */
  esym = finfo->external_syms;
  esymend = esym + locsymcount;
  isym = finfo->internal_syms;
  pindex = finfo->indices;
  ppsection = finfo->sections;
  for (; esym < esymend; esym++, isym++, pindex++, ppsection++)
    {
      asection *isec;
      const char *name;
      Elf_Internal_Sym osym;

      elf_swap_symbol_in (input_bfd, esym, isym);
      *pindex = -1;

      if (elf_bad_symtab (input_bfd))
	{
	  if (ELF_ST_BIND (isym->st_info) != STB_LOCAL)
	    {
	      *ppsection = NULL;
	      continue;
	    }
	}

      if (isym->st_shndx == SHN_UNDEF)
	isec = bfd_und_section_ptr;
      else if (isym->st_shndx > 0 && isym->st_shndx < SHN_LORESERVE)
	isec = section_from_elf_index (input_bfd, isym->st_shndx);
      else if (isym->st_shndx == SHN_ABS)
	isec = bfd_abs_section_ptr;
      else if (isym->st_shndx == SHN_COMMON)
	isec = bfd_com_section_ptr;
      else
	{
	  /* Who knows?  */
	  isec = NULL;
	}

      *ppsection = isec;

      /* Don't output the first, undefined, symbol.  */
      if (esym == finfo->external_syms)
	continue;

      /* If we are stripping all symbols, we don't want to output this
	 one.  */
      if (finfo->info->strip == strip_all)
	continue;

      /* We never output section symbols.  Instead, we use the section
	 symbol of the corresponding section in the output file.  */
      if (ELF_ST_TYPE (isym->st_info) == STT_SECTION)
	continue;

      /* If we are discarding all local symbols, we don't want to
	 output this one.  If we are generating a relocateable output
	 file, then some of the local symbols may be required by
	 relocs; we output them below as we discover that they are
	 needed.  */
      if (finfo->info->discard == discard_all)
	continue;

      /* Get the name of the symbol.  */
      name = bfd_elf_string_from_elf_section (input_bfd, symtab_hdr->sh_link,
					  isym->st_name);
      if (name == NULL)
	return false;

      /* See if we are discarding symbols with this name.  */
      if ((finfo->info->strip == strip_some
	   && (bfd_hash_lookup (finfo->info->keep_hash, name, false, false)
	       == NULL))
	  || (finfo->info->discard == discard_l
	      && strncmp (name, finfo->info->lprefix,
			  finfo->info->lprefix_len) == 0))
	continue;

      /* If we get here, we are going to output this symbol.  */

      osym = *isym;

      /* Adjust the section index for the output file.  */
      osym.st_shndx = _bfd_elf_section_from_bfd_section (output_bfd,
							 isec->output_section);
      if (osym.st_shndx == (unsigned short) -1)
	return false;

      *pindex = output_bfd->symcount;

      /* ELF symbols in relocateable files are section relative, but
	 in executable files they are virtual addresses.  Note that
	 this code assumes that all ELF sections have an associated
	 BFD section with a reasonable value for output_offset; below
	 we assume that they also have a reasonable value for
	 output_section.  Any special sections must be set up to meet
	 these requirements.  */
      osym.st_value += isec->output_offset;
      if (! finfo->info->relocateable)
	osym.st_value += isec->output_section->vma;

      if (! elf_link_output_sym (finfo, name, &osym, isec))
	return false;
    }

  /* Relocate the contents of each section.  */
  for (o = input_bfd->sections; o != NULL; o = o->next)
    {
      if ((o->flags & SEC_HAS_CONTENTS) == 0)
	continue;

      if ((o->flags & SEC_IN_MEMORY) != 0
	  && input_bfd == elf_hash_table (finfo->info)->dynobj)
	{
	  /* Section was created by elf_link_create_dynamic_sections.
             FIXME: This test is fragile.  */
	  continue;
	}

      /* Read the contents of the section.  */
      if (! bfd_get_section_contents (input_bfd, o, finfo->contents,
				      (file_ptr) 0, o->_raw_size))
	return false;

      if ((o->flags & SEC_RELOC) != 0)
	{
	  Elf_Internal_Rela *internal_relocs;

	  /* Get the swapped relocs.  */
	  internal_relocs = elf_link_read_relocs (input_bfd, o,
						  finfo->external_relocs,
						  finfo->internal_relocs,
						  false);
	  if (internal_relocs == NULL
	      && o->reloc_count > 0)
	    return false;

	  /* Relocate the section by invoking a back end routine.

	     The back end routine is responsible for adjusting the
	     section contents as necessary, and (if using Rela relocs
	     and generating a relocateable output file) adjusting the
	     reloc addend as necessary.

	     The back end routine does not have to worry about setting
	     the reloc address or the reloc symbol index.

	     The back end routine is given a pointer to the swapped in
	     internal symbols, and can access the hash table entries
	     for the external symbols via elf_sym_hashes (input_bfd).

	     When generating relocateable output, the back end routine
	     must handle STB_LOCAL/STT_SECTION symbols specially.  The
	     output symbol is going to be a section symbol
	     corresponding to the output section, which will require
	     the addend to be adjusted.  */

	  if (! (*relocate_section) (output_bfd, finfo->info,
				     input_bfd, o,
				     finfo->contents,
				     internal_relocs,
				     finfo->internal_syms,
				     finfo->sections))
	    return false;

	  if (finfo->info->relocateable)
	    {
	      Elf_Internal_Rela *irela;
	      Elf_Internal_Rela *irelaend;
	      struct elf_link_hash_entry **rel_hash;
	      Elf_Internal_Shdr *input_rel_hdr;
	      Elf_Internal_Shdr *output_rel_hdr;

	      /* Adjust the reloc addresses and symbol indices.  */

	      irela = internal_relocs;
	      irelaend = irela + o->reloc_count;
	      rel_hash = (elf_section_data (o->output_section)->rel_hashes
			  + o->output_section->reloc_count);
	      for (; irela < irelaend; irela++, rel_hash++)
		{
		  unsigned long r_symndx;
		  Elf_Internal_Sym *isym;
		  asection *sec;

		  irela->r_offset += o->output_offset;

		  r_symndx = ELF_R_SYM (irela->r_info);

		  if (r_symndx == 0)
		    continue;

		  if (r_symndx >= locsymcount
		      || (elf_bad_symtab (input_bfd)
			  && finfo->sections[r_symndx] == NULL))
		    {
		      long indx;

		      /* This is a reloc against a global symbol.  We
			 have not yet output all the local symbols, so
			 we do not know the symbol index of any global
			 symbol.  We set the rel_hash entry for this
			 reloc to point to the global hash table entry
			 for this symbol.  The symbol index is then
			 set at the end of elf_bfd_final_link.  */
		      indx = r_symndx - extsymoff;
		      *rel_hash = elf_sym_hashes (input_bfd)[indx];

		      /* Setting the index to -2 tells
			 elf_link_output_extsym that this symbol is
			 used by a reloc.  */
		      BFD_ASSERT ((*rel_hash)->indx < 0);
		      (*rel_hash)->indx = -2;

		      continue;
		    }

		  /* This is a reloc against a local symbol. */

		  *rel_hash = NULL;
		  isym = finfo->internal_syms + r_symndx;
		  sec = finfo->sections[r_symndx];
		  if (ELF_ST_TYPE (isym->st_info) == STT_SECTION)
		    {
		      /* I suppose the backend ought to fill in the
			 section of any STT_SECTION symbol against a
			 processor specific section.  */
		      if (sec != NULL && bfd_is_abs_section (sec))
			r_symndx = 0;
		      else if (sec == NULL || sec->owner == NULL)
			{
			  bfd_set_error (bfd_error_bad_value);
			  return false;
			}
		      else
			{
			  r_symndx = sec->output_section->target_index;
			  BFD_ASSERT (r_symndx != 0);
			}
		    }
		  else
		    {
		      if (finfo->indices[r_symndx] == -1)
			{
			  unsigned long link;
			  const char *name;
			  asection *osec;

			  if (finfo->info->strip == strip_all)
			    {
			      /* You can't do ld -r -s.  */
			      bfd_set_error (bfd_error_invalid_operation);
			      return false;
			    }

			  /* This symbol was skipped earlier, but
			     since it is needed by a reloc, we
			     must output it now.  */
			  link = symtab_hdr->sh_link;
			  name = bfd_elf_string_from_elf_section (input_bfd,
								  link,
								  isym->st_name);
			  if (name == NULL)
			    return false;

			  osec = sec->output_section;
			  isym->st_shndx =
			    _bfd_elf_section_from_bfd_section (output_bfd,
							       osec);
			  if (isym->st_shndx == (unsigned short) -1)
			    return false;

			  isym->st_value += sec->output_offset;
			  if (! finfo->info->relocateable)
			    isym->st_value += osec->vma;

			  finfo->indices[r_symndx] = output_bfd->symcount;

			  if (! elf_link_output_sym (finfo, name, isym, sec))
			    return false;
			}

		      r_symndx = finfo->indices[r_symndx];
		    }

		  irela->r_info = ELF_R_INFO (r_symndx,
					      ELF_R_TYPE (irela->r_info));
		}

	      /* Swap out the relocs.  */
	      input_rel_hdr = &elf_section_data (o)->rel_hdr;
	      output_rel_hdr = &elf_section_data (o->output_section)->rel_hdr;
	      BFD_ASSERT (output_rel_hdr->sh_entsize
			  == input_rel_hdr->sh_entsize);
	      irela = internal_relocs;
	      irelaend = irela + o->reloc_count;
	      if (input_rel_hdr->sh_entsize == sizeof (Elf_External_Rel))
		{
		  Elf_External_Rel *erel;

		  erel = ((Elf_External_Rel *) output_rel_hdr->contents
			  + o->output_section->reloc_count);
		  for (; irela < irelaend; irela++, erel++)
		    {
		      Elf_Internal_Rel irel;

		      irel.r_offset = irela->r_offset;
		      irel.r_info = irela->r_info;
		      BFD_ASSERT (irela->r_addend == 0);
		      elf_swap_reloc_out (output_bfd, &irel, erel);
		    }
		}
	      else
		{
		  Elf_External_Rela *erela;

		  BFD_ASSERT (input_rel_hdr->sh_entsize
			      == sizeof (Elf_External_Rela));
		  erela = ((Elf_External_Rela *) output_rel_hdr->contents
			   + o->output_section->reloc_count);
		  for (; irela < irelaend; irela++, erela++)
		    elf_swap_reloca_out (output_bfd, irela, erela);
		}

	      o->output_section->reloc_count += o->reloc_count;
	    }
	}

      /* Write out the modified section contents.  */
      if (! bfd_set_section_contents (output_bfd, o->output_section,
				      finfo->contents, o->output_offset,
				      (o->_cooked_size != 0
				       ? o->_cooked_size
				       : o->_raw_size)))
	return false;
    }

  return true;
}

/* Generate a reloc when linking an ELF file.  This is a reloc
   requested by the linker, and does come from any input file.  This
   is used to build constructor and destructor tables when linking
   with -Ur.  */

static boolean
elf_reloc_link_order (output_bfd, info, output_section, link_order)
     bfd *output_bfd;
     struct bfd_link_info *info;
     asection *output_section;
     struct bfd_link_order *link_order;
{
  reloc_howto_type *howto;
  long indx;
  bfd_vma offset;
  struct elf_link_hash_entry **rel_hash_ptr;
  Elf_Internal_Shdr *rel_hdr;

  howto = bfd_reloc_type_lookup (output_bfd, link_order->u.reloc.p->reloc);
  if (howto == NULL)
    {
      bfd_set_error (bfd_error_bad_value);
      return false;
    }

  /* If this is an inplace reloc, we must write the addend into the
     object file.  */
  if (howto->partial_inplace
      && link_order->u.reloc.p->addend != 0)
    {
      bfd_size_type size;
      bfd_reloc_status_type rstat;
      bfd_byte *buf;
      boolean ok;

      size = bfd_get_reloc_size (howto);
      buf = (bfd_byte *) bfd_zmalloc (size);
      if (buf == (bfd_byte *) NULL)
	{
	  bfd_set_error (bfd_error_no_memory);
	  return false;
	}
      rstat = _bfd_relocate_contents (howto, output_bfd,
				      link_order->u.reloc.p->addend, buf);
      switch (rstat)
	{
	case bfd_reloc_ok:
	  break;
	default:
	case bfd_reloc_outofrange:
	  abort ();
	case bfd_reloc_overflow:
	  if (! ((*info->callbacks->reloc_overflow)
		 (info,
		  (link_order->type == bfd_section_reloc_link_order
		   ? bfd_section_name (output_bfd,
				       link_order->u.reloc.p->u.section)
		   : link_order->u.reloc.p->u.name),
		  howto->name, link_order->u.reloc.p->addend,
		  (bfd *) NULL, (asection *) NULL, (bfd_vma) 0)))
	    {
	      free (buf);
	      return false;
	    }
	  break;
	}
      ok = bfd_set_section_contents (output_bfd, output_section, (PTR) buf,
				     (file_ptr) link_order->offset, size);
      free (buf);
      if (! ok)
	return false;
    }

  /* Figure out the symbol index.  */
  rel_hash_ptr = (elf_section_data (output_section)->rel_hashes
		  + output_section->reloc_count);
  if (link_order->type == bfd_section_reloc_link_order)
    {
      indx = link_order->u.reloc.p->u.section->target_index;
      BFD_ASSERT (indx != 0);
      *rel_hash_ptr = NULL;
    }
  else
    {
      struct elf_link_hash_entry *h;

      h = elf_link_hash_lookup (elf_hash_table (info),
				link_order->u.reloc.p->u.name,
				false, false, true);
      if (h != NULL)
	{
	  /* Setting the index to -2 tells elf_link_output_extsym that
	     this symbol is used by a reloc.  */
	  h->indx = -2;
	  *rel_hash_ptr = h;
	  indx = 0;
	}
      else
	{
	  if (! ((*info->callbacks->unattached_reloc)
		 (info, link_order->u.reloc.p->u.name, (bfd *) NULL,
		  (asection *) NULL, (bfd_vma) 0)))
	    return false;
	  indx = 0;
	}
    }

  /* The address of a reloc is relative to the section in a
     relocateable file, and is a virtual address in an executable
     file.  */
  offset = link_order->offset;
  if (! info->relocateable)
    offset += output_section->vma;

  rel_hdr = &elf_section_data (output_section)->rel_hdr;

  if (rel_hdr->sh_type == SHT_REL)
    {
      Elf_Internal_Rel irel;
      Elf_External_Rel *erel;

      irel.r_offset = offset;
      irel.r_info = ELF_R_INFO (indx, howto->type);
      erel = ((Elf_External_Rel *) rel_hdr->contents
	      + output_section->reloc_count);
      elf_swap_reloc_out (output_bfd, &irel, erel);
    }
  else
    {
      Elf_Internal_Rela irela;
      Elf_External_Rela *erela;

      irela.r_offset = offset;
      irela.r_info = ELF_R_INFO (indx, howto->type);
      irela.r_addend = link_order->u.reloc.p->addend;
      erela = ((Elf_External_Rela *) rel_hdr->contents
	       + output_section->reloc_count);
      elf_swap_reloca_out (output_bfd, &irela, erela);
    }

  ++output_section->reloc_count;

  return true;
}

