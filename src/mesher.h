#pragma once

/// \file
/// Generation of triangular mesh from fluid.

#include <vector>

#include "math/vec.h"
#include "data_structures/particle.h"
#include "data_structures/grid.h"
#include "data_structures/space_hashing.h"
#include "data_structures/mesh.h"

namespace fluid {
	/// Used to generate triangular surface meshes from fluid particles.
	class mesher {
	public:
		/// The mesh type.
		using mesh_t = mesh<double, std::uint8_t, double, std::size_t>;

		/// Resizes the sampling grid. This is counted in the number of cells, so \ref _surface_function will have
		/// one more coordinate on each dimension.
		void resize(vec3s);

		/// Generates a mesh for the given set of particles.
		[[nodiscard]] mesh_t generate_mesh(const std::vector<vec3d>&, double r);

		vec3d grid_offset; ///< The offset of the sampling grid in world space.
		double
			cell_size = 0.0, ///< The size of a cell.
			particle_extent = 0.5; ///< The extent of each particle.
		std::size_t cell_radius = 2; ///< The radius of cells to look at when sampling the fluid to a grid.
	private:
		grid3<double> _surface_function; ///< Sampled values of the implicit surface function.
		space_hashing<const vec3d> _hash; ///< Space hashing.

		/// The kernel function for weighing particles.
		[[nodiscard]] double _kernel(double sqr_dist) const;
		/// Hashes the particles and samples the particles into the grid.
		void _sample_surface_function(const std::vector<vec3d>&, double);
		/// Generate the mesh using the marching cubes algorithm.
		[[nodiscard]] mesh_t _marching_cubes() const;
	};
}