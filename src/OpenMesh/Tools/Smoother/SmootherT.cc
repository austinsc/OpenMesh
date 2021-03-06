/*===========================================================================*\
 *                                                                           *
 *                               OpenMesh                                    *
 *      Copyright (C) 2001-2012 by Computer Graphics Group, RWTH Aachen      *
 *                           www.openmesh.org                                *
 *                                                                           *
 *---------------------------------------------------------------------------* 
 *  This file is part of OpenMesh.                                           *
 *                                                                           *
 *  OpenMesh is free software: you can redistribute it and/or modify         * 
 *  it under the terms of the GNU Lesser General Public License as           *
 *  published by the Free Software Foundation, either version 3 of           *
 *  the License, or (at your option) any later version with the              *
 *  following exceptions:                                                    *
 *                                                                           *
 *  If other files instantiate templates or use macros                       *
 *  or inline functions from this file, or you compile this file and         *
 *  link it with other files to produce an executable, this file does        *
 *  not by itself cause the resulting executable to be covered by the        *
 *  GNU Lesser General Public License. This exception does not however       *
 *  invalidate any other reasons why the executable file might be            *
 *  covered by the GNU Lesser General Public License.                        *
 *                                                                           *
 *  OpenMesh is distributed in the hope that it will be useful,              *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU LesserGeneral Public          *
 *  License along with OpenMesh.  If not,                                    *
 *  see <http://www.gnu.org/licenses/>.                                      *
 *                                                                           *
\*===========================================================================*/ 

/*===========================================================================*\
 *                                                                           *             
 *   $Revision: 736 $                                                         *
 *   $Date: 2012-10-08 03:30:49 -0400 (Mon, 08 Oct 2012) $                   *
 *                                                                           *
\*===========================================================================*/

/** \file SmootherT.cc

 */

//=============================================================================
//
//  CLASS SmootherT - IMPLEMENTATION
//
//=============================================================================

#define OPENMESH_SMOOTHERT_C

//== INCLUDES =================================================================

#include <OpenMesh/Core/Utils/vector_cast.hh>
#include <OpenMesh/Tools/Smoother/SmootherT.hh>

//== NAMESPACES ===============================================================


