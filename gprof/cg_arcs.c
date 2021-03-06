/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */
#include "libiberty.h"
#include "gprof.h"
#include "call_graph.h"
#include "cg_arcs.h"
#include "cg_dfn.h"
#include "cg_print.h"
#include "utils.h"
#include "sym_ids.h"

Sym *cycle_header;
int num_cycles;

/*
 * Return TRUE iff PARENT has an arc to covers the address
 * range covered by CHILD.
 */
Arc *
DEFUN (arc_lookup, (parent, child), Sym * parent AND Sym * child)
{
  Arc *arc;

  if (!parent || !child)
    {
      printf ("[arc_lookup] parent == 0 || child == 0\n");
      return 0;
    }
  DBG (LOOKUPDEBUG, printf ("[arc_lookup] parent %s child %s\n",
			    parent->name, child->name));
  for (arc = parent->cg.children; arc; arc = arc->next_child)
    {
      DBG (LOOKUPDEBUG, printf ("[arc_lookup]\t parent %s child %s\n",
				arc->parent->name, arc->child->name));
      if (child->addr >= arc->child->addr
	  && child->end_addr <= arc->child->end_addr)
	{
	  return arc;
	}
    }
  return 0;
}


/*
 * Add (or just increment) an arc:
 */
void
DEFUN (arc_add, (parent, child, count),
       Sym * parent AND Sym * child AND int count)
{
  Arc *arc;

  DBG (TALLYDEBUG, printf ("[arc_add] %d arcs from %s to %s\n",
			   count, parent->name, child->name));
  arc = arc_lookup (parent, child);
  if (arc)
    {
      /*
       * A hit: just increment the count.
       */
      DBG (TALLYDEBUG, printf ("[tally] hit %d += %d\n",
			       arc->count, count));
      arc->count += count;
      return;
    }
  arc = (Arc *) xmalloc (sizeof (*arc));
  arc->parent = parent;
  arc->child = child;
  arc->count = count;

  /* prepend this child to the children of this parent: */
  arc->next_child = parent->cg.children;
  parent->cg.children = arc;

  /* prepend this parent to the parents of this child: */
  arc->next_parent = child->cg.parents;
  child->cg.parents = arc;
}


static int
DEFUN (cmp_topo, (lp, rp), const PTR lp AND const PTR rp)
{
  const Sym *left = *(const Sym **) lp;
  const Sym *right = *(const Sym **) rp;

  return left->cg.top_order - right->cg.top_order;
}


static void
DEFUN (propagate_time, (parent), Sym * parent)
{
  Arc *arc;
  Sym *child;
  double share, prop_share;

  if (parent->cg.prop.fract == 0.0)
    {
      return;
    }

  /* gather time from children of this parent: */

  for (arc = parent->cg.children; arc; arc = arc->next_child)
    {
      child = arc->child;
      if (arc->count == 0 || child == parent || child->cg.prop.fract == 0)
	{
	  continue;
	}
      if (child->cg.cyc.head != child)
	{
	  if (parent->cg.cyc.num == child->cg.cyc.num)
	    {
	      continue;
	    }
	  if (parent->cg.top_order <= child->cg.top_order)
	    {
	      fprintf (stderr, "[propagate] toporder botches\n");
	    }
	  child = child->cg.cyc.head;
	}
      else
	{
	  if (parent->cg.top_order <= child->cg.top_order)
	    {
	      fprintf (stderr, "[propagate] toporder botches\n");
	      continue;
	    }
	}
      if (child->ncalls == 0)
	{
	  continue;
	}

      /* distribute time for this arc: */
      arc->time = child->hist.time * (((double) arc->count)
				      / ((double) child->ncalls));
      arc->child_time = child->cg.child_time
	* (((double) arc->count) / ((double) child->ncalls));
      share = arc->time + arc->child_time;
      parent->cg.child_time += share;

      /* (1 - cg.prop.fract) gets lost along the way: */
      prop_share = parent->cg.prop.fract * share;

      /* fix things for printing: */
      parent->cg.prop.child += prop_share;
      arc->time *= parent->cg.prop.fract;
      arc->child_time *= parent->cg.prop.fract;

      /* add this share to the parent's cycle header, if any: */
      if (parent->cg.cyc.head != parent)
	{
	  parent->cg.cyc.head->cg.child_time += share;
	  parent->cg.cyc.head->cg.prop.child += prop_share;
	}
      DBG (PROPDEBUG,
	   printf ("[prop_time] child \t");
	   print_name (child);
	   printf (" with %f %f %d/%d\n", child->hist.time,
		   child->cg.child_time, arc->count, child->ncalls);
	   printf ("[prop_time] parent\t");
	   print_name (parent);
	   printf ("\n[prop_time] share %f\n", share));
    }
}


