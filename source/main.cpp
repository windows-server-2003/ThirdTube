#include "headers.hpp"

int main()
{
	Menu_init();

	// Main loop
	while (aptMainLoop())
	{
		if (!Menu_main()) break;
	}

	Menu_exit();
	return 0;
}
