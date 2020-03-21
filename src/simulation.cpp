#include "simulation.h"

/// \file
/// Implementation of the fluid simulation.

#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>

#include "pressure_solver.h"

namespace fluid {
	void simulation::resize(vec3s sz) {
		_grid = fluid_grid(sz);
		_space_hash.resize(sz);
	}

	void simulation::update(double dt) {
		std::cout << "update\n";
		while (true) {
			double ts = cfl_number * cfl();
			if (ts > dt) {
				time_step(dt);
				break;
			}
			time_step(ts);
			dt -= ts;
		}
	}

	void simulation::time_step(double dt) {
		std::cout << "  timestep " << dt << "\n";
		/*_add_spring_forces(dt, 1, 0);*/
		_advect_particles(dt);
		hash_particles();
		_transfer_to_grid();
		// add external forces
		for (std::size_t z = 0; z < grid().grid().get_size().z; ++z) {
			for (std::size_t y = 0; y < grid().grid().get_size().y; ++y) {
				for (std::size_t x = 0; x < grid().grid().get_size().x; ++x) {
					grid().grid()(x, y, z).velocities_posface += vec3d(0.0, -981.0, 0.0) * dt;
				}
			}
		}
		// solve and apply pressure
		{
			std::vector<vec3s> fluid_cells = _space_hash.get_sorted_occupied_cells();
			pressure_solver solver(*this, fluid_cells);
			auto [pressure, residual, iters] = solver.solve(dt);
			if (iters >= solver.max_iterations) {
				std::cerr << "    WARNING: maximum iterations exceeded\n";
			}
			solver.apply_pressure(dt, pressure);

			std::cerr << "    iterations = " << iters << "\n";
			std::cerr << "    residual = " << residual << "\n";
			std::cerr << "    max pressure = " << *std::max_element(pressure.begin(), pressure.end()) << "\n";
			double maxv = 0.0;
			for (const particle &p : _particles) {
				maxv = std::max(maxv, p.velocity.squared_length());
			}
			std::cerr << "    max velocity = " << std::sqrt(maxv) << "\n";
		}
		_transfer_from_grid();
	}

	void simulation::time_step() {
		time_step(std::min(cfl_number * cfl(), 0.033));
	}

	void simulation::seed_cell(vec3s cell, vec3d velocity, std::size_t density) {
		std::size_t
			index = grid().grid().index_to_raw(cell),
			num = _space_hash.get_objects_at(cell).size(),
			target = density * density * density;
		std::uniform_real_distribution<double> dist(0.0, cell_size);
		vec3d offset = grid_offset + vec3d(cell) * cell_size;
		for (; num < target; ++num) {
			particle p;
			p.grid_index = cell;
			p.position = offset + vec3d(dist(random), dist(random), dist(random));
			p.velocity = velocity;
			_space_hash.add_object_at_raw(index, p);
			_particles.emplace_back(p);
		}
	}

	void simulation::seed_box(vec3d start, vec3d size, std::size_t density) {
		vec3d end = start + size;
		vec3s
			start_cell = world_position_to_cell_index_unclamped(start),
			end_cell = world_position_to_cell_index_unclamped(end);
		seed_func(
			start_cell, end_cell - start_cell + vec3s(1, 1, 1),
			[&](vec3d pos) {
				return
					pos.x > start.x && pos.y > start.y && pos.z > start.z &&
					pos.x < end.x && pos.y < end.y && pos.z < end.z;
			},
			density
				);
	}

	void simulation::seed_sphere(vec3d center, double radius, std::size_t density) {
		vec3s
			start_cell = world_position_to_cell_index_unclamped(center - vec3d(radius, radius, radius)),
			end_cell = world_position_to_cell_index_unclamped(center + vec3d(radius, radius, radius));
		double sqr_radius = radius * radius;
		seed_func(
			start_cell, end_cell - start_cell + vec3s(1, 1, 1),
			[&](vec3d pos) {
				return (pos - center).squared_length() < sqr_radius;
			},
			density
				);
	}

	vec3s simulation::world_position_to_cell_index(vec3d pos) const {
		return vec_ops::apply<vec3s>(
			static_cast<const std::size_t & (*)(const std::size_t&, const std::size_t&)>(std::min),
			world_position_to_cell_index_unclamped(pos), grid().grid().get_size()
		);
	}

	vec3s simulation::world_position_to_cell_index_unclamped(vec3d pos) const {
		return vec_ops::apply<vec3s>(
			[](double v) {
				return static_cast<std::size_t>(std::max(v, 0.0));
			},
			(pos - grid_offset) / cell_size
				);
	}