/*
 * Compute the time of a cycle as the sum of the times of all
 * its members.
 */
static void
DEFUN_VOID (cycle_time)
{
  Sym *member, *cyc;

  for (cyc = &cycle_header[1]; cyc <= &cycle_header[num_cycles]; ++cyc)
    {
      for (member = cyc->cg.cyc.next; member; member = member->cg.cyc.next)
	{
	  if (member->cg.prop.fract == 0.0)
	    {
	      /*
	       * All members have the same propfraction except those
	       * that were excluded with -E.
	       */
	      continue;
	    }
	  cyc->hist.time += member->hist.time;
	}
      cyc->cg.prop.self = cyc->cg.prop.fract * cyc->hist.time;
    }
}


static void
DEFUN_VOID (cycle_link)
{
  Sym *sym, *cyc, *member;
  Arc *arc;
  int num;

  /* count the number of cycles, and initialize the cycle lists: */

  num_cycles = 0;
  for (sym = symtab.base; sym < symtab.limit; ++sym)
    {
      /* this is how you find unattached cycles: */
      if (sym->cg.cyc.head == sym && sym->cg.cyc.next)
	{
	  ++num_cycles;
	}
    }

  /*
   * cycle_header is indexed by cycle number: i.e. it is origin 1,
   * not origin 0.
   */
  cycle_header = (Sym *) xmalloc ((num_cycles + 1) * sizeof (Sym));

  /*
   * Now link cycles to true cycle-heads, number them, accumulate
   * the data for the cycle.
   */
  num = 0;
  cyc = cycle_header;
  for (sym = symtab.base; sym < symtab.limit; ++sym)
    {
      if (!(sym->cg.cyc.head == sym && sym->cg.cyc.next != 0))
	{
	  continue;
	}
      ++num;
      ++cyc;
      sym_init (cyc);
      cyc->cg.print_flag = TRUE;	/* should this be printed? */
      cyc->cg.top_order = DFN_NAN;	/* graph call chain top-sort order */
      cyc->cg.cyc.num = num;	/* internal number of cycle on */
      cyc->cg.cyc.head = cyc;	/* pointer to head of cycle */
      cyc->cg.cyc.next = sym;	/* pointer to next member of cycle */
      DBG (CYCLEDEBUG, printf ("[cycle_link] ");
	   print_name (sym);
	   printf (" is the head of cycle %d\n", num));

      /* link members to cycle header: */
      for (member = sym; member; member = member->cg.cyc.next)
	{
	  member->cg.cyc.num = num;
	  member->cg.cyc.head = cyc;
	}

      /*
       * Count calls from outside the cycle and those among cycle
       * members:
       */
      for (member = sym; member; member = member->cg.cyc.next)
	{
	  for (arc = member->cg.parents; arc; arc = arc->next_parent)
	    {
	      if (arc->parent == member)
		{
		  continue;
		}
	      if (arc->parent->cg.cyc.num == num)
		{
		  cyc->cg.self_calls += arc->count;
		}
	      else
		{
		  cyc->ncalls += arc->count;
		}
	    }
	}
    }
}


/*
 * Check if any parent of this child (or outside parents of this
 * cycle) have their print flags on and set the print flag of the
 * child (cycle) appropriately.  Similarly, deal with propagation
 * fractions from parents.
 */
static void
DEFUN (inherit_flags, (child), Sym * child)
{
  Sym *head, *parent, *member;
  Arc *arc;

  head = child->cg.cyc.head;
  if (child == head)
    {
      /* just a regular child, check its parents: */
      child->cg.print_flag = FALSE;
      child->cg.prop.fract = 0.0;
      for (arc = child->cg.parents; arc; arc = arc->next_parent)
	{
	  parent = arc->parent;
	  if (child == parent)
	    {
	      continue;
	    }
	  child->cg.print_flag |= parent->cg.print_flag;
	  /*
	   * If the child was never actually called (e.g., this arc
	   * is static (and all others are, too)) no time propagates
	   * along this arc.
	   */
	  if (child->ncalls)
	    {
	      child->cg.prop.fract += parent->cg.prop.fract
		* (((double) arc->count) / ((double) child->ncalls));
	    }
	}
    }
  else
    {
      /*
       * Its a member of a cycle, look at all parents from outside
       * the cycle.
       */
      head->cg.print_flag = FALSE;
      head->cg.prop.fract = 0.0;
      for (member = head->cg.cyc.next; member; member = member->cg.cyc.next)
	{
	  for (arc = member->cg.parents; arc; arc = arc->next_parent)
	    {
	      if (arc->parent->cg.cyc.head == head)
		{
		  continue;
		}
	      parent = arc->parent;
	      head->cg.print_flag |= parent->cg.print_flag;
	      /*
	       * If the cycle was never actually called (e.g. this
	       * arc is static (and all others are, too)) no time
	       * propagates along this arc.
	       */
	      if (head->ncalls)
		{
		  head->cg.prop.fract += parent->cg.prop.fract
		    * (((double) arc->count) / ((double) head->ncalls));
		}
	    }
	}
      for (member = head; member; member = member->cg.cyc.next)
	{
	  member->cg.print_flag = head->cg.print_flag;
	  member->cg.prop.fract = head->cg.prop.fract;
	}
    }
}


