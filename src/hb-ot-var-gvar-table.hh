/*
 * Copyright © 2019 Adobe Inc.
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
 * Adobe Author(s): Michiharu Ariza
 */

#ifndef HB_OT_VAR_GVAR_TABLE_HH
#define HB_OT_VAR_GVAR_TABLE_HH

#include "hb-open-type.hh"
#include "hb-ot-glyf-table.hh"
#include "hb-ot-var-fvar-table.hh"

/*
 * gvar -- Glyph Variation Table
 * https://docs.microsoft.com/en-us/typography/opentype/spec/gvar
 */
#define HB_OT_TAG_gvar HB_TAG('g','v','a','r')

namespace OT {

struct Tuple : UnsizedArrayOf<F2DOT14> {};

struct TuppleIndex : HBUINT16
{
  enum Flags {
    EmbeddedPeakTuple	= 0x8000u,
    IntermediateRegion	= 0x4000u,
    PrivatePointNumbers	= 0x2000u,
    TupleIndexMask	= 0x0FFFu
  };

  DEFINE_SIZE_STATIC (2);
};

struct TupleVarHeader
{
  unsigned int get_size (unsigned int axis_count) const
  {
    return min_size +
    	   (has_peak ()? get_peak_tuple ().get_size (axis_count): 0) +
    	   (has_intermediate ()? (get_start_tuple (axis_count).get_size (axis_count) +
				  get_end_tuple (axis_count).get_size (axis_count)): 0);
  }

  const TupleVarHeader &get_next (unsigned int axis_count) const
  { return StructAtOffset<TupleVarHeader> (this, get_size (axis_count)); }

  float calculate_scalar (const int *coords, unsigned int coord_count,
  			  const hb_array_t<const F2DOT14> shared_tuples) const
  {
    const F2DOT14 *peak_tuple;

    if (has_peak ())
      peak_tuple = &(get_peak_tuple ()[0]);
    else
    {
      unsigned int index = get_index ();
      if (unlikely (index * coord_count >= shared_tuples.length))
      	return 0.f;
      peak_tuple = &shared_tuples[coord_count * index];
    }

    const F2DOT14 *start_tuple = nullptr;
    const F2DOT14 *end_tuple = nullptr;
    if (has_intermediate ())
    {
      start_tuple = get_start_tuple (coord_count);
      end_tuple = get_end_tuple (coord_count);
    }

    float scalar = 1.f;
    for (unsigned int i = 0; i < coord_count; i++)
    {
      int v = coords[i];
      int peak = peak_tuple[i];
      if (!peak || v == peak) continue;

      if (has_intermediate ())
      {
      	int start = start_tuple[i];
      	int end = end_tuple[i];
	if (unlikely (start > peak || peak > end ||
		      (start < 0 && end > 0 && peak))) continue;
	if (v < start || v > end) return 0.f;
	if (v < peak)
	{ if (peak != start) scalar *= (float)(v - start) / (peak - start); }
	else
	{ if (peak != end) scalar *= (float)(end - v) / (end - peak); }
      }
      else if (!v || v < MIN (0, peak) || v > MAX (0, peak)) return 0.f;
      else
      	scalar *= (float)v / peak;
    }
    return scalar;
  }

  unsigned int get_data_size () const { return varDataSize; }

  bool has_peak () const { return (tupleIndex & TuppleIndex::EmbeddedPeakTuple) != 0; }
  bool has_intermediate () const { return (tupleIndex & TuppleIndex::IntermediateRegion) != 0; }
  bool has_private_points () const { return (tupleIndex & TuppleIndex::PrivatePointNumbers) != 0; }
  unsigned int get_index () const { return (tupleIndex & TuppleIndex::TupleIndexMask); }

  protected:
  const Tuple &get_peak_tuple () const
  { return StructAfter<Tuple> (tupleIndex); }
  const Tuple &get_start_tuple (unsigned int axis_count) const
  { return StructAfter<Tuple> (get_peak_tuple ()[has_peak ()? axis_count: 0]); }
  const Tuple &get_end_tuple (unsigned int axis_count) const
  { return StructAfter<Tuple> (get_peak_tuple ()[has_peak ()? (axis_count * 2): 0]); }