	double simulation::cfl() const {
		double maxlen = 0.0;
		for (const particle &p : particles()) {
			maxlen = std::max(maxlen, p.velocity.squared_length());
		}
		return cell_size / std::sqrt(maxlen);
	}

	double simulation::_kernel(vec3d p) const {
		// TODO more kernels
		// this is the linear kernel
		p /= cell_size;
		return
			std::max(0.0, 1.0 - std::abs(p.x)) *
			std::max(0.0, 1.0 - std::abs(p.y)) *
			std::max(0.0, 1.0 - std::abs(p.z));
	}

	void simulation::_advect_particles(double dt) {
		vec3d
			skin_width = vec3d(boundary_skin_width, boundary_skin_width, boundary_skin_width),
			min_corner = grid_offset + skin_width,
			max_corner = cell_size * vec3d(grid().grid().get_size()) + grid_offset - skin_width;
		for (particle &p : _particles) {
			p.position += p.velocity * dt;
			// clamp the particle back into the grid
			vec_ops::apply_to(p.position, std::clamp<double>, p.position, min_corner, max_corner);
		}
	}

	void simulation::hash_particles() {
		_space_hash.clear();
		for (particle &p : _particles) {
			vec3d grid_pos = (p.position - grid_offset) / cell_size;
			p.grid_index = vec_ops::apply<vec3s>(
				[](double pos, std::size_t max) {
					return std::min(static_cast<std::size_t>(std::max(pos, 0.0)), max - 1);
				},
				grid_pos, grid().grid().get_size()
					);
			_space_hash.add_object_at(p.grid_index, p);
		}
	}

	void simulation::_transfer_to_grid_pic() {
		double half_cell = 0.5 * cell_size;
		double zpos = grid_offset.z + half_cell;
		for (std::size_t z = 0; z < grid().grid().get_size().z; ++z, zpos += cell_size) {
			double zpos_face = zpos + half_cell, ypos = grid_offset.y + half_cell;
			for (std::size_t y = 0; y < grid().grid().get_size().y; ++y, ypos += cell_size) {
				double ypos_face = ypos + half_cell, xpos = grid_offset.x + half_cell;
				for (std::size_t x = 0; x < grid().grid().get_size().x; ++x, xpos += cell_size) {
					fluid_grid::cell &cell = grid().grid()(x, y, z);
					if (cell.cell_type != fluid_grid::cell::type::solid) {
						double xpos_face = xpos + half_cell;

						vec3d sum_vel, sum_weight;
						vec3d
							xface(xpos_face, ypos, zpos),
							yface(xpos, ypos_face, zpos),
							zface(xpos, ypos, zpos_face);
						_space_hash.for_all_nearby_objects(
							vec3s(x, y, z), vec3s(1, 1, 1), vec3s(1, 1, 1),
							[&](particle &p) {
								vec3d weights(
									_kernel(p.position - xface),
									_kernel(p.position - yface),
									_kernel(p.position - zface)
								);
								sum_weight += weights;
								sum_vel += vec_ops::memberwise::mul(weights, p.velocity);
							}
						);

						cell.cell_type = fluid_grid::cell::type::air;
						if (!_space_hash.get_objects_at(vec3s(x, y, z)).empty()) {
							cell.cell_type = fluid_grid::cell::type::fluid;
						}
						vec_ops::apply_to(
							cell.velocities_posface,
							[](double vel, double weight) {
								return weight > 1e-6 ? vel / weight : 0.0; // TODO magic number
							},
							sum_vel, sum_weight
								);
					}
				}
			}
		}
	}

	void simulation::_transfer_to_grid_flip() {
		_transfer_from_grid_pic();
		_old_grid = _grid;
		_remove_boundary_velocities(_old_grid);
	}

