#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "output.h"


int main(int argc, char** argv) {

	Output::SetLogLevel(LogLevel::Error);

	return doctest::Context(argc, argv).run();
}