namespace OpenMesh {
namespace Smoother {


//== IMPLEMENTATION ==========================================================


template <class Mesh>
SmootherT<Mesh>::
SmootherT(Mesh& _mesh)
  : mesh_(_mesh),
    skip_features_(false)
{
  // request properties
  mesh_.request_vertex_status();
  mesh_.request_face_normals();
  mesh_.request_vertex_normals();

  // custom properties
  mesh_.add_property(original_positions_);
  mesh_.add_property(original_normals_);
  mesh_.add_property(new_positions_);
  mesh_.add_property(is_active_);


  // default settings
  component_  = Tangential_and_Normal;
  continuity_ = C0;
  tolerance_  = -1.0;
}


//-----------------------------------------------------------------------------


template <class Mesh>
SmootherT<Mesh>::
~SmootherT()
{
  // free properties
  mesh_.release_vertex_status();
  mesh_.release_face_normals();
  mesh_.release_vertex_normals();

  // free custom properties
  mesh_.remove_property(original_positions_);
  mesh_.remove_property(original_normals_);
  mesh_.remove_property(new_positions_);
  mesh_.remove_property(is_active_);
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
initialize(Component _comp, Continuity _cont)
{
  typename Mesh::VertexIter  v_it, v_end(mesh_.vertices_end());


  // store smoothing settings
  component_  = _comp;
  continuity_ = _cont;


  // update normals
  mesh_.update_face_normals();
  mesh_.update_vertex_normals();


  // store original points & normals
  for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
  {
    mesh_.property(original_positions_, v_it) = mesh_.point(v_it);
    mesh_.property(original_normals_,   v_it) = mesh_.normal(v_it);
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
set_active_vertices()
{
  typename Mesh::VertexIter  v_it, v_end(mesh_.vertices_end());


  // is something selected?
  bool nothing_selected(true);
  for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
    if (mesh_.status(v_it).selected())
    { nothing_selected = false; break; }


  // tagg all active vertices
  bool active;
  for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
  {
    active = ((nothing_selected || mesh_.status(v_it).selected())
	          && !mesh_.is_boundary(v_it)
	          && !mesh_.status(v_it).locked());

    if ( skip_features_ ) {

      active = active && !mesh_.status(v_it).feature();

      typename Mesh::VertexOHalfedgeIter  voh_it(mesh_,v_it);
      for ( ; voh_it ; ++voh_it ) {

        // If the edge is a feature edge, skip the current vertex while smoothing
        if ( mesh_.status(mesh_.edge_handle(voh_it.handle())).feature() )
          active = false;

        typename Mesh::FaceHandle fh1 = mesh_.face_handle(voh_it.handle() );
        typename Mesh::FaceHandle fh2 = mesh_.face_handle(mesh_.opposite_halfedge_handle(voh_it.handle() ) );

        // If one of the faces is a feature, lock current vertex
        if ( fh1.is_valid() && mesh_.status( fh1 ).feature() )
          active = false;
        if ( fh2.is_valid() && mesh_.status( fh2 ).feature() )
          active = false;

      }
    }

    mesh_.property(is_active_, v_it) = active;
  }


  // C1: remove one ring of boundary vertices
  if (continuity_ == C1)
  {
    typename Mesh::VVIter     vv_it;

    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
      if (mesh_.is_boundary(v_it))
	for (vv_it=mesh_.vv_iter(v_it); vv_it; ++vv_it)
	  mesh_.property(is_active_, vv_it) = false;
  }


  // C2: remove two rings of boundary vertices
  if (continuity_ == C2)
  {
    typename Mesh::VVIter     vv_it;

    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
    {
      mesh_.status(v_it).set_tagged(false);
      mesh_.status(v_it).set_tagged2(false);
    }

    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
      if (mesh_.is_boundary(v_it))
	for (vv_it=mesh_.vv_iter(v_it); vv_it; ++vv_it)
	  mesh_.status(v_it).set_tagged(true);

    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
      if (mesh_.status(v_it).tagged())
	for (vv_it=mesh_.vv_iter(v_it); vv_it; ++vv_it)
	  mesh_.status(v_it).set_tagged2(true);

    for (v_it=mesh_.vertices_begin(); v_it!=v_end; ++v_it)
    {
      if (mesh_.status(v_it).tagged2())
	mesh_.property(is_active_, vv_it) = false;
      mesh_.status(v_it).set_tagged(false);
      mesh_.status(v_it).set_tagged2(false);
    }
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
set_relative_local_error(Scalar _err)
{
  if (!mesh_.vertices_empty())
  {
    typename Mesh::VertexIter  v_it(mesh_.vertices_begin()),
                               v_end(mesh_.vertices_end());


    // compute bounding box
    Point  bb_min, bb_max;
    bb_min = bb_max = mesh_.point(v_it);
    for (++v_it; v_it!=v_end; ++v_it)
    {
      bb_min.minimize(mesh_.point(v_it));
      bb_max.minimize(mesh_.point(v_it));
    }


    // abs. error = rel. error * bounding-diagonal
    set_absolute_error(_err * (bb_max-bb_min).norm());
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
set_absolute_local_error(Scalar _err)
{
  tolerance_ = _err;
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
disable_local_error_check()
{
  tolerance_ = -1.0;
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
smooth(unsigned int _n)
{
  // mark active vertices
  set_active_vertices();

  // smooth _n iterations
  while (_n--)
  {
    compute_new_positions();

    if (component_ == Tangential)
      project_to_tangent_plane();

    else if (tolerance_ >= 0.0)
	local_error_check();

    move_points();
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
compute_new_positions()
{
  switch (continuity_)
  {
    case C0:
      compute_new_positions_C0();
      break;

    case C1:
      compute_new_positions_C1();
      break;

    case C2:
      break;
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
project_to_tangent_plane()
{
  typename Mesh::VertexIter  v_it(mesh_.vertices_begin()),
                             v_end(mesh_.vertices_end());
  // Normal should be a vector type. In some environment a vector type
  // is different from point type, e.g. OpenSG!
  typename Mesh::Normal      translation, normal;


  for (; v_it != v_end; ++v_it)
  {
    if (is_active(v_it))
    {
      translation  = new_position(v_it)-orig_position(v_it);
      normal       = orig_normal(v_it);
      normal      *= dot(translation, normal);
      translation -= normal;
      translation += vector_cast<typename Mesh::Normal>(orig_position(v_it));
      set_new_position(v_it, translation);
    }
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
local_error_check()
{
  typename Mesh::VertexIter  v_it(mesh_.vertices_begin()),
                             v_end(mesh_.vertices_end());

  typename Mesh::Normal      translation;
  typename Mesh::Scalar      s;


  for (; v_it != v_end; ++v_it)
  {
    if (is_active(v_it))
    {
      translation  = new_position(v_it) - orig_position(v_it);

      s = fabs(dot(translation, orig_normal(v_it)));

      if (s > tolerance_)
      {
	translation *= (tolerance_ / s);
	translation += vector_cast<NormalType>(orig_position(v_it));
	set_new_position(v_it, translation);
      }
    }
  }
}


//-----------------------------------------------------------------------------


template <class Mesh>
void
SmootherT<Mesh>::
move_points()
{
  typename Mesh::VertexIter  v_it(mesh_.vertices_begin()),
                             v_end(mesh_.vertices_end());

  for (; v_it != v_end; ++v_it)
    if (is_active(v_it))
      mesh_.set_point(v_it, mesh_.property(new_positions_, v_it));
}


//=============================================================================
} // namespace Smoother
} // namespace OpenMesh
//=============================================================================
