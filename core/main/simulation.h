#pragma once
#include "framework/reflected.h"
#include "framework/reflection_macros.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "simulation.gen.h"
#endif

namespace feather {

// class that runs the engine main loop.
// It manages the main logic for execution of entities, components and such.
class Simulation : public Reflected {
	FCLASS();

protected:
	Simulation() = default;

public:
	[[method]] virtual void init() {};
	virtual void pre_update(double delta) {};
	[[method]] virtual void fixed_update(double delta) {};
	[[method]] virtual void update(double delta) {};

	~Simulation() override = default;
};

} //namespace feather