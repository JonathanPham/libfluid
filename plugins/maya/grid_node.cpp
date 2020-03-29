#include "grid_node.h"

/// \file
/// Implementation of the grid node.

#include <maya/MFnUnitAttribute.h>
#include <maya/MTime.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnPointArrayData.h>

#include <fluid/simulation.h>

#include "misc.h"

namespace fluid::maya {
	MObject
		grid_node::attr_time,
		grid_node::attr_cell_size,
		grid_node::attr_grid_size,
		grid_node::attr_grid_offset,
		grid_node::attr_gravity,
		grid_node::attr_transfer_method,
		grid_node::attr_output_particle_positions;

	void *grid_node::creator() {
		return new grid_node();
	}

	MStatus grid_node::initialize() {
		MStatus stat;

		MFnUnitAttribute time;
		attr_time = time.create("time", "t", MTime(0.0), &stat);
		FLUID_MAYA_CHECK(stat, "parameter creation");

		MFnNumericAttribute cell_size;
		attr_cell_size = cell_size.create("cellSize", "cell", MFnNumericData::kDouble, 1.0, &stat);
		FLUID_MAYA_CHECK(stat, "parameter creation");

		MFnNumericAttribute grid_size;
		attr_grid_size = grid_size.create("gridSize", "grid", MFnNumericData::k3Int, 0.0, &stat);
		FLUID_MAYA_CHECK(stat, "parameter creation");

		MFnNumericAttribute grid_offset;
		attr_grid_offset = grid_offset.create("gridOffset", "goff", MFnNumericData::k3Double, 0.0, &stat);
		FLUID_MAYA_CHECK(stat, "parameter creation");

		MFnNumericAttribute gravity;
		attr_gravity = gravity.create("gravity", "g", MFnNumericData::k3Double, 0.0, &stat);
		FLUID_MAYA_CHECK(stat, "parameter creation");

		MFnEnumAttribute transfer_method;
		attr_transfer_method = transfer_method.create(
			"transferMethod", "mthd", static_cast<short>(simulation::method::apic), &stat
		);
		FLUID_MAYA_CHECK(stat, "parameter creation");
		FLUID_MAYA_CHECK_RETURN(
			transfer_method.addField("PIC", static_cast<short>(simulation::method::pic)), "parameter creation"
		);
		FLUID_MAYA_CHECK_RETURN(
			transfer_method.addField("FLIPBlend", static_cast<short>(simulation::method::flip_blend)),
			"parameter creation"
		);
		FLUID_MAYA_CHECK_RETURN(
			transfer_method.addField("APIC", static_cast<short>(simulation::method::apic)), "parameter creation"
		);

		MFnTypedAttribute output_particle_positions;
		attr_output_particle_positions = output_particle_positions.create(
			"outputParticlePositions", "out", MFnData::kPointArray, MObject::kNullObj, &stat
		);
		FLUID_MAYA_CHECK(stat, "parameter creation");
		FLUID_MAYA_CHECK_RETURN(output_particle_positions.setWritable(false), "parameter creation");
		FLUID_MAYA_CHECK_RETURN(output_particle_positions.setStorable(false), "parameter creation");

		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_time), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_cell_size), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_grid_size), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_grid_offset), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_gravity), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_transfer_method), "parameter registration");
		FLUID_MAYA_CHECK_RETURN(addAttribute(attr_output_particle_positions), "parameter registration");

		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_time, attr_output_particle_positions), "parameter registration"
		);
		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_cell_size, attr_output_particle_positions), "parameter registration"
		);
		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_grid_size, attr_output_particle_positions), "parameter registration"
		);
		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_grid_offset, attr_output_particle_positions), "parameter registration"
		);
		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_gravity, attr_output_particle_positions), "parameter registration"
		);
		FLUID_MAYA_CHECK_RETURN(
			attributeAffects(attr_transfer_method, attr_output_particle_positions), "parameter registration"
		);

		return MStatus::kSuccess;
	}

	MStatus grid_node::compute(const MPlug &plug, MDataBlock &data_block) {
		if (plug != attr_output_particle_positions) {
			return MStatus::kUnknownParameter;
		}

		MStatus stat;

		MDataHandle time_data = data_block.inputValue(attr_time, &stat);
		FLUID_MAYA_CHECK(stat, "retrieve attribute");
		MDataHandle output_particle_positions_data = data_block.outputValue(attr_output_particle_positions, &stat);
		FLUID_MAYA_CHECK(stat, "retrieve attribute");
		std::size_t frame = static_cast<std::size_t>(time_data.asTime().as(MTime::uiUnit()));
		if (frame >= _particle_cache.size()) { // keep on simulating
			MDataHandle cell_size_data = data_block.inputValue(attr_cell_size, &stat);
			FLUID_MAYA_CHECK(stat, "retrieve attribute");
			MDataHandle grid_size_data = data_block.inputValue(attr_grid_size, &stat);
			FLUID_MAYA_CHECK(stat, "retrieve attribute");
			MDataHandle grid_offset_data = data_block.inputValue(attr_grid_offset, &stat);
			FLUID_MAYA_CHECK(stat, "retrieve attribute");
			MDataHandle gravity_data = data_block.inputValue(attr_gravity, &stat);
			FLUID_MAYA_CHECK(stat, "retrieve attribute");
			MDataHandle transfer_method_data = data_block.inputValue(attr_transfer_method, &stat);
			FLUID_MAYA_CHECK(stat, "retrieve attribute");

			simulation sim;
			// cell size
			sim.cell_size = cell_size_data.asDouble();
			// grid size
			const int3 &grid_size = grid_size_data.asInt3();
			for (std::size_t i = 0; i < 3; ++i) {
				if (grid_size[i] < 0) {
					return MStatus::kInvalidParameter;
				}
			}
			sim.resize(vec3s(vec3i(grid_size[0], grid_size[1], grid_size[2])));
			// grid offset
			const double3 &grid_offset = grid_offset_data.asDouble3();
			sim.grid_offset = vec3d(grid_offset[0], grid_offset[1], grid_offset[2]);
			// gravity
			const double3 &gravity = gravity_data.asDouble3();
			sim.gravity = vec3d(gravity[0], gravity[1], gravity[2]);
			// transfer method
			sim.simulation_method = static_cast<simulation::method>(transfer_method_data.asShort());

			if (_particle_cache.size() > 0) {
				sim.particles() = std::move(_last_frame_particles);
			} else {
				// TODO seed based on sources
				sim.seed_sphere(vec3d(25.0, 25.0, 25.0), 15.0);
			}
			sim.hash_particles();

			double frame_time = MTime(1.0, MTime::uiUnit()).as(MTime::kSeconds);
			do {
				sim.update(frame_time);
				MPointArray &array = _particle_cache.emplace_back(static_cast<unsigned int>(sim.particles().size()));
				std::size_t i = 0;
				for (const particle &p : sim.particles()) {
					array.set(static_cast<unsigned int>(i), p.position.x, p.position.y, p.position.z);
					++i;
				}
			} while (frame >= _particle_cache.size());

			_last_frame_particles = std::move(sim.particles());
		}

		MFnPointArrayData points_array;
		MObject points_array_data = points_array.create(_particle_cache[frame], &stat);
		FLUID_MAYA_CHECK(stat, "finalize compute");
		FLUID_MAYA_CHECK_RETURN(output_particle_positions_data.set(points_array_data), "finalize compute");
		output_particle_positions_data.setClean();
		return MStatus::kSuccess;
	}

	MStatus grid_node::setDependentsDirty(const MPlug &in_plug, MPlugArray &affected_plugs) {
		if (in_plug != attr_time && in_plug != attr_output_particle_positions) {
			_particle_cache.clear();
			_last_frame_particles.clear();
		}
		return MPxNode::setDependentsDirty(in_plug, affected_plugs);
	}
}