	void simulation::_transfer_to_grid_apic() {
		double half_cell = 0.5 * cell_size;
		double zpos = grid_offset.z + half_cell;
		for (std::size_t z = 0; z < grid().grid().get_size().z; ++z, zpos += cell_size) {
			double zpos_face = zpos + half_cell, ypos = grid_offset.y + half_cell;
			for (std::size_t y = 0; y < grid().grid().get_size().y; ++y, ypos += cell_size) {
				double ypos_face = ypos + half_cell, xpos = grid_offset.x + half_cell;
				for (std::size_t x = 0; x < grid().grid().get_size().x; ++x, xpos += cell_size) {
					fluid_grid::cell &cell = grid().grid()(x, y, z);
					if (cell.cell_type != fluid_grid::cell::type::solid) {
						double xpos_face = xpos + half_cell;

						vec3d sum_vel, sum_weight;
						vec3d
							xface(xpos_face, ypos, zpos),
							yface(xpos, ypos_face, zpos),
							zface(xpos, ypos, zpos_face);
						_space_hash.for_all_nearby_objects(
							vec3s(x, y, z), vec3s(1, 1, 1), vec3s(1, 1, 1),
							[&](particle &p) {
								vec3d
									weights(
										_kernel(p.position - xface),
										_kernel(p.position - yface),
										_kernel(p.position - zface)
									),
									affine(
										vec_ops::dot(p.cx, xface - p.position),
										vec_ops::dot(p.cy, yface - p.position),
										vec_ops::dot(p.cz, zface - p.position)
									);
								sum_weight += weights;
								sum_vel += vec_ops::memberwise::mul(weights, p.velocity + affine);
							}
						);

						cell.cell_type = fluid_grid::cell::type::air;
						if (!_space_hash.get_objects_at(vec3s(x, y, z)).empty()) {
							cell.cell_type = fluid_grid::cell::type::fluid;
						}
						vec_ops::apply_to(
							cell.velocities_posface,
							[](double vel, double weight) {
								return weight > 1e-6 ? vel / weight : 0.0; // TODO magic number
							},
							sum_vel, sum_weight
								);
					}
				}
			}
		}
	}

	void simulation::_transfer_to_grid() {
		switch (simulation_method) {
		case method::pic:
			_transfer_to_grid_pic();
			break;
		case method::flip_blend:
			_transfer_to_grid_flip();
			break;
		case method::apic:
			_transfer_to_grid_apic();
			break;
		}
	}

	vec3d simulation::_get_negative_face_velocities(const fluid_grid &grid, vec3s id) {
		vec3d neg_vel;
		if (id.x > 0) {
			neg_vel.x = grid.grid()(id - vec3s::axis<0>()).velocities_posface.x;
		}
		if (id.y > 0) {
			neg_vel.y = grid.grid()(id - vec3s::axis<1>()).velocities_posface.y;
		}
		if (id.z > 0) {
			neg_vel.z = grid.grid()(id - vec3s::axis<2>()).velocities_posface.z;
		}
		return neg_vel;
	}

	void simulation::_remove_boundary_velocities(fluid_grid &g) {
		vec3s max_pos = g.grid().get_size() - vec3s(1, 1, 1);
		for (std::size_t z = 0; z < g.grid().get_size().z; ++z) {
			for (std::size_t y = 0; y < g.grid().get_size().y; ++y) {
				g.grid()(max_pos.x, y, z).velocities_posface.x = 0.0;
			}
			for (std::size_t x = 0; x < g.grid().get_size().x; ++x) {
				g.grid()(x, max_pos.y, z).velocities_posface.y = 0.0;
			}
		}
		for (std::size_t y = 0; y < g.grid().get_size().y; ++y) {
			for (std::size_t x = 0; x < g.grid().get_size().x; ++x) {
				g.grid()(x, y, max_pos.z).velocities_posface.z = 0.0;
			}
		}
	}

	void simulation::_transfer_from_grid_pic() {
		for (particle &p : _particles) {
			const fluid_grid::cell &cell = grid().grid()(p.grid_index);
			vec3d t = (p.position - grid_offset) / cell_size - vec3d(p.grid_index);
			vec_ops::apply_to(
				p.velocity,
				lerp<double>,
				_get_negative_face_velocities(_grid, p.grid_index), cell.velocities_posface, t
					);
		}
	}

	void simulation::_transfer_from_grid_flip(double blend) {
		for (particle &p : _particles) {
			vec3d t = (p.position - grid_offset) / cell_size - vec3d(p.grid_index);
			vec3d
				old_velocity = vec_ops::apply<vec3d>(
					lerp<double>,
					_get_negative_face_velocities(_old_grid, p.grid_index),
					_old_grid.grid()(p.grid_index).velocities_posface,
					t
					),
				new_velocity = vec_ops::apply<vec3d>(
					lerp<double>,
					_get_negative_face_velocities(_grid, p.grid_index),
					_grid.grid()(p.grid_index).velocities_posface,
					t
					);
			p.velocity = new_velocity + (p.velocity - old_velocity) * blend;
		}
	}

