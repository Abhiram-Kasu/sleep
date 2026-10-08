#include "runtime.hpp"
#include "task.hpp"
#include <chrono>
#include <print>
#include <thread>
using namespace std::chrono_literals;

int main() { std::this_thread::sleep_for(1500ms); }
