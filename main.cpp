#include "editor.h"
#include "win_console_utils.h"
#include <iostream>

void processKeyPress(Editor& editor, int c) {
    if (editor.mode == EDIT_MODE) {
        switch (c) {
        case CMD_QUIT:
            break;
        case CMD_SAVE:
            editor.saveFile();
            break;
        case CMD_OPEN: {
            editor.statusMessage = "Ctrl-O pressed. Opening 'demo.txt'.";
            editor.statusMessageTime = GetTickCount64();
            if (editor.openFile("demo.txt")) {
            }
            break;
        }
        case 8:
            editor.deleteChar();
            break;
        case 13:
            editor.insertNewline();
            break;
        case 9: // Tab key
            editor.insertChar('\t');
            break;
        case DELETE_KEY:
            editor.deleteForwardChar();
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case PAGE_UP:
        case PAGE_DOWN:
        case HOME_KEY:
        case END_KEY:
            editor.moveCursor(c);
            break;
        case 27: // ESC
            editor.statusMessage = "";
            editor.statusMessageTime = 0;
            break;
        case 'n': // 'n' for next search result (should be Ctrl+N later)
            editor.findNext();
            break;
        case 'p': // 'p' for previous search result (should be Ctrl+P later)
            editor.findPrevious();
            break;
        default:
            if (c >= 32 && c <= 126) {
                editor.insertChar(c);
            }
            break;
        }
    } else if (editor.mode == FILE_EXPLORER_MODE) {
        switch (c) {
            case ARROW_UP:
            case ARROW_DOWN:
            case PAGE_UP:
            case PAGE_DOWN:
            case HOME_KEY:
            case END_KEY:
                editor.moveFileExplorerSelection(c);
                break;
            case 13: // Enter key
                editor.handleFileExplorerEnter();
                break;
            case 27: // ESC key to exit explorer mode
                editor.toggleFileExplorer();
                break;
        }
    } else if (editor.mode == PROMPT_MODE) {
        // Handle input for the prompt here
        if (editor.promptUser(editor.promptMessage, c, editor.searchQuery)) {
            // User pressed Enter, prompt finished successfully
            if (editor.promptMessage.rfind("Search:", 0) == 0) { // Check if it was a search prompt
                editor.performSearch();
            }
            // Add other prompt types here (e.g., new file name, delete confirmation)
        }
    }
}

int main(int argc, char* argv[]) {
    enableRawMode();
    atexit(disableRawMode);

    Editor editor;

    if (argc >= 2) {
        editor.openFile(argv[1]);
    }

    while (true) {
        editor.refreshScreen();

        int c = readKey();

        // Global commands (regardless of mode)
        if (c == CMD_QUIT || (c == 'q' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (editor.mode == EDIT_MODE && editor.isDirty()) {
                editor.statusMessage = "WARNING: Unsaved changes. Ctrl-Q again to quit.";
                editor.statusMessageTime = GetTickCount64();
                int second_c = readKey();
                if (!(second_c == CMD_QUIT || (second_c == 'q' && GetAsyncKeyState(VK_CONTROL) & 0x8000))) {
                    continue;
                }
            }
            break;
        }
        else if (c == CMD_SAVE || (c == 's' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            processKeyPress(editor, CMD_SAVE);
        }
        else if (c == CMD_OPEN || (c == 'o' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            processKeyPress(editor, CMD_OPEN);
        }
        else if (c == 5 || (c == 'e' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) { // Ctrl+E
            editor.toggleFileExplorer();
        }
        // NEW: Ctrl+F for Find
        else if (c == 6 || (c == 'f' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) { // Ctrl+F is ASCII 6
            editor.startSearch();
        }
        // NEW: Ctrl+N for Find Next, Ctrl+P for Find Previous (common shortcuts)
        else if (c == 14 || (c == 'n' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) { // Ctrl+N is ASCII 14
            if (editor.mode == EDIT_MODE) editor.findNext();
        }
        else if (c == 16 || (c == 'p' && GetAsyncKeyState(VK_CONTROL) & 0x8000)) { // Ctrl+P is ASCII 16
            if (editor.mode == EDIT_MODE) editor.findPrevious();
        }
        else {
            processKeyPress(editor, c);
        }
    }

    clearScreen();
    setCursorPosition(0, 0);
    std::cout << "Exiting editor." << std::endl;

    return 0;
}