  HBUINT16		varDataSize;
  TuppleIndex		tupleIndex;
  /* UnsizedArrayOf<F2DOT14> peakTuple - optional */
  /* UnsizedArrayOf<F2DOT14> intermediateStartTuple - optional */
  /* UnsizedArrayOf<F2DOT14> intermediateEndTuple - optional */

  public:
  DEFINE_SIZE_MIN (4);
};

struct TupleVarCount : HBUINT16
{
  bool has_shared_point_numbers () const { return ((*this) & SharedPointNumbers) != 0; }
  unsigned int get_count () const { return (*this) & CountMask; }

  protected:
  enum Flags {
    SharedPointNumbers	= 0x8000u,
    CountMask		= 0x0FFFu
  };

  public:
  DEFINE_SIZE_STATIC (2);
};

struct GlyphVarData
{
  const TupleVarHeader &get_tuple_var_header (void) const
  { return StructAfter<TupleVarHeader>(data); }

  struct tuple_iterator_t
  {
    void init (const GlyphVarData *_var_data, unsigned int _length, unsigned int _axis_count)
    {
      var_data = _var_data;
      length = _length;
      index = 0;
      axis_count = _axis_count;
      current_tuple = &var_data->get_tuple_var_header ();
      data_offset = 0;
    }
  
    bool is_valid () const
    {
      return (index < var_data->tupleVarCount.get_count ()) &&
	     in_range (current_tuple) &&
	     current_tuple->get_size (axis_count);
    };

    bool move_to_next ()
    {
      data_offset += current_tuple->get_data_size ();
      current_tuple = &current_tuple->get_next (axis_count);
      index++;
      return is_valid ();
    }

    bool in_range (const void *p, unsigned int l) const
    { return (const char*)p >= (const char*)var_data && (const char*)p+l <= (const char*)var_data + length; }

    template <typename T> bool in_range (const T *p) const { return in_range (p, sizeof (*p)); }

    const HBUINT8 *get_serialized_data () const
    { return &(var_data+var_data->data) + data_offset; }

    private:
    const GlyphVarData		*var_data;
    unsigned int		length;
    unsigned int		index;
    unsigned int		axis_count;
    unsigned int		data_offset;

    public:
    const TupleVarHeader	*current_tuple;
  };

  static bool get_tuple_iterator (const GlyphVarData *var_data,
  				  unsigned int length,
  				  unsigned int axis_count,
  				  tuple_iterator_t *iterator /* OUT */)
  {
    iterator->init (var_data, length, axis_count);
    return iterator->is_valid ();
  }

  bool has_shared_point_numbers () const { return tupleVarCount.has_shared_point_numbers (); }

  protected:
  TupleVarCount		tupleVarCount;
  OffsetTo<HBUINT8>	data;
  /* TupleVarHeader tupleVarHeaders[] */
  
  public:
  DEFINE_SIZE_MIN (4);
};

struct gvar
{
  static constexpr hb_tag_t tableTag = HB_OT_TAG_gvar;

  bool sanitize_shallow (hb_sanitize_context_t *c) const
  {
    TRACE_SANITIZE (this);
    return_trace (c->check_struct (this) && (version.major == 1) &&
    		 (glyphCount == c->get_num_glyphs ()) &&
    		 c->check_array (&(this+sharedTuples), axisCount * sharedTupleCount) &&
    		 (is_long_offset ()?
		    c->check_array (get_long_offset_array (), glyphCount+1):
		    c->check_array (get_short_offset_array (), glyphCount+1)) &&
		 c->check_array (((const HBUINT8*)&(this+dataZ)) + get_offset (0),
				 get_offset (glyphCount) - get_offset (0)));
  }

  /* GlyphVarData not sanitized here; must be checked while accessing each glyph varation data */
  bool sanitize (hb_sanitize_context_t *c) const
  { return sanitize_shallow (c); }