	/// Computes the gradient vector.
	vec3d _grad(
		// zyx order
		double v000, double v001, double v010, double v011,
		double v100, double v101, double v110, double v111,
		double fx, double fy, double fz
		) {
		// double xg =
		//	lerp(lerp(v001, v011, fy), lerp(v101, v111, fy), fz) -
		//	lerp(lerp(v000, v010, fy), lerp(v100, v110, fy), fz)
		// f000(-(1.0 - fy) * (1.0 - fz), -(1.0 - fx) * (1.0 - fz), -(1.0 - fx) * (1.0 - fy))
		// f001( (1.0 - fy) * (1.0 - fz), -       fx  * (1.0 - fz), -       fx  * (1.0 - fy))
		// f010(-       fy  * (1.0 - fz),  (1.0 - fx) * (1.0 - fz), -(1.0 - fx) *        fy )
		// f011(        fy  * (1.0 - fz),         fx  * (1.0 - fz), -       fx  *        fy )
		// f100(-(1.0 - fy) *        fz , -(1.0 - fx) *        fz ,  (1.0 - fx) * (1.0 - fy))
		// f101( (1.0 - fy) *        fz , -       fx  *        fz ,         fx  * (1.0 - fy))
		// f110(-       fy  *        fz ,  (1.0 - fx) *        fz ,  (1.0 - fx) *        fy )
		// f111(        fy  *        fz ,         fx  *        fz ,         fx  *        fy )
		vec3d
			f000(-(1.0 - fy) * (1.0 - fz), -(1.0 - fx) * (1.0 - fz), -(1.0 - fx) * (1.0 - fy)),
			f001((1.0 - fy) * (1.0 - fz), -fx * (1.0 - fz), -fx * (1.0 - fy)),
			f010(-fy * (1.0 - fz), (1.0 - fx) * (1.0 - fz), -(1.0 - fx) * fy),
			f011(fy * (1.0 - fz), fx * (1.0 - fz), -fx * fy),
			f100(-(1.0 - fy) * fz, -(1.0 - fx) * fz, (1.0 - fx) * (1.0 - fy)),
			f101((1.0 - fy) * fz, -fx * fz, fx * (1.0 - fy)),
			f110(-fy * fz, (1.0 - fx) * fz, (1.0 - fx) * fy),
			f111(fy * fz, fx * fz, fx * fy);
		return
			f000 * v000 + f001 * v001 + f010 * v010 + f011 * v011 +
			f100 * v100 + f101 * v101 + f110 * v110 + f111 * v111;
	}
	/// Clamps the input value, returning both the result and whether the value has been modified. Note that this
	/// function returns clamped for the max value, so that velocities at the max borders are ignored.
	std::pair<std::size_t, bool> _clamp(std::size_t val, std::size_t min, std::size_t max) {
		if (val < min) {
			return { min, true };
		}
		if (val >= max) {
			return { max, true };
		}
		return { val, false };
	}
	void simulation::_transfer_from_grid_apic() {
		for (particle &p : _particles) {
			const fluid_grid::cell &cell = grid().grid()(p.grid_index);
			vec3d t = (p.position - grid_offset) / cell_size - vec3d(p.grid_index);

			// compute velocity
			vec_ops::apply_to(
				p.velocity,
				lerp<double>,
				_get_negative_face_velocities(_grid, p.grid_index), cell.velocities_posface, t
					);

			// compute c vectors
			//         z  y  x
			vec3d vels[3][3][3];
			for (std::size_t dz = 0; dz < 3; ++dz) {
				auto [cz, zclamp] = _clamp(p.grid_index.z + dz, 1, grid().grid().get_size().z);
				--cz;
				for (std::size_t dy = 0; dy < 3; ++dy) {
					auto [cy, yclamp] = _clamp(p.grid_index.y + dy, 1, grid().grid().get_size().y);
					--cy;
					for (std::size_t dx = 0; dx < 3; ++dx) {
						auto [cx, xclamp] = _clamp(p.grid_index.x + dx, 1, grid().grid().get_size().x);
						--cx;

						vec3d vel = grid().grid()(cx, cy, cz).velocities_posface;
						if (xclamp) {
							vel.x = 0.0;
						}
						if (yclamp) {
							vel.y = 0.0;
						}
						if (zclamp) {
							vel.z = 0.0;
						}
						vels[dz][dy][dx] = vel;
					}
				}
			}
			std::size_t dx = 1, dy = 1, dz = 1;
			vec3d tmid = t - vec3d(0.5, 0.5, 0.5);
			if (tmid.x < 0.0) {
				dx = 0;
				tmid.x += 1.0;
			}
			if (tmid.y < 0.0) {
				dy = 0;
				tmid.y += 1.0;
			}
			if (tmid.z < 0.0) {
				dz = 0;
				tmid.z += 1.0;
			}
			// v000(vels[dz    ][dy    ][0].x, vels[dz    ][0][dx    ].y, vels[0][dy    ][dx    ].z)
			// v001(vels[dz    ][dy    ][1].x, vels[dz    ][0][dx + 1].y, vels[0][dy    ][dx + 1].z)
			// v010(vels[dz    ][dy + 1][0].x, vels[dz    ][1][dx    ].y, vels[0][dy + 1][dx    ].z)
			// v011(vels[dz    ][dy + 1][1].x, vels[dz    ][1][dx + 1].y, vels[0][dy + 1][dx + 1].z)
			// v100(vels[dz + 1][dy    ][0].x, vels[dz + 1][0][dx    ].y, vels[1][dy    ][dx    ].z)
			// v101(vels[dz + 1][dy    ][1].x, vels[dz + 1][0][dx + 1].y, vels[1][dy    ][dx + 1].z)
			// v110(vels[dz + 1][dy + 1][0].x, vels[dz + 1][1][dx    ].y, vels[1][dy + 1][dx    ].z)
			// v111(vels[dz + 1][dy + 1][1].x, vels[dz + 1][1][dx + 1].y, vels[1][dy + 1][dx + 1].z)
			vec3d
				v000(vels[dz][dy][0].x, vels[dz][0][dx].y, vels[0][dy][dx].z),
				v001(vels[dz][dy][1].x, vels[dz][0][dx + 1].y, vels[0][dy][dx + 1].z),
				v010(vels[dz][dy + 1][0].x, vels[dz][1][dx].y, vels[0][dy + 1][dx].z),
				v011(vels[dz][dy + 1][1].x, vels[dz][1][dx + 1].y, vels[0][dy + 1][dx + 1].z),
				v100(vels[dz + 1][dy][0].x, vels[dz + 1][0][dx].y, vels[1][dy][dx].z),
				v101(vels[dz + 1][dy][1].x, vels[dz + 1][0][dx + 1].y, vels[1][dy][dx + 1].z),
				v110(vels[dz + 1][dy + 1][0].x, vels[dz + 1][1][dx].y, vels[1][dy + 1][dx].z),
				v111(vels[dz + 1][dy + 1][1].x, vels[dz + 1][1][dx + 1].y, vels[1][dy + 1][dx + 1].z);
			p.cx = _grad(
				v000.x, v001.x, v010.x, v011.x, v100.x, v101.x, v110.x, v111.x, t.x, tmid.y, tmid.z
				) / cell_size;
			p.cy = _grad(
				v000.y, v001.y, v010.y, v011.y, v100.y, v101.y, v110.y, v111.y, tmid.x, t.y, tmid.z
				) / cell_size;
			p.cz = _grad(
				v000.z, v001.z, v010.z, v011.z, v100.z, v101.z, v110.z, v111.z, tmid.x, tmid.y, t.z
				) / cell_size;
		}
	}

