#include "win_console_utils.h"
#include <iostream>

static DWORD g_originalInputMode = 0;
static DWORD g_originalOutputMode = 0;
static WORD g_originalAttributes;
static HANDLE g_hStdin = INVALID_HANDLE_VALUE;
static HANDLE g_hStdout = INVALID_HANDLE_VALUE;

void enableRawMode()
{
	g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
	g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	if (g_hStdin == INVALID_HANDLE_VALUE || g_hStdout == INVALID_HANDLE_VALUE)
	{
		std::cerr << "Error getting standard handles." << std::endl;
		exit(1);
	}

	if (!GetConsoleMode(g_hStdin, &g_originalInputMode))
	{
		std::cerr << "Error getting console input mode." << std::endl;
		exit(1);
	}

	if (!GetConsoleMode(g_hStdout, &g_originalOutputMode))
	{
		std::cerr << "Error getting console ouput mode." << std::endl;
		exit(1);
	}

	DWORD newMode = 0;
	newMode |= ENABLE_WINDOW_INPUT;
	newMode |= ENABLE_MOUSE_INPUT;

	if (!SetConsoleMode(g_hStdin, newMode))
	{
		std::cerr << "Error setting console input mode." << std::endl;
		exit(1);
	}

	DWORD newOutputMode = g_originalOutputMode;
	newOutputMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(g_hStdout, newOutputMode))
	{
		if (GetLastError() == ERROR_INVALID_PARAMETER)
		{
			std::cerr << "ENABLE_VIRTUAL_TERMINAL_PROCESSING not supported for output. Falling back." << std::endl;
		}
		else
		{
			std::cerr << "Error setting console output mode." << std::endl;
			exit(1);
		}
	}

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(g_hStdout, &csbi)) {
		std::cerr << "Error getting console screen buffer info." << std::endl;
		exit(1);
	}
	g_originalAttributes = csbi.wAttributes;	
}

void disableRawMode()
{
	SetConsoleMode(g_hStdin, g_originalInputMode);
	SetConsoleMode(g_hStdout, g_originalOutputMode);
	SetConsoleTextAttribute(g_hStdout, g_originalAttributes);
}

void setTextColor(WORD attributes) {
	SetConsoleTextAttribute(g_hStdout, attributes);
}

void resetTextColor() {
	SetConsoleTextAttribute(g_hStdout, g_originalAttributes);
}

void clearScreen() 
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD count;
	COORD homeCoords = { 0, 0 };

	if (!GetConsoleScreenBufferInfo(g_hStdout, &csbi)) return;

	FillConsoleOutputCharacter(g_hStdout, ' ', csbi.dwSize.X * csbi.dwSize.Y, homeCoords, &count);
	FillConsoleOutputAttribute(g_hStdout, csbi.wAttributes, csbi.dwSize.X * csbi.dwSize.Y, homeCoords, &count);
	SetConsoleCursorPosition(g_hStdout, homeCoords);
}

void setCursorPosition(int x, int y)
{
	COORD coord = { (SHORT)x, (SHORT)y };
	SetConsoleCursorPosition(g_hStdout, coord);
}

void writeStringAt(int x, int y, const std::string& s) 
{
	setCursorPosition(x, y);
	DWORD charsWritten;
	// Use WriteConsoleA for ANSI (char) strings
	WriteConsoleA(g_hStdout, s.c_str(), s.length(), &charsWritten, NULL);
}

int readKey() 
{
	INPUT_RECORD irInBuf[128];
	DWORD cNumRead;

	while (true) 
	{
		if (!ReadConsoleInput(g_hStdin, irInBuf, 128, &cNumRead))
		{
			std::cerr << "Error reading console input." << std::endl;
			exit(1);
		}

		for (DWORD i = 0; i < cNumRead; i++) 
		{
			if (irInBuf[i].EventType == KEY_EVENT && irInBuf[i].Event.KeyEvent.bKeyDown) 
			{
				// Check if it's a standard character
				if (irInBuf[i].Event.KeyEvent.uChar.AsciiChar != 0) 
				{
					return irInBuf[i].Event.KeyEvent.uChar.AsciiChar;
				}
				else 
				{
					// It's a special key
					switch (irInBuf[i].Event.KeyEvent.wVirtualKeyCode) 
					{
						case VK_LEFT: return ARROW_LEFT;
						case VK_RIGHT: return ARROW_RIGHT;
						case VK_UP: return ARROW_UP;
						case VK_DOWN: return ARROW_DOWN;
						case VK_PRIOR: return PAGE_UP;
						case VK_NEXT: return PAGE_DOWN;
						case VK_HOME: return HOME_KEY;
						case VK_END: return END_KEY;
						case VK_DELETE: return DELETE_KEY;
						case VK_BACK: return 8; // ASCII for backspace
						case VK_RETURN: return 13; // ASCII for enter
						case VK_ESCAPE: return 27; // ASCII for escape
						case VK_TAB: return 9;
						default:
							// For unhandled special keys, you might return 0 or another indicator
							// Or add more cases as your editor needs them.
							break;
					}
				}
			}
		}
	}
}