  bool subset (hb_subset_context_t *c) const
  {
    TRACE_SUBSET (this);

    gvar *out = c->serializer->allocate_min<gvar> ();
    if (unlikely (!out)) return_trace (false);

    out->version.major.set (1);
    out->version.minor.set (0);
    out->axisCount.set (axisCount);
    out->sharedTupleCount.set (sharedTupleCount);

    unsigned int num_glyphs = c->plan->num_output_glyphs ();
    out->glyphCount.set (num_glyphs);

    unsigned int subset_data_size = 0;
    for (hb_codepoint_t gid = 0; gid < num_glyphs; gid++)
    {
      unsigned int old_gid;
      if (!c->plan->old_gid_for_new_gid (gid, &old_gid)) continue;
      subset_data_size += get_glyph_var_data_length (old_gid);
    }

    bool long_offset = subset_data_size & ~0xFFFFu;
    out->flags.set (long_offset? 1: 0);

    HBUINT8 *subset_offsets = c->serializer->allocate_size<HBUINT8> ((long_offset? 4: 2) * (num_glyphs+1));
    if (!subset_offsets) return_trace (false);

    char *subset_data = c->serializer->allocate_size<char>(subset_data_size);
    if (!subset_data) return_trace (false);
    out->dataZ.set (subset_data - (char *)out);

    unsigned int glyph_offset = 0;
    for (hb_codepoint_t gid = 0; gid < num_glyphs; gid++)
    {
      unsigned int old_gid;
      unsigned int length = c->plan->old_gid_for_new_gid (gid, &old_gid)? get_glyph_var_data_length (old_gid): 0;

      if (long_offset)
	((HBUINT32 *)subset_offsets)[gid].set (glyph_offset);
      else
      	((HBUINT16 *)subset_offsets)[gid].set (glyph_offset / 2);
      
      if (length > 0) memcpy (subset_data, get_glyph_var_data (old_gid), length);
      subset_data += length;
      glyph_offset += length;
    }
    if (long_offset)
      ((HBUINT32 *)subset_offsets)[num_glyphs].set (glyph_offset);
    else
      ((HBUINT16 *)subset_offsets)[num_glyphs].set (glyph_offset / 2);

    /* shared tuples */
    if (!sharedTupleCount || !sharedTuples)
      out->sharedTuples.set (0);
    else
    {
      unsigned int shared_tuple_size = F2DOT14::static_size * axisCount * sharedTupleCount;
      F2DOT14 *tuples = c->serializer->allocate_size<F2DOT14> (shared_tuple_size);
      if (!tuples) return_trace (false);
      out->sharedTuples.set ((char *)tuples - (char *)out);
      memcpy (tuples, &(this+sharedTuples), shared_tuple_size);
    }

    return_trace (true);
  }

  protected:
  const GlyphVarData *get_glyph_var_data (hb_codepoint_t glyph) const
  {
    unsigned int start_offset = get_offset (glyph);
    unsigned int end_offset = get_offset (glyph+1);

    if ((start_offset == end_offset) ||
	unlikely ((start_offset > get_offset (glyphCount)) ||
		  (start_offset + GlyphVarData::min_size > end_offset)))
      return &Null(GlyphVarData);
    return &(((unsigned char *)this+start_offset)+dataZ);
  }

  bool is_long_offset () const { return (flags & 1)!=0; }

  unsigned int get_offset (unsigned int i) const
  {
    if (is_long_offset ())
      return get_long_offset_array ()[i];
    else
      return get_short_offset_array ()[i] * 2;
  }

  unsigned int get_glyph_var_data_length (unsigned int glyph) const
  { return get_offset (glyph+1) - get_offset (glyph); }

  const HBUINT32 *get_long_offset_array () const { return (const HBUINT32 *)&offsetZ; }
  const HBUINT16 *get_short_offset_array () const { return (const HBUINT16 *)&offsetZ; }

  typedef glyf::accelerator_t::contour_point_t contour_point_t;
  typedef glyf::accelerator_t::phantom_point_index_t pp_t;
  typedef glyf::accelerator_t::range_checker_t range_checker_t;

  public:
  struct accelerator_t
  {
    void init (hb_face_t *face)
    {
      memset (this, 0, sizeof (accelerator_t));

      gvar_table = hb_sanitize_context_t ().reference_table<gvar> (face);
      glyf_accel.init (face);
      hb_blob_ptr_t<fvar> fvar_table = hb_sanitize_context_t ().reference_table<fvar> (face);
      unsigned int axis_count = fvar_table->get_axis_count ();
      fvar_table.destroy ();

      if (unlikely ((gvar_table->glyphCount != face->get_num_glyphs ()) ||
		    (gvar_table->axisCount != axis_count)))
      	fini ();

      unsigned int num_shared_coord = gvar_table->sharedTupleCount * gvar_table->axisCount;
      shared_tuples.resize (num_shared_coord);
      for (unsigned int i = 0; i < num_shared_coord; i++)
      	shared_tuples[i] = (&(gvar_table+gvar_table->sharedTuples))[i];
    }