	void simulation::_transfer_from_grid() {
		switch (simulation_method) {
		case method::pic:
			_transfer_from_grid_pic();
			break;
		case method::flip_blend:
			_transfer_from_grid_flip(blending_factor);
			break;
		case method::apic:
			_transfer_from_grid_apic();
			break;
		}
	}

	void simulation::_add_spring_forces(double dt, std::size_t step, std::size_t substep) {
		double re = cell_size / std::sqrt(2.0); // some kind of radius
		// in apic2d, this distribution is actually (0, 1), not sure why
		std::uniform_real_distribution<double> dist(-1.0, 1.0);
		double min_dist = 0.1 * re;
		std::vector<vec3d> new_positions(_particles.size());
#pragma omp parallel for
		for (std::size_t i = substep; i < _particles.size(); i += step) {
			const particle &p = _particles[i];
			vec3d spring;
			_space_hash.for_all_nearby_objects(
				p.grid_index, vec3s(1, 1, 1), vec3s(1, 1, 1),
				[&](const particle &other) {
					if (&other != &p) {
						vec3d offset = p.position - other.position;
						double sqr_dist = offset.squared_length();
						if (sqr_dist < min_dist * min_dist) {
							// the two particles are not too far away, so just add a random force to avoid
							// floating point errors
							// apic2d multiplies this value by 0.01 * dt here
							spring += re * vec3d(dist(random), dist(random), dist(random));
						} else {
							double kernel_lower = 1.0 - sqr_dist / (cell_size * cell_size);
							double kernel = 0.0;
							if (kernel_lower > 0.0) {
								kernel = kernel_lower * kernel_lower * kernel_lower;
							}
							spring += (kernel * re / std::sqrt(sqr_dist)) * offset;
						}
					}
				}
			);
			new_positions[i] = p.position + spring * dt;
		}
		for (std::size_t i = substep; i < _particles.size(); i += step) {
			_particles[i].position = new_positions[i];
		}
	}
}
