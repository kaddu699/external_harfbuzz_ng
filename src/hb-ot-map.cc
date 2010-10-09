/*
 * Copyright (C) 2009,2010  Red Hat, Inc.
 * Copyright (C) 2010  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#include "hb-ot-map-private.hh"

#include "hb-ot-shape-private.hh"

HB_BEGIN_DECLS


void
hb_ot_map_t::compile (hb_ot_shape_context_t *c)
{
 global_mask = 0;
 lookup_count[0] = lookup_count[1] = 0;

  if (!feature_count)
    return;


  /* Fetch script/language indices for GSUB/GPOS.  We need these later to skip
   * features not available in either table and not waste precious bits for them. */

  const hb_tag_t *script_tags;
  hb_tag_t language_tag;

  script_tags = hb_ot_tags_from_script (c->buffer->props.script);
  language_tag = hb_ot_tag_from_language (c->buffer->props.language);

  unsigned int script_index[2], language_index[2];
  for (unsigned int table_index = 0; table_index < 2; table_index++) {
    hb_tag_t table_tag = table_tags[table_index];
    hb_ot_layout_table_choose_script (c->face, table_tag, script_tags, &script_index[table_index]);
    hb_ot_layout_script_find_language (c->face, table_tag, script_index[table_index], language_tag, &language_index[table_index]);
  }


  /* Sort features and merge duplicates */
  qsort (feature_infos, feature_count, sizeof (feature_infos[0]), (hb_compare_func_t) feature_info_t::cmp);
  unsigned int j = 0;
  for (unsigned int i = 1; i < feature_count; i++)
    if (feature_infos[i].tag != feature_infos[j].tag)
      feature_infos[++j] = feature_infos[i];
    else {
      if (feature_infos[i].global)
	feature_infos[j] = feature_infos[i];
      else {
	feature_infos[j].global = false;
	feature_infos[j].value = MAX (feature_infos[j].value, feature_infos[i].value);
      }
    }
  feature_count = j + 1;


  /* Allocate bits now */
  unsigned int next_bit = 1;
  j = 0;
  for (unsigned int i = 0; i < feature_count; i++) {
    const feature_info_t *info = &feature_infos[i];

    unsigned int bits_needed;

    if (info->global && info->value == 1)
      /* Uses the global bit */
      bits_needed = 0;
    else
      bits_needed = _hb_bit_storage (info->value);

    if (!info->value || next_bit + bits_needed > 8 * sizeof (hb_mask_t))
      continue; /* Feature disabled, or not enough bits. */


    bool found = false;
    unsigned int feature_index[2];
    for (unsigned int table_index = 0; table_index < 2; table_index++)
      found |= hb_ot_layout_language_find_feature (c->face,
						   table_tags[table_index],
						   script_index[table_index],
						   language_index[table_index],
						   info->tag,
						   &feature_index[table_index]);
    if (!found)
      continue;


    feature_map_t *map = &feature_maps[j++];

    map->tag = info->tag;
    map->index[0] = feature_index[0];
    map->index[1] = feature_index[1];
    if (info->global && info->value == 1) {
      /* Uses the global bit */
      map->shift = 0;
      map->mask = 1;
    } else {
      map->shift = next_bit;
      map->mask = (1 << (next_bit + bits_needed)) - (1 << next_bit);
      next_bit += bits_needed;
      if (info->global)
	global_mask |= map->mask;
    }

  }
  feature_count = j;


  for (unsigned int table_index = 0; table_index < 2; table_index++) {
    hb_tag_t table_tag = table_tags[table_index];

    /* Collect lookup indices for features */

    unsigned int required_feature_index;
    if (hb_ot_layout_language_get_required_feature_index (c->face,
							  table_tag,
							  script_index[table_index],
							  language_index[table_index],
							  &required_feature_index))
      add_lookups (c, table_index, required_feature_index, 1);

    for (unsigned i = 0; i < feature_count; i++)
      add_lookups (c, table_index, feature_maps[i].index[table_index], feature_maps[i].mask);

    /* Sort lookups and merge duplicates */
    qsort (lookup_maps[table_index], lookup_count[table_index], sizeof (lookup_maps[table_index][0]), (hb_compare_func_t) lookup_map_t::cmp);
    if (lookup_count[table_index])
    {
      unsigned int j = 0;
      for (unsigned int i = 1; i < lookup_count[table_index]; i++)
	if (lookup_maps[table_index][i].index != lookup_maps[table_index][j].index)
	  lookup_maps[table_index][++j] = lookup_maps[table_index][i];
	else
	  lookup_maps[table_index][j].mask |= lookup_maps[table_index][i].mask;
      j++;
      lookup_count[table_index] = j;
    }
  }
}


HB_END_DECLS