/*
 * In one top-to-bottom pass over the topologically sorted symbols
 * propagate:
 *      cg.print_flag as the union of parents' print_flags
 *      propfraction as the sum of fractional parents' propfractions
 * and while we're here, sum time for functions.
 */
static void
DEFUN (propagate_flags, (symbols), Sym ** symbols)
{
  int index;
  Sym *old_head, *child;

  old_head = 0;
  for (index = symtab.len - 1; index >= 0; --index)
    {
      child = symbols[index];
      /*
       * If we haven't done this function or cycle, inherit things
       * from parent.  This way, we are linear in the number of arcs
       * since we do all members of a cycle (and the cycle itself)
       * as we hit the first member of the cycle.
       */
      if (child->cg.cyc.head != old_head)
	{
	  old_head = child->cg.cyc.head;
	  inherit_flags (child);
	}
      DBG (PROPDEBUG,
	   printf ("[prop_flags] ");
	   print_name (child);
	   printf ("inherits print-flag %d and prop-fract %f\n",
		   child->cg.print_flag, child->cg.prop.fract));
      if (!child->cg.print_flag)
	{
	  /*
	   * Printflag is off. It gets turned on by being in the
	   * INCL_GRAPH table, or there being an empty INCL_GRAPH
	   * table and not being in the EXCL_GRAPH table.
	   */
	  if (sym_lookup (&syms[INCL_GRAPH], child->addr)
	      || (syms[INCL_GRAPH].len == 0
		  && !sym_lookup (&syms[EXCL_GRAPH], child->addr)))
	    {
	      child->cg.print_flag = TRUE;
	    }
	}
      else
	{
	  /*
	   * This function has printing parents: maybe someone wants
	   * to shut it up by putting it in the EXCL_GRAPH table.
	   * (But favor INCL_GRAPH over EXCL_GRAPH.)
	   */
	  if (!sym_lookup (&syms[INCL_GRAPH], child->addr)
	      && sym_lookup (&syms[EXCL_GRAPH], child->addr))
	    {
	      child->cg.print_flag = FALSE;
	    }
	}
      if (child->cg.prop.fract == 0.0)
	{
	  /*
	   * No parents to pass time to.  Collect time from children
	   * if its in the INCL_TIME table, or there is an empty
	   * INCL_TIME table and its not in the EXCL_TIME table.
	   */
	  if (sym_lookup (&syms[INCL_TIME], child->addr)
	      || (syms[INCL_TIME].len == 0
		  && !sym_lookup (&syms[EXCL_TIME], child->addr)))
	    {
	      child->cg.prop.fract = 1.0;
	    }
	}
      else
	{
	  /*
	   * It has parents to pass time to, but maybe someone wants
	   * to shut it up by puttting it in the EXCL_TIME table.
	   * (But favor being in INCL_TIME tabe over being in
	   * EXCL_TIME table.)
	   */
	  if (!sym_lookup (&syms[INCL_TIME], child->addr)
	      && sym_lookup (&syms[EXCL_TIME], child->addr))
	    {
	      child->cg.prop.fract = 0.0;
	    }
	}
      child->cg.prop.self = child->hist.time * child->cg.prop.fract;
      print_time += child->cg.prop.self;
      DBG (PROPDEBUG,
	   printf ("[prop_flags] ");
	   print_name (child);
	   printf (" ends up with printflag %d and prop-fract %f\n",
		   child->cg.print_flag, child->cg.prop.fract);
	   printf ("[prop_flags] time %f propself %f print_time %f\n",
		   child->hist.time, child->cg.prop.self, print_time));
    }
}


/*
 * Compare by decreasing propagated time.  If times are equal, but one
 * is a cycle header, say that's first (e.g. less, i.e. -1).  If one's
 * name doesn't have an underscore and the other does, say that one is
 * first.  All else being equal, compare by names.
 */