    void fini ()
    {
      gvar_table.destroy ();
      glyf_accel.fini ();
    }

    bool apply_deltas_to_points (hb_codepoint_t glyph,
				 const int *coords, unsigned int coord_count,
				 const hb_array_t<contour_point_t> points,
				 const hb_array_t<unsigned int> end_points) const
    {
      if (unlikely (coord_count != gvar_table->axisCount)) return false;

      const GlyphVarData *var_data = gvar_table->get_glyph_var_data (glyph);
      GlyphVarData::tuple_iterator_t iterator;
      if (!GlyphVarData::get_tuple_iterator (var_data,
					     gvar_table->get_glyph_var_data_length (glyph),
					     gvar_table->axisCount,
					     &iterator))
	return false;

      do {
	float scalar = iterator.current_tuple->calculate_scalar (coords, coord_count, shared_tuples.as_array ());
	if (scalar == 0.f) continue;
	const HBUINT8 *p = iterator.get_serialized_data ();
	unsigned int length = iterator.current_tuple->get_data_size ();
	if (unlikely (!iterator.in_range (p, length))) return false;

	range_checker_t checker (p, 0, length);
	hb_vector_t <unsigned int>	shared_indices;
	if (var_data->has_shared_point_numbers () &&
	    !unpack_points (p, shared_indices, checker)) return false;
	hb_vector_t <unsigned int>	private_indices;
	if (iterator.current_tuple->has_private_points () &&
	    !unpack_points (p, private_indices, checker)) return false;
	const hb_array_t<unsigned int> &indices = shared_indices.length? shared_indices: private_indices;

      	bool apply_to_all = (indices.length == 0);
	unsigned int num_deltas = apply_to_all? points.length: indices.length;
	hb_vector_t <int>	x_deltas;
	x_deltas.resize (num_deltas);
	if (!unpack_deltas (p, x_deltas, checker)) return false;
	hb_vector_t <int>	y_deltas;
	y_deltas.resize (num_deltas);
	if (!unpack_deltas (p, y_deltas, checker)) return false;

	for (unsigned int i = 0; i < num_deltas; i++)
	{
	  unsigned int pt_index = apply_to_all? i: indices[i];
	  points[pt_index].x += x_deltas[i] * scalar;
	  points[pt_index].y += y_deltas[i] * scalar;
	}
	/* TODO: interpolate untouched points for glyph extents */
      } while (iterator.move_to_next ());

      return true;
    }

    /* Note: Recursively calls itself. Who's checking recursively nested composite glyph BTW? */
    bool get_var_metrics (hb_codepoint_t glyph,
			  const int *coords, unsigned int coord_count,
			  hb_vector_t<contour_point_t> &phantoms) const
    {
      hb_vector_t<contour_point_t>	points;
      hb_vector_t<unsigned int>		end_points;
      if (!glyf_accel.get_contour_points (glyph, true, points, end_points)) return false;
      if (!apply_deltas_to_points (glyph, coords, coord_count, points.as_array (), end_points.as_array ())) return false;

      for (unsigned int i = 0; i < pp_t::PHANTOM_COUNT; i++)
      	phantoms[i] = points[points.length - pp_t::PHANTOM_COUNT + i];

      glyf::CompositeGlyphHeader::Iterator composite;
      if (!glyf_accel.get_composite (glyph, &composite)) return true;	/* simple glyph */
      do
      {
	/* TODO: support component scale/transformation */
	if (((composite.current->flags & glyf::CompositeGlyphHeader::USE_MY_METRICS) != 0) &&
	    !get_var_metrics (composite.current->glyphIndex, coords, coord_count, phantoms))
	  return false;
      } while (composite.move_to_next());
      return true;
    }

    float get_advance_var (hb_codepoint_t glyph,
			   const int *coords, unsigned int coord_count,
			   bool vertical) const
    {
      float advance = 0.f;
      if (coord_count != gvar_table->axisCount) return advance;
    
      hb_vector_t<contour_point_t>	points;
      points.resize (pp_t::PHANTOM_COUNT);

      if (!get_var_metrics (glyph, coords, coord_count, points))
      	return advance;

      if (vertical)
      	return -(points[pp_t::PHANTOM_BOTTOM].y - points[pp_t::PHANTOM_TOP].y);	// is this sign correct?
      else
      	return points[pp_t::PHANTOM_RIGHT].x - points[pp_t::PHANTOM_LEFT].x;
    }

