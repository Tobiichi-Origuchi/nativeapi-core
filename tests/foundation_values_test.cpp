#include "foundation_value_checks.h"
#include <iostream>
int main() {
  nativeapi::tests::FoundationChecks checks;
  checks.failure=[](const char* text){std::cerr << "FAIL " << text << '\n';};
  checks.Run();
  std::cout << "Foundation values: " << checks.passed << " passed, " << checks.failed << " failed\n";
  return checks.failed ? 1 : 0;
}