static int
DEFUN (cmp_total, (lp, rp), const PTR lp AND const PTR rp)
{
  const Sym *left = *(const Sym **) lp;
  const Sym *right = *(const Sym **) rp;
  double diff;

  diff = (left->cg.prop.self + left->cg.prop.child)
    - (right->cg.prop.self + right->cg.prop.child);
  if (diff < 0.0)
    {
      return 1;
    }
  if (diff > 0.0)
    {
      return -1;
    }
  if (!left->name && left->cg.cyc.num != 0)
    {
      return -1;
    }
  if (!right->name && right->cg.cyc.num != 0)
    {
      return 1;
    }
  if (!left->name)
    {
      return -1;
    }
  if (!right->name)
    {
      return 1;
    }
  if (left->name[0] != '_' && right->name[0] == '_')
    {
      return -1;
    }
  if (left->name[0] == '_' && right->name[0] != '_')
    {
      return 1;
    }
  if (left->ncalls > right->ncalls)
    {
      return -1;
    }
  if (left->ncalls < right->ncalls)
    {
      return 1;
    }
  return strcmp (left->name, right->name);
}


/*
 * Topologically sort the graph (collapsing cycles), and propagates
 * time bottom up and flags top down.
 */
Sym **
DEFUN_VOID (cg_assemble)
{
  Sym *parent, **time_sorted_syms, **top_sorted_syms;
  long index;
  Arc *arc;
  extern void find_call PARAMS ((Sym * parent,
				 bfd_vma p_lowpc, bfd_vma p_highpc));
  /*
   * initialize various things:
   *      zero out child times.
   *      count self-recursive calls.
   *      indicate that nothing is on cycles.
   */
  for (parent = symtab.base; parent < symtab.limit; parent++)
    {
      parent->cg.child_time = 0.0;
      arc = arc_lookup (parent, parent);
      if (arc && parent == arc->child)
	{
	  parent->ncalls -= arc->count;
	  parent->cg.self_calls = arc->count;
	}
      else
	{
	  parent->cg.self_calls = 0;
	}
      parent->cg.prop.fract = 0.0;
      parent->cg.prop.self = 0.0;
      parent->cg.prop.child = 0.0;
      parent->cg.print_flag = FALSE;
      parent->cg.top_order = DFN_NAN;
      parent->cg.cyc.num = 0;
      parent->cg.cyc.head = parent;
      parent->cg.cyc.next = 0;
      if (ignore_direct_calls)
	{
	  find_call (parent, parent->addr, (parent + 1)->addr);
	}
    }
  /*
   * Topologically order things.  If any node is unnumbered, number
   * it and any of its descendents.
   */
  for (parent = symtab.base; parent < symtab.limit; parent++)
    {
      if (parent->cg.top_order == DFN_NAN)
	{
	  cg_dfn (parent);
	}
    }

  /* link together nodes on the same cycle: */
  cycle_link ();

  /* sort the symbol table in reverse topological order: */
  top_sorted_syms = (Sym **) xmalloc (symtab.len * sizeof (Sym *));
  for (index = 0; index < symtab.len; ++index)
    {
      top_sorted_syms[index] = &symtab.base[index];
    }
  qsort (top_sorted_syms, symtab.len, sizeof (Sym *), cmp_topo);
  DBG (DFNDEBUG,
       printf ("[cg_assemble] topological sort listing\n");
       for (index = 0; index < symtab.len; ++index)
       {
       printf ("[cg_assemble] ");
       printf ("%d:", top_sorted_syms[index]->cg.top_order);
       print_name (top_sorted_syms[index]);
       printf ("\n");
       }
  );
  /*
   * Starting from the topological top, propagate print flags to
   * children.  also, calculate propagation fractions.  this happens
   * before time propagation since time propagation uses the
   * fractions.
   */
  propagate_flags (top_sorted_syms);

  /*
   * Starting from the topological bottom, propogate children times
   * up to parents.
   */
  cycle_time ();
  for (index = 0; index < symtab.len; ++index)
    {
      propagate_time (top_sorted_syms[index]);
    }

  free (top_sorted_syms);

  /*
   * Now, sort by CG.PROP.SELF + CG.PROP.CHILD.  Sorting both the regular
   * function names and cycle headers.
   */
  time_sorted_syms = (Sym **) xmalloc ((symtab.len + num_cycles) * sizeof (Sym *));
  for (index = 0; index < symtab.len; index++)
    {
      time_sorted_syms[index] = &symtab.base[index];
    }
  for (index = 1; index <= num_cycles; index++)
    {
      time_sorted_syms[symtab.len + index - 1] = &cycle_header[index];
    }
  qsort (time_sorted_syms, symtab.len + num_cycles, sizeof (Sym *),
	 cmp_total);
  for (index = 0; index < symtab.len + num_cycles; index++)
    {
      time_sorted_syms[index]->cg.index = index + 1;
    }
  return time_sorted_syms;
}