    protected:
    const GlyphVarData *get_glyph_var_data (hb_codepoint_t glyph) const
    { return gvar_table->get_glyph_var_data (glyph); }

    static bool unpack_points (const HBUINT8 *&p /* IN/OUT */,
			       hb_vector_t<unsigned int> &points /* OUT */,
			       const range_checker_t &check)
    {
      enum packed_point_flag_t
      {
	POINTS_ARE_WORDS = 0x80,
	POINT_RUN_COUNT_MASK = 0x7F
      };

      if (!check.in_range (p)) return false;
      uint16_t count = *p++;
      if ((count & POINTS_ARE_WORDS) != 0)
      {
      	if (!check.in_range (p)) return false;
      	count = ((count & POINT_RUN_COUNT_MASK) << 8) | *p++;
      }
      points.resize (count);

      uint16_t i = 0;
      while (i < count)
      {
	if (!check.in_range (p)) return false;
	uint16_t j;
	uint8_t control = *p++;
	uint16_t run_count = (control & POINT_RUN_COUNT_MASK) + 1;
	if ((control & POINTS_ARE_WORDS) != 0)
	{
	  for (j = 0; j < run_count && i < count; j++, i++)
	  {
	    if (!check.in_range ((const HBUINT16 *)p)) return false;
	    points[i] = *(const HBUINT16 *)p;
	    p += HBUINT16::static_size;
	  }
	}
	else
	{
	  for (j = 0; j < run_count && i < count; j++, i++)
	  {
	    if (!check.in_range (p)) return false;
	    points[i] = *p++;
	  }
	}
	if (j < run_count) return false;
      }
      return true;
    }

    static bool unpack_deltas (const HBUINT8 *&p /* IN/OUT */,
			       hb_vector_t<int> &deltas /* IN/OUT */,
			       const range_checker_t &check)
    {
      enum packed_delta_flag_t
      {
	DELTAS_ARE_ZERO	= 0x80,
	DELTAS_ARE_WORDS = 0x40,
	DELTA_RUN_COUNT_MASK = 0x3F
      };

      unsigned int i = 0;
      unsigned int count = deltas.length;
      while (i < count)
      {
	if (!check.in_range (p)) return false;
	uint16_t j;
	uint8_t control = *p++;
	uint16_t run_count = (control & DELTA_RUN_COUNT_MASK) + 1;
	if ((control & DELTAS_ARE_ZERO) != 0)
	{
	  for (j = 0; j < run_count && i < count; j++, i++)
	    deltas[i] = 0;
	}
	else if ((control & DELTAS_ARE_WORDS) != 0)
	{
	  for (j = 0; j < run_count && i < count; j++, i++)
	  {
	    if (!check.in_range ((const HBUINT16 *)p)) return false;
	    deltas[i] = *(const HBINT16 *)p;
	    p += HBUINT16::static_size;
	  }
	}
	else
	{
	  for (j = 0; j < run_count && i < count; j++, i++)
	  {
	    if (!check.in_range (p)) return false;
	    deltas[i] = *(const HBINT8 *)p++;
	  }
	}
	if (j < run_count) return false;
      }
      return true;
    }

    private:
    hb_blob_ptr_t<gvar>		gvar_table;
    hb_vector_t<F2DOT14>	shared_tuples;
    glyf::accelerator_t		glyf_accel;
  };

  protected:
  FixedVersion<>		version;		/* Version of gvar table. Set to 0x00010000u. */
  HBUINT16			axisCount;
  HBUINT16			sharedTupleCount;
  LOffsetTo<F2DOT14>		sharedTuples;		/* LOffsetTo<UnsizedArrayOf<Tupple>> */
  HBUINT16			glyphCount;
  HBUINT16			flags;
  LOffsetTo<GlyphVarData>	dataZ;			/* Array of GlyphVarData */
  UnsizedArrayOf<HBUINT8>	offsetZ;		/* Array of 16-bit or 32-bit (glyphCount+1) offsets */

  public:
  DEFINE_SIZE_MIN (20);
};

} /* namespace OT */

#endif /* HB_OT_VAR_GVAR_TABLE_HH */
