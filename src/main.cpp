#include "HelloTriangleApplication.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

auto main() -> int
{
	try {
		HelloTriangle::Application app{};
		app.run();
	} catch (std::exception const& e) {
		std::cerr << e.what() << std::endl;
		std::exit(EXIT_FAILURE);
	}
	std::exit(EXIT_SUCCESS);
}
