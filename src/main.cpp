#include "editor.h"
#include "win_console_utils.h"
#include <iostream>
#include <Windows.h> // Required for INPUT_RECORD, GetStdHandle, GetNumberOfConsoleInputEvents, ReadConsoleInput

// The old processKeyPress function is removed entirely as its logic is now within Editor::processInput

int main(int argc, char* argv[]) {
    Editor editor; // Editor constructor handles console mode setup

    editor.setupDefaultKeybindings(); // Populate commandRegistry and customKeybindings
    editor.loadKeybindings("keybindings.json"); // Load user overrides

    std::cerr << "--- Loaded Keybindings ---" << std::endl;
    for (const auto& pair : editor.customKeybindings) {
        std::cerr << "Key: " << pair.first.toString() << " -> Command: " << pair.second << std::endl;
    }
    std::cerr << "------------------------" << std::endl;

    if (argc >= 2) {
        editor.openFile(argv[1]);
    }
    
    bool running = true;
    while (running) {
        editor.refreshScreen();

        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        DWORD numEvents = 0;
        
        if (GetNumberOfConsoleInputEvents(hInput, &numEvents) && numEvents > 0) {
            INPUT_RECORD irBuffer[128];
            DWORD eventsRead;
            if (ReadConsoleInput(hInput, irBuffer, 128, &eventsRead)) {
                for (DWORD i = 0; i < eventsRead; ++i) {
                    if (irBuffer[i].EventType == KEY_EVENT && irBuffer[i].Event.KeyEvent.bKeyDown) {
                        editor.processInput(
                            irBuffer[i].Event.KeyEvent.wVirtualKeyCode,
                            irBuffer[i].Event.KeyEvent.uChar.AsciiChar,
                            irBuffer[i].Event.KeyEvent.dwControlKeyState
                        );
                        if (editor.should_exit) {
                            running = false;
                            break; 
                        }
                    }
                }
            }
        } else {
            Sleep(10);
        }

        if (editor.should_exit) {
            running = false;
        }
    }

    editor.clearScreen();
    std::cout << "Exiting editor." << std::endl;

    return 0;
}