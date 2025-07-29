#pragma once

#define NOMINMAX

#include <string>
#include <vector>
#include <Windows.h>	
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <functional>

extern "C" {
#include "lua_src/lua.h"  
#include "lua_src/lualib.h"
#include "lua_src/lauxlib.h"
}

enum KeyCode {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DELETE_KEY,
};

void enableRawMode();
void disableRawMode();
void clearScreen();
void setCursorPosition(int x, int y);
void writeStringAt(int x, int y, const std::string& s);
int readKey();

struct Editor;