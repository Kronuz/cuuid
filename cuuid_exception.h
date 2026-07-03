#pragma once

#include <stdexcept>
#include <string>

namespace cuuid {
	struct SerialisationError : std::runtime_error { using std::runtime_error::runtime_error; };
	struct InvalidArgument : std::invalid_argument { using std::invalid_argument::invalid_argument; };
}

#ifndef THROW
#define THROW(exc_type, ...) throw ::cuuid::exc_type(__VA_ARGS__)
#endif
