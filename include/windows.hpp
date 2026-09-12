#pragma once
#include "gesture.hpp"
#include <string>
namespace tpc {
int devices();
int live(const std::wstring& target, bool driver, bool stream = false);
int rotation_test(const std::wstring& target);
}
