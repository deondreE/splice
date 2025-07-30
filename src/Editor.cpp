#include "editor.h"
#include <sstream>
#include <algorithm>
#include "win_console_utils.h"
#include <iostream>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

DWORD WINAPI TerminalOutputReaderThread(LPVOID lpParam);

///Lua
int lua_set_status_message(lua_State* L) {
    // Get the Editor* from Lua's registry or closure (we'll push it into a closure)
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) {
        return luaL_error(L, "Editor instance not found in upvalue.");
    }

    // Check arguments: expects (string message)
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (message) must be a string.");
    }
    std::string message = lua_tostring(L, 1);

    editor->statusMessage = message;
    editor->statusMessageTime = GetTickCount64();
    return 0; // Number of return values on the Lua stack
}

int lua_insert_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Argument #1 (text) must be a string.");
    }
    std::string text = lua_tostring(L, 1);

    for (char c : text) {
        editor->insertChar(c);
    }
    return 0;
}

int lua_get_current_line_text(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");

    std::string line_text = "";
    if (editor->cursorY >= 0 && editor->cursorY < editor->lines.size()) {
        line_text = editor->lines[editor->cursorY];
    }
    lua_pushstring(L, line_text.c_str());
    return 1; // Return 1 value (the string)
}

int lua_get_cursor_x(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->cursorX);
    return 1;
}

int lua_get_cursor_y(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->cursorY);
    return 1;
}

int lua_get_line_count(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushinteger(L, editor->lines.size());
    return 1;
}

int lua_get_current_filename(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    lua_pushstring(L, editor->filename.c_str());
    return 1;
}

int lua_refresh_screen(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    editor->refreshScreen();
    return 0;
}

int lua_force_full_redraw(lua_State* L) {
    Editor* editor = (Editor*)lua_touserdata(L, lua_upvalueindex(1));
    if (!editor) return luaL_error(L, "Editor instance not found.");
    for(auto& s : editor->prevDrawnLines) s.assign(editor->screenCols, ' ');
    editor->prevStatusMessage = "";
    editor->prevMessageBarMessage = "";
    return 0;
}

int lua_get_current_time_ms(lua_State* L) {
    lua_pushinteger(L, GetTickCount64());
    return 1;
}

static const luaL_Reg editor_lib[] = {
    {"set_status_message", lua_set_status_message},
    {"insert_text", lua_insert_text},
    {"get_current_line_text", lua_get_current_line_text},
    {"get_cursor_x", lua_get_cursor_x},
    {"get_cursor_y", lua_get_cursor_y},
    {"get_line_count", lua_get_line_count},
    {"get_current_filename", lua_get_current_filename},
    {"refresh_screen", lua_refresh_screen},
    {"force_full_redraw", lua_force_full_redraw},
    {"get_current_time_ms", lua_get_current_time_ms},
    {NULL, NULL}  // Sentinel
};
///

Editor::Editor() : 
    cursorX(0), 
    cursorY(0), 
    screenRows(0), 
    screenCols(0),
    rowOffset(0), 
    colOffset(0), 
    lineNumberWidth(0),
    statusMessage("HELP: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer | Ctrl-F = find | Ctrl-L = plugins"),
    statusMessageTime(GetTickCount64()),
    prevStatusMessage(""), 
    prevMessageBarMessage(""), 
    dirty(false),
    mode(EDIT_MODE), 
    selectedFileIndex(0),
    fileExplorerScrollOffset(0),
    currentMatchIndex(-1), 
    originalCursorX(0), 
    originalCursorY(0),
    originalRowOffset(0), 
    originalColOffset(0), 
    L(nullptr),
    terminalActive(false),
    terminalCursorX(0),
    terminalScrollOffset(0),
    terminalHeight(0),
    terminalWidth(0),
    hChildStdinRead(NULL), hChildStdinWrite(NULL), hChildStdoutRead(NULL), hChildStdoutWrite(NULL),
    currentFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE), // Default white
    currentBgColor(0),
    currentBold(false),
    defaultFgColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE),
    defaultBgColor(0)
{
    lines.push_back("");
    updateScreenSize();
    prevDrawnLines.resize(screenRows - 2);
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');

    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    currentDirPath = buffer;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        defaultFgColor = csbi.wAttributes & 0x0F;
        defaultBgColor = csbi.wAttributes & 0x0F;
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
    }

    clearTerminalBuffer();

    initializeLua();
    loadLuaPlugins(); 
}

Editor::~Editor() {
    finalizeLua();
    stopTerminal();
}

void Editor::initializeLua() {
    L = luaL_newstate(); // Create new Lua state
    if (!L) {
        std::cerr << "Error: Failed to create Lua state." << std::endl;
        exit(1);
    }

    luaL_openlibs(L); // Open all standard Lua libraries (base, table, io, string, math, debug etc.)

    // Expose Editor API to Lua
    exposeEditorToLua();

    statusMessage = "Lua interpreter initialized.";
    statusMessageTime = GetTickCount64();
}

void Editor::finalizeLua() {
    if (L) {
        lua_close(L); // Close Lua state, cleans up everything
        L = nullptr;
    }
    statusMessage = "Lua interpreter finalized.";
    statusMessageTime = GetTickCount64();
}

void Editor::exposeEditorToLua() {
    // Create a new table (like a Python module)
    lua_newtable(L);
    int editor_api_table_idx = lua_gettop(L); // Get its index on the stack

    // Push the 'this' pointer to Editor onto the stack as userdata.
    // This userdata will be used as an 'upvalue' for all functions in editor_lib.
    lua_pushlightuserdata(L, this); // 'light' means Lua doesn't manage its memory

    // Register all C functions in editor_lib into the new table
    // For each function in editor_lib, lua_pushcclosure pushes the C function
    // along with 1 upvalue (the Editor* userdata)
    luaL_setfuncs(L, editor_lib, 1);

    // Set the table as a global variable named 'editor_api'
    lua_setglobal(L, "editor_api");

    // Add plugin directory to Lua's package.path
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path"); // Get package.path
    std::string current_path = lua_tostring(L, -1);
    current_path += ";./plugins/?.lua"; // Add plugins dir relative to executable
    lua_pop(L, 1); // Pop old path
    lua_pushstring(L, current_path.c_str());
    lua_setfield(L, -2, "path"); // Set new path
    lua_pop(L, 1); // Pop package table

    // Check for errors
    if (lua_status(L) != LUA_OK) {
        std::cerr << "Error exposing Editor to Lua: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1); // Pop error message
    }
}

void Editor::loadLuaPlugins(const std::string& pluginDir) {
    if (!L) {
        statusMessage = "Lua interpreter not initialized, cannot load plugins.";
        statusMessageTime = GetTickCount64();
        return;
    }
    statusMessage = "Loading Lua plugins from '" + pluginDir + "'...";
    statusMessageTime = GetTickCount64();

    CreateDirectoryA(pluginDir.c_str(), NULL); // Ensure plugins directory exists

    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA((pluginDir + "\\*.lua").c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "No Lua plugins found in '" + pluginDir + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string luaFileName = findFileData.cFileName;
        if (luaFileName == "." || luaFileName == "..") continue;

        std::string luaFilePath = pluginDir + "\\" + luaFileName;

        // Load and execute the Lua script
        int status = luaL_dofile(L, luaFilePath.c_str());
        if (status != LUA_OK) {
            std::string error_msg = lua_tostring(L, -1);
            lua_pop(L, 1); // Pop error message
            statusMessage = "Error loading Lua plugin '" + luaFileName + "': " + error_msg;
            statusMessageTime = GetTickCount64();
            std::cerr << "Lua Error: " << error_msg << std::endl;
        } else {
            statusMessage = "Loaded Lua plugin: " + luaFileName;
            statusMessageTime = GetTickCount64();

            // Optional: Call an 'on_load' function in the plugin if it exists
            lua_getglobal(L, "on_load"); // Try to get a global 'on_load' function from the plugin
            if (lua_isfunction(L, -1)) {
                // Call it (no arguments)
                status = lua_pcall(L, 0, 0, 0); // 0 args, 0 results, 0 error func
                if (status != LUA_OK) {
                    std::string error_msg = lua_tostring(L, -1);
                    lua_pop(L, 1);
                    statusMessage = "Error in plugin 'on_load': " + error_msg;
                    statusMessageTime = GetTickCount64();
                    std::cerr << "Lua on_load Error: " << error_msg << std::endl;
                }
            } else {
                lua_pop(L, 1); // Pop nil if on_load not found
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
    statusMessage = "Finished loading Lua plugins.";
    statusMessageTime = GetTickCount64();
}

bool Editor::executeLuaPluginCommand(const std::string& pluginName, const std::string& commandName) {
    if (!L) {
        statusMessage = "Lua interpreter not initialized, cannot execute plugin command.";
        statusMessageTime = GetTickCount64();
        return false;
    }
    
    // Attempt to get the function from the global scope (assuming plugins define global functions)
    // Or if plugins create a table/module, you'd do:
    // lua_getglobal(L, pluginName.c_str());
    // if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; } // Not a table
    // lua_getfield(L, -1, commandName.c_str());
    // lua_remove(L, -2); // Pop the table
    
    // For simplicity, let's assume `commandName` is a global function directly
    lua_getglobal(L, commandName.c_str()); // Push function onto stack

    if (!lua_isfunction(L, -1)) {
        std::string type_name = lua_typename(L, lua_type(L, -1));
        lua_pop(L, 1); // Pop non-function value
        statusMessage = "Command '" + commandName + "' not found or not a function in Lua. Type was: " + type_name;
        statusMessageTime = GetTickCount64();
        return false;
    }

    // Call the Lua function (0 args, 0 results, 0 error handler)
    int status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK) {
        std::string error_msg = lua_tostring(L, -1);
        lua_pop(L, 1); // Pop error message
        statusMessage = "Error executing Lua command '" + commandName + "': " + error_msg;
        statusMessageTime = GetTickCount64();
        std::cerr << "Lua Command Error: " << error_msg << std::endl;
        return false;
    }

    statusMessage = "Lua command '" + commandName + "' executed.";
    statusMessageTime = GetTickCount64();
    return true;
}

void Editor::calculateLineNumberWidth()
{
    size_t effectiveLineNumbers = std::max(static_cast<size_t>(1), lines.size());
    int numLines = static_cast<int>(effectiveLineNumbers);

    int digits = 0;
    if (numLines == 0) digits = 1;
    else {
        int temp = numLines;
        while (temp > 0) {
            temp /= 10;
            ++digits;
        }
    }
    lineNumberWidth = digits + 1;
}

void Editor::updateScreenSize() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi); // Use GetStdHandle locally
    int newScreenRows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int newScreenCols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

    if (newScreenRows != screenRows || newScreenCols != screenCols)
    {
        screenRows = newScreenRows;
        screenCols = newScreenCols;
        calculateLineNumberWidth();

        prevDrawnLines.resize(screenRows - 2);
        for (auto& s : prevDrawnLines)
        {
            if (s.length() != screenCols)
            {
                s.assign(screenCols, ' ');
            }
        }
        clearScreen();
        prevStatusMessage = "";
        prevMessageBarMessage = "";
    }
}

void Editor::drawScreenContent() {
    int effectiveScreenCols = screenCols - lineNumberWidth;

    for (int i = 0; i < screenRows - 2; ++i) {
        setCursorPosition(0, i);

        int fileRow = rowOffset + i;
        std::string lineToDraw;

        std::ostringstream ss_lineNumber;
        if (fileRow < lines.size()) {
            ss_lineNumber << std::setw(lineNumberWidth - 1) << (fileRow + 1) << " ";
        } else {
            ss_lineNumber << std::string(lineNumberWidth - 1, ' ') << "~";
        }
        lineToDraw += ss_lineNumber.str();

        std::string renderedTextContent = "";
        if (fileRow < lines.size()) {
            std::string fullRenderedLine = getRenderedLine(fileRow);

            int startCharInRenderedLine = colOffset;
            int endCharInRenderedLine = colOffset + effectiveScreenCols;

            if (startCharInRenderedLine < 0) startCharInRenderedLine = 0;
            if (startCharInRenderedLine > (int)fullRenderedLine.length()) startCharInRenderedLine = (int)fullRenderedLine.length();

            if (endCharInRenderedLine > (int)fullRenderedLine.length()) endCharInRenderedLine = (int)fullRenderedLine.length();
            if (endCharInRenderedLine < 0) endCharInRenderedLine = 0;

            if (endCharInRenderedLine > startCharInRenderedLine) {
                renderedTextContent = fullRenderedLine.substr(startCharInRenderedLine, endCharInRenderedLine - startCharInRenderedLine);
            }
        }

        bool lineHasHighlight = false;
        if (!searchQuery.empty() && currentMatchIndex != -1) {
            // Check if any match is on this fileRow
            for (const auto& match : searchResults) {
                if (match.first == fileRow) {
                    lineHasHighlight = true;
                    break;
                }
            }
        }
        
        std::string fullLineContentForDiff = lineToDraw + renderedTextContent + std::string(screenCols - (lineToDraw.length() + renderedTextContent.length()), ' ');

        if (lineHasHighlight || i >= prevDrawnLines.size() || prevDrawnLines[i] != fullLineContentForDiff) {
            setCursorPosition(0, i);
            resetTextColor(); // Default color for text area

            // Draw line number part first
            writeStringAt(0, i, lineToDraw.substr(0, lineNumberWidth));

            // Now iterate through the visible part of the rendered text content for highlighting
            int currentRenderedCol = lineNumberWidth;
            for (int k = 0; k < renderedTextContent.length(); ++k) {
                char ch = renderedTextContent[k];
                bool isHighlight = false;
                
                // Check if current char is part of the highlighted search result
                if (!searchQuery.empty() && currentMatchIndex != -1 && fileRow == searchResults[currentMatchIndex].first) {
                    // Convert search result char index to rendered index to compare
                    int matchRenderedStart = cxToRx(fileRow, searchResults[currentMatchIndex].second);
                    int matchRenderedEnd = cxToRx(fileRow, searchResults[currentMatchIndex].second + searchQuery.length());

                    // Relative to visible screen (colOffset)
                    if (currentRenderedCol - lineNumberWidth + colOffset >= matchRenderedStart &&
                        currentRenderedCol - lineNumberWidth + colOffset < matchRenderedEnd) {
                        isHighlight = true;
                    }
                }

                if (isHighlight) {
                    setTextColor(BG_YELLOW | BLACK); // Highlight: Black on Yellow
                } else {
                    resetTextColor(); // Default color
                }
                writeStringAt(currentRenderedCol, i, std::string(1, ch)); // Write one character
                currentRenderedCol++;
            }
            resetTextColor(); // Reset after text
            
            // Clear rest of the line to ensure old highlights are gone
            writeStringAt(currentRenderedCol, i, std::string(screenCols - currentRenderedCol, ' '));
            
            if (i < prevDrawnLines.size()) {
                prevDrawnLines[i] = fullLineContentForDiff; // Update prevDrawnLines with the full rendered line (no colors)
            }
        }
    }
    drawStatusBar();
    drawMessageBar();
}

void Editor::startSearch() {
    originalCursorX = cursorX; // Save original cursor position
    originalCursorY = cursorY;
    originalRowOffset = rowOffset;
    originalColOffset = colOffset;

    mode = PROMPT_MODE;
    promptMessage = "Search: ";
    searchQuery = ""; // Clear previous search query
    searchResults.clear(); // Clear previous results
    currentMatchIndex = -1; // No match selected yet
    statusMessage = "Enter search term. ESC to cancel, Enter to search.";
    statusMessageTime = GetTickCount64();

    // Invalidate prevDrawnLines to ensure prompt is drawn fresh
    for(auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
    clearScreen(); // Force full screen redraw to show prompt cleanly
}

void Editor::performSearch() {
    searchResults.clear();
    currentMatchIndex = -1;

    if (searchQuery.empty()) {
        statusMessage = "Search cancelled or empty.";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // Exit search mode if query is empty
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Iterate through lines to find all matches
    for (int r = 0; r < lines.size(); ++r) {
        size_t pos = lines[r].find(searchQuery, 0);
        while (pos != std::string::npos) {
            searchResults.push_back({r, (int)pos});
            pos = lines[r].find(searchQuery, pos + 1); // Find next occurrence after current match
        }
    }

    if (searchResults.empty()) {
        statusMessage = "No matches found for '" + searchQuery + "'";
        statusMessageTime = GetTickCount64();
        mode = EDIT_MODE; // No matches, go back to edit mode
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return;
    }

    // Found matches. Go to the first one.
    currentMatchIndex = 0;
    statusMessage = "Found " + std::to_string(searchResults.size()) + " matches. (N)ext (P)rev";
    statusMessageTime = GetTickCount64();

    // Move cursor and scroll to the first match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    
    // Adjust scroll to make sure the found match is visible
    scroll(); // This should adjust rowOffset/colOffset
    
    mode = EDIT_MODE; // Exit search prompt mode
}


void Editor::findNext() {
    if (searchResults.empty()) return;

    currentMatchIndex = (currentMatchIndex + 1) % searchResults.size();

    // Move cursor and scroll to the new match
    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll(); // Adjust scroll

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

void Editor::findPrevious() {
    if (searchResults.empty()) return;

    currentMatchIndex--;
    if (currentMatchIndex < 0) {
        currentMatchIndex = searchResults.size() - 1;
    }

    cursorY = searchResults[currentMatchIndex].first;
    cursorX = searchResults[currentMatchIndex].second;
    scroll();

    statusMessage = "Match " + std::to_string(currentMatchIndex + 1) + " of " + std::to_string(searchResults.size());
    statusMessageTime = GetTickCount64();
}

bool Editor::promptUser(const std::string& prompt, int input_c, std::string& result) {
    if (input_c == 13) { // Enter
        mode = EDIT_MODE; // Exit prompt mode
        return true; // Success
    } else if (input_c == 27) { // Escape
        mode = EDIT_MODE; // Exit prompt mode
        result.clear(); // Clear the input
        statusMessage = "Operation cancelled.";
        statusMessageTime = GetTickCount64();
        cursorX = originalCursorX; // Restore cursor
        cursorY = originalCursorY;
        rowOffset = originalRowOffset; // Restore scroll
        colOffset = originalColOffset;
        return false; // Cancelled
    } else if (input_c == 8) { // Backspace
        if (!result.empty()) {
            result.pop_back();
        }
    } else if (input_c >= 32 && input_c <= 126) { // Printable characters
        result += static_cast<char>(input_c);
    }
    // Update the message bar with the current prompt and input
    statusMessage = prompt + result;
    statusMessageTime = GetTickCount64(); // Keep message alive while typing

    // Return false to indicate prompt mode is still active
    return false;
}

void Editor::drawStatusBar() {
    std::string currentStatus;
    std::string filename_display = filename.empty() ? "[No Name]" : filename;
    currentStatus += filename_display;
    currentStatus += " - " + std::to_string(lines.size()) + " lines";

    std::string cursor_info = std::to_string(cursorY + 1) + "/" + std::to_string(lines.size());
    std::string spaces(screenCols - currentStatus.length() - cursor_info.length(), ' ');

    currentStatus += spaces + cursor_info;

    // --- Only redraw if status bar content has changed ---
    if (currentStatus != prevStatusMessage) {
        setCursorPosition(0, screenRows - 2); // Second to last row
        setTextColor(BG_BLUE | WHITE);
        writeStringAt(0, screenRows - 2, currentStatus);
        prevStatusMessage = currentStatus;
    }
}

void Editor::drawMessageBar() {
    std::string currentMessage = "";
    ULONGLONG currentTime = GetTickCount64();
    if (currentTime - statusMessageTime < 5000) { // Display for 5 seconds
        currentMessage = trimRight(statusMessage);
    }
    currentMessage += std::string(screenCols - currentMessage.length(), ' ');

    // --- Only redraw if message bar content has changed ---
    if (currentMessage != prevMessageBarMessage) {
        setCursorPosition(0, screenRows - 1); // Last row
        setTextColor(YELLOW | INTENSITY);
        writeStringAt(0, screenRows - 1, currentMessage);
        prevMessageBarMessage = currentMessage;
    }
}

// Helper to trim trailing spaces (useful for status message display)
std::string Editor::trimRight(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

void Editor::scroll() {
    // ... vertical scrolling (unchanged) ...
    if (cursorY < rowOffset) {
        rowOffset = cursorY;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
    if (cursorY >= rowOffset + screenRows - 2) {
        rowOffset = cursorY - (screenRows - 2) + 1;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }

    // --- MODIFIED: Horizontal scrolling to use rendered columns ---
    int renderedCursorX = cxToRx(cursorY, cursorX); // Get visual position of cursor
    int renderedLineLength = getRenderedLine(cursorY).length(); // Get visual length of current line

    if (renderedCursorX < colOffset) {
        colOffset = renderedCursorX;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
    if (renderedCursorX >= colOffset + (screenCols - lineNumberWidth)) {
        colOffset = renderedCursorX - (screenCols - lineNumberWidth) + 1;
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    }
}

void Editor::moveCursor(int key) {
    int oldCursorX = cursorX;
    int oldCursorY = cursorY;
    std::string currentLine = (cursorY < lines.size()) ? lines[cursorY] : lines.front();

    switch (key) {
    case ARROW_LEFT:
        if (cursorX > 0) {
            // To move left, decrement cursorX.
            // If the character at cursorX-1 is a tab,
            // this is fine for internal storage.
            cursorX--;
        }
        else if (cursorY > 0) {
            cursorY--;
            cursorX = lines[cursorY].length(); // Move to end of previous line (char index)
        }
        break;
    case ARROW_RIGHT:
        if (cursorY < lines.size() && cursorX < currentLine.length()) {
            // To move right, increment cursorX.
            // This correctly moves to the next character index.
            cursorX++;
        }
        else if (cursorY < lines.size() - 1) { // Move to beginning of next line
            cursorY++;
            cursorX = 0;
        }
        break;
    case ARROW_UP:
        if (cursorY > 0) {
            // When moving up/down, we want the cursor to try and stay at the
            // *same visual column* if possible.
            // So, first convert current cx to rx, then convert that rx to new cx on target line.
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY--;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0; // If somehow moved past valid lines, reset
            }
        }
        break;
    case ARROW_DOWN:
        if (cursorY < lines.size() - 1) {
            // Same logic as ARROW_UP
            int currentRx = cxToRx(cursorY, cursorX);
            cursorY++;
            if (cursorY < lines.size()) {
                cursorX = rxToCx(cursorY, currentRx); // Find the character index closest to the current rendered column
            }
            else {
                cursorX = 0;
            }
        }
        break;
    case HOME_KEY:
        cursorX = 0; // Always character index 0
        break;
    case END_KEY:
        if (cursorY < lines.size()) {
            cursorX = lines[cursorY].length(); // Always end of character string
        }
        break;
    case PAGE_UP:
    case PAGE_DOWN: {
        int currentRx = cxToRx(cursorY, cursorX); // Store current rendered X before jump
        int times = screenRows - 2;
        if (key == PAGE_UP) {
            cursorY = std::max(0, cursorY - times);
        }
        else { // PAGE_DOWN
            cursorY = std::min((int)lines.size() - 1, cursorY + times);
        }
        // After changing Y, re-calculate X based on old rendered position
        if (cursorY < lines.size()) {
            cursorX = rxToCx(cursorY, currentRx);
        }
        else if (lines.empty()) { // Edge case: empty file, moved page
            cursorX = 0;
            cursorY = 0;
        }
        else { // Landed on an invalid line, default to end of last line
            cursorY = lines.size() - 1;
            cursorX = lines[cursorY].length();
        }
        break;
    }
    }
}

void Editor::insertChar(int c) {
    if (cursorY == lines.size()) {
        lines.push_back("");
    }
    lines[cursorY].insert(cursorX, 1, static_cast<char>(c));
    cursorX++;
    statusMessage = "";
    statusMessageTime = 0; // Clear immediately
    calculateLineNumberWidth();
    dirty = true;
}

void Editor::insertNewline() {
    if (cursorX == lines[cursorY].length()) {
        lines.insert(lines.begin() + cursorY + 1, "");
    }
    else {
        std::string remaining = lines[cursorY].substr(cursorX);
        lines[cursorY].erase(cursorX);
        lines.insert(lines.begin() + cursorY + 1, remaining);
    }
    cursorY++;
    cursorX = 0;
    calculateLineNumberWidth();
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteChar() { // Backspace
    if (cursorY == lines.size()) return;
    if (cursorX == 0 && cursorY == 0 && lines[0].empty()) {
        return;
    }

    if (cursorX > 0) {
        lines[cursorY].erase(cursorX - 1, 1);
        cursorX--;
    }
    else {
        cursorX = lines[cursorY - 1].length();
        lines[cursorY - 1] += lines[cursorY];
        lines.erase(lines.begin() + cursorY);
        cursorY--;
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}

void Editor::deleteForwardChar() { // Delete key
    if (cursorY == lines.size()) return;
    if (cursorX == lines[cursorY].length() && cursorY == lines.size() - 1) {
        return;
    }

    if (cursorX < lines[cursorY].length()) {
        lines[cursorY].erase(cursorX, 1);
    }
    else {
        lines[cursorY] += lines[cursorY + 1];
        lines.erase(lines.begin() + cursorY + 1);
        calculateLineNumberWidth();
    }
    statusMessage = "";
    statusMessageTime = 0;
    dirty = true;
}


bool Editor::openFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        statusMessage = "Error: Could not open file '" + path + "'";
        statusMessageTime = GetTickCount64();
        return false;
    }

    lines.clear();
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    if (lines.empty()) {
        lines.push_back("");
    }

    filename = path;
    cursorX = 0;
    cursorY = 0;
    rowOffset = 0;
    colOffset = 0;
    calculateLineNumberWidth();
    statusMessage = "Opened '" + path + "'";
    statusMessageTime = GetTickCount64();

    // Force a full redraw after file open
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";

    dirty = false;
    return true;
}

bool Editor::saveFile() {
    if (filename.empty()) {
        statusMessage = "Cannot save: No filename specified. Use Ctrl-O to open/create a file.";
        statusMessageTime = GetTickCount64();
        return false;
    }

    std::ofstream file(filename);
    if (!file.is_open()) {
        statusMessage = "Error: Could not save file '" + filename + "'";
        statusMessageTime = GetTickCount64();
        dirty = false;
        return false;
    }

    for (const auto& line : lines) {
        file << line << "\r\n"; // Windows line endings (CRLF)
    }
    file.close();

    statusMessage = "Saved '" + filename + "' (" + std::to_string(lines.size()) + " lines)";
    statusMessageTime = GetTickCount64();
    return true;
}

int Editor::cxToRx(int lineIndex, int cx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int rx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < cx; ++i) {
        if (line[i] == '\t') {
            rx += (KILO_TAB_STOP - (rx % KILO_TAB_STOP));
        }
        else {
            ++rx;
        }
    }
    return rx;
}

int Editor::rxToCx(int lineIndex, int rx)
{
    if (lineIndex < 0 || lineIndex >= lines.size()) return 0;

    int currentRx = 0;
    int cx = 0;
    const std::string& line = lines[lineIndex];
    for (int i = 0; i < line.length(); ++i) {
        if (line[i] == '\t') {
            currentRx += (KILO_TAB_STOP - (currentRx % KILO_TAB_STOP));
        }
        else {
            currentRx++;
        }
        if (currentRx > rx) return cx;
        ++cx;
    }
    return cx;
}

std::string Editor::getRenderedLine(int fileRow)
{
    if (fileRow < 0 || fileRow >= lines.size()) {
        return "";
    }
    std::string renderedLine;
    const std::string& originalLine = lines[fileRow];
    for (char c : originalLine) {
        if (c == '\t') {
            int spacesToAdd = KILO_TAB_STOP - (renderedLine.length() % KILO_TAB_STOP);
            renderedLine.append(spacesToAdd, ' ');
        }
        else {
            renderedLine += c;
        }
    }
    return renderedLine;
}

void Editor::toggleFileExplorer() {
    if (mode == EDIT_MODE) {
        mode = FILE_EXPLORER_MODE;
        statusMessage = "File Explorer Mode: Navigate with arrows, Enter to open/CD, N for New, D for Delete.";
        statusMessageTime = GetTickCount64();
        populateDirectoryEntries(currentDirPath); // Populate entries when entering mode
        selectedFileIndex = 0; // Reset selection
        fileExplorerScrollOffset = 0; // Reset scroll
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    else { // FILE_EXPLORER_MODE
        mode = EDIT_MODE;
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-O = open | Ctrl-E = explorer";
        statusMessageTime = GetTickCount64();
        directoryEntries.clear(); // Clear entries when exiting
        clearScreen(); // Force full redraw when changing modes
        for (auto& s : prevDrawnLines) s.assign(screenCols, ' '); // Invalidate text editor view
    }
    prevStatusMessage = ""; // Force status bar redraw
    prevMessageBarMessage = ""; // Force message bar redraw
}

void Editor::populateDirectoryEntries(const std::string& path) {
    directoryEntries.clear();

    // Add ".." entry for navigating up
    directoryEntries.push_back({ "..", true });

    WIN32_FIND_DATAA findFileData; // Using A for ANSI string
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &findFileData); // Find files and directories

    if (hFind == INVALID_HANDLE_VALUE) {
        statusMessage = "Error: Could not list directory '" + path + "'";
        statusMessageTime = GetTickCount64();
        return;
    }

    do {
        std::string entryName = findFileData.cFileName;
        // Skip "." and ".." as we handle ".." explicitly
        if (entryName == "." || entryName == "..") {
            continue;
        }

        bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        directoryEntries.push_back({ entryName, isDir });

    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Sort entries: directories first, then files, both alphabetically
    std::sort(directoryEntries.begin() + 1, directoryEntries.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory && !b.isDirectory) return true;
        if (!a.isDirectory && b.isDirectory) return false;
        return a.name < b.name;
        });

    currentDirPath = path; // Update current path
    statusMessage = "Viewing: " + currentDirPath;
    statusMessageTime = GetTickCount64();
}

void Editor::moveFileExplorerSelection(int key) {
    if (directoryEntries.empty()) return;

    int oldSelectedFileIndex = selectedFileIndex;

    if (key == ARROW_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - 1);
    }
    else if (key == ARROW_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + 1);
    }
    else if (key == PAGE_UP) {
        selectedFileIndex = std::max(0, selectedFileIndex - (screenRows - 4)); // -4 for bars and top/bottom padding
    }
    else if (key == PAGE_DOWN) {
        selectedFileIndex = std::min((int)directoryEntries.size() - 1, selectedFileIndex + (screenRows - 4));
    }
    else if (key == HOME_KEY) {
        selectedFileIndex = 0;
    }
    else if (key == END_KEY) {
        selectedFileIndex = directoryEntries.size() - 1;
    }

    // Adjust scroll offset
    if (selectedFileIndex < fileExplorerScrollOffset) {
        fileExplorerScrollOffset = selectedFileIndex;
    }
    if (selectedFileIndex >= fileExplorerScrollOffset + (screenRows - 4)) {
        fileExplorerScrollOffset = selectedFileIndex - (screenRows - 4) + 1;
    }

    if (oldSelectedFileIndex != selectedFileIndex) {
        clearScreen();
    }
}

void Editor::handleFileExplorerEnter() {
    if (directoryEntries.empty() || selectedFileIndex < 0 || selectedFileIndex >= directoryEntries.size()) {
        return; // Nothing selected
    }

    DirEntry& selectedEntry = directoryEntries[selectedFileIndex];

    char newPathBuffer[MAX_PATH];
    if (PathCombineA(newPathBuffer, currentDirPath.c_str(), selectedEntry.name.c_str()) == NULL) {
        statusMessage = "Error: Invalid path combination.";
        statusMessageTime = GetTickCount64();
        return;
    }
    std::string newPath = newPathBuffer;

    if (selectedEntry.isDirectory) {
        // Change directory
        populateDirectoryEntries(newPath);
        selectedFileIndex = 0; // Reset selection in new directory
        fileExplorerScrollOffset = 0;
        clearScreen(); // Full refresh after CD
    }
    else {
        // Open file in editor mode
        openFile(newPath);
        toggleFileExplorer(); // Switch back to edit mode
    }
}

void Editor::drawFileExplorer() {
    clearScreen(); // Full clear to avoid artifacts when switching from editor

    int startRow = 0; // Starting screen row for explorer list

    // Display current path
    setCursorPosition(0, startRow);
    setTextColor(CYAN | INTENSITY);
    writeStringAt(0, startRow, "PATH: " + currentDirPath);
    resetTextColor();

    startRow += 2; // Leave a line for separation

    int visibleRows = screenRows - startRow - 2; // Remaining rows for list, excluding status/message bars

    for (int i = 0; i < visibleRows; ++i) {
        setCursorPosition(0, startRow + i);
        int entryIndex = fileExplorerScrollOffset + i;

        if (entryIndex < directoryEntries.size()) {
            const DirEntry& entry = directoryEntries[entryIndex];
            std::string lineToDraw;

            // Highlight selected item
            if (entryIndex == selectedFileIndex) {  
                setTextColor(BG_BLUE | WHITE | INTENSITY);
                lineToDraw += "> ";
            }
            else {
                setTextColor(WHITE);
                lineToDraw += "  ";
            }

            // Indicate directory or file
            if (entry.isDirectory) {
                setTextColor(BLUE | INTENSITY);
                lineToDraw += "[" + entry.name + "]";
            }
            else {
                setTextColor(WHITE);
                lineToDraw += " " + entry.name;
            }

            std::string formattedEntryText;
            if (entry.isDirectory) {
                formattedEntryText = "[" + entry.name + "]";
            }
            else {
                formattedEntryText = " " + entry.name;
            }

            std::string lineForDraw = (entryIndex == selectedFileIndex ? "> " : "  ") + formattedEntryText;
            lineForDraw += std::string(screenCols - lineForDraw.length(), ' '); // Pad to full width

            // Re-apply colors for the entire line to ensure consistent background
            if (entryIndex == selectedFileIndex) {
                setTextColor(BG_BLUE | WHITE | INTENSITY);
            }
            else if (entry.isDirectory) {
                setTextColor(BLUE | INTENSITY); // Directories, non-selected
            }
            else {
                setTextColor(WHITE); // Files, non-selected
            }

            writeStringAt(0, startRow + i, lineForDraw);
            resetTextColor(); // Reset for next line/outside this loop
        }
        else {
            // Draw empty lines beyond content with default colors
            setCursorPosition(0, startRow + i);
            writeStringAt(0, startRow + i, std::string(screenCols, ' '));
        }
    }
}

// Terminal
void Editor::toggleTerminal() {
    if (mode == TERMINAL_MODE) {
        mode = EDIT_MODE;
        stopTerminal();
        statusMessage = "Edit Mode: Ctrl-Q = quit | Ctrl-S = save | Ctrl-o = open | Ctrl-E = fileexplorer";
        statusMessageTime = GetTickCount64();
    } else {
        mode = TERMINAL_MODE;
        startTerminal();
        statusMessage = "Temrinal Mode: Ctrl-T to close";
        statusMessageTime = GetTickCount64();
    }
    clearScreen();
    for (auto& s : prevDrawnLines) s.assign(screenCols, ' ');
    prevStatusMessage = "";
    prevMessageBarMessage = "";
}

void Editor::startTerminal() {
    if (piProcInfo.hProcess != NULL) {
        stopTerminal();
    }

    ZeroMemory(&piProcInfo, sizeof(piProcInfo));

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);   
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // create pipe for stdout.
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &saAttr, 0)) {
        statusMessage = "Error: Stdout pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64(); 
        return;
    }
    // ensure the read handle is not inherited.
    if (!SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        statusMessage = "Error: Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64(); 
        return;
    }

    // Create a pipe for stdin
    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &saAttr, 0)) {
        statusMessage = "Error: Stdin pipe creation failed. (GLE: " + std::to_string(GetLastError()) = ")";
        statusMessageTime = GetTickCount64();
        CloseHandle(hChildStdinRead); 
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); 
        CloseHandle(hChildStdoutWrite);
        return;
    }

    // setup a start for child process
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hChildStdoutWrite;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdInput = hChildStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    const char* shell_path = "cmd.exe";
    char commandLine[MAX_PATH];
    sprintf_s(commandLine, MAX_PATH, "%s", shell_path);

    BOOL success = CreateProcessA(
        NULL,           // lpApplicationName
        commandLine,    // lpCommandLine
        NULL,           // lpProcessAttributes
        NULL,           // lpThreadAttributes
        TRUE,           // bInheritHandles (VERY IMPORTANT for pipe redirection)
        CREATE_NEW_CONSOLE | NORMAL_PRIORITY_CLASS,
        NULL,           // lpEnvironment
        NULL,           // lpCurrentDirectory
        &si,            // lpStartupInfo
        &piProcInfo     // lpProcessInformation
    );

    if (!success) {
        statusMessage = "Error: Failed to create child process. (GLE: " + std::to_string(GetLastError()) + ")";
        statusMessageTime = GetTickCount64();
        
        CloseHandle(hChildStdinRead); CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead); CloseHandle(hChildStdoutWrite);
        return;
    }

    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdinWrite);
    
    terminalActive = true;
    CreateThread(NULL, 0, TerminalOutputReaderThread, this, 0, NULL);
    
    statusMessage = "Terminal Started. Shell: " + std::string(shell_path);
    statusMessageTime = GetTickCount64();

    clearTerminalBuffer();
    terminalCursorX = 0;
    terminalCursorY = 0;
    terminalScrollOffset = 0;
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
    ansiSGRParams.clear();
    asiEscapeBuffe.clear();

    resizeTerminal(screenCols, screenRows - 2);
}

void Editor::stopTerminal() {
    if (!terminalActive) return;
    terminalActive = false;

    if (piProcInfo.hProcess != NULL) {
        TerminateProcess(piProcInfo.hProcess, 0);
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);
        ZeroMemory(&piProcInfo, sizeof(piProcInfo));
    }

    if (hChildStdinWrite != NULL) { CloseHandle(hChildStdinWrite); hChildStdinWrite = NULL; }
    if (hChildStdoutRead != NULL) { CloseHandle(hChildStdoutRead); hChildStdoutRead = NULL; }

    statusMessage = "Terminal stopped.";
    statusMessageTime = GetTickCount64();
}

DWORD WINAPI TerminalOutputReaderThread(LPVOID lpParam) {
    Editor* editor = (Editor*)lpParam;
    char buffer[4096];
    DWORD bytesRead;

    const DWORD read_interval_ms = 10;

    while(editor->terminalActive) {
        if (ReadFile(editor->hChildStdoutRead, buffer, sizeof(buffer), &bytesRead, NULL)) {
            if (bytesRead > 0) {
                editor->processTerminalOutput(std::string(buffer, bytesRead));
            } 
        } else {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
                if (editor->terminalActive) {
                    editor->stopTerminal();
                }
                break;
            } else {
                std::cerr << "Terminal ReadFile Error: " << error << std::endl;
            }
        }
        Sleep(read_interval_ms);
    }
    return 0;
}

void Editor::readTerminalOutput() {
    // The reading is handled by the background thread.
    // This function can be a no-op or merely exist to fit the refreshScreen dispatch.
    // The thread calls processTerminalOutput, which will modify the buffer
    // and ideally trigger a redraw (which refreshScreen handles on next loop).
}

void Editor::writeTerminalInput(const std::string& input) {
    if (hChildStdinWrite == NULL) return;
    DWORD bytesWriten;

    WriteFile(hChildStdinWrite, input.c_str(), input.length(), &bytesWriten, NULL);
}

void Editor::processTerminalOutput(const std::string& data) {
    // Basic ANSI parser state machine
    for (char c : data) {
        if (!asiEscapeBuffe.empty()) {
            asiEscapeBuffe += c;
            if (c >= '@' && c <= '~') { // End of a CSI (Control Sequence Introducer) sequence
                if (asiEscapeBuffe.length() > 2 && asiEscapeBuffe[1] == '[') {
                    std::string params_str = asiEscapeBuffe.substr(2, asiEscapeBuffe.length() - 3);
                    ansiSGRParams.clear();

                    if (params_str.empty()) {
                        ansiSGRParams.push_back(0);
                    } else {
                        std::istringstream iss(params_str);
                        std::string segment;
                        while(std::getline(iss, segment, ';')) {
                            try {
                                ansiSGRParams.push_back(std::stoi(segment));
                            } catch (...) { /* Invalid param, ignore */ }
                        }
                    }

                    char final_char = asiEscapeBuffe.back();
                    if (final_char == 'm') {
                        applyAnsiSGR(ansiSGRParams);
                    } else if (final_char == 'H' || final_char == 'f') { // CUP or HVP
                        int row = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                        int col = (ansiSGRParams.size() < 2) ? 1 : ansiSGRParams[1];
                        applyAnsiCUP(row, col);
                    } else if (final_char == 'J') { // ED (Erase Display)
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiED(param);
                    } else if (final_char == 'K') { // EL (Erase Line)
                        int param = ansiSGRParams.empty() ? 0 : ansiSGRParams[0];
                        applyAnsiEL(param);
                    } else if (final_char == 'A') { // CUU - Cursor Up
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::max(0, terminalCursorY - num);
                    } else if (final_char == 'B') { // CUD - Cursor Down
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorY = std::min(terminalHeight - 1, terminalCursorY + num);
                    } else if (final_char == 'C') { // CUF - Cursor Forward
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::min(terminalWidth - 1, terminalCursorX + num);
                    } else if (final_char == 'D') { // CUB - Cursor Backward
                         int num = ansiSGRParams.empty() ? 1 : ansiSGRParams[0];
                         terminalCursorX = std::max(0, terminalCursorX - num);
                    }
                    // Add more if needed (e.g., S/T for scroll, L/M for insert/delete line)
                }
                asiEscapeBuffe.clear();
            }
        } else if (c == 0x1B) { // ESC
            asiEscapeBuffe += c;
        } else if (c == '\r') {
            terminalCursorX = 0;
        } else if (c == '\n') {
            terminalCursorY++;
            terminalCursorX = 0; // Usually moves to column 0 on newline
            if (terminalCursorY >= terminalHeight) {
                // Scroll up the buffer by shifting lines
                for (int r = 0; r < terminalHeight - 1; ++r) {
                    terminalBuffer[r] = terminalBuffer[r+1];
                }
                // Clear the last line
                terminalBuffer[terminalHeight-1].assign(terminalWidth, { ' ', currentFgColor, currentBgColor, currentBold });
                terminalCursorY = terminalHeight - 1; // Stay on the last line
                // terminalScrollOffset is not used for internal buffer scroll here
            }
        } else if (c == '\b') { // Backspace
            if (terminalCursorX > 0) {
                terminalCursorX--;
                // Optionally: clear the character that was backspaced over
                if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                    terminalBuffer[terminalCursorY][terminalCursorX] = { ' ', currentFgColor, currentBgColor, currentBold };
                }
            }
        } else if (c == '\t') { // Tab
            terminalCursorX += (KILO_TAB_STOP - (terminalCursorX % KILO_TAB_STOP));
            if (terminalCursorX >= terminalWidth) terminalCursorX = terminalWidth - 1; // Clamp
            // Tab character itself is not stored in buffer if expanding
            // To store tab characters literally in buffer, then expand on drawing (like editor mode)
            // would require different handling. For now, it moves cursor.
        } else {
            // Regular character
            if (terminalCursorY < terminalHeight && terminalCursorX < terminalWidth) {
                terminalBuffer[terminalCursorY][terminalCursorX] = { c, currentFgColor, currentBgColor, currentBold };
            }
            terminalCursorX++;
            if (terminalCursorX >= terminalWidth) { // Auto-wrap
                terminalCursorX = 0;
                terminalCursorY++;
                if (terminalCursorY >= terminalHeight) { // Scroll if needed
                    for (int r = 0; r < terminalHeight - 1; ++r) {
                        terminalBuffer[r] = terminalBuffer[r+1];
                    }
                    terminalBuffer[terminalHeight-1].assign(terminalWidth, { ' ', currentFgColor, currentBgColor, currentBold });
                    terminalCursorY = terminalHeight - 1;
                }
            }
        }
    }

    for (auto& s : prevDrawnLines)
      s.assign(screenCols, ' ');
    prevStatusMessage = ""; 
    prevMessageBarMessage = "";
}

void Editor::resizeTerminal(int width, int height) {
    terminalWidth = width;
    terminalHeight = height;
    terminalBuffer.resize(terminalHeight);
    for (int r = 0; r < terminalHeight; ++r) {
        terminalBuffer[r].resize(terminalWidth, { ' ', defaultFgColor, defaultBgColor, false });
    }
    // Reset cursor and scroll after resize
    terminalCursorX = 0;
    terminalCursorY = 0;
    terminalScrollOffset = 0; // Not actively used for internal buffer scrolling in this model
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
    ansiSGRParams.clear();
    asiEscapeBuffe.clear();
    clearTerminalBuffer(); // Clear content
    
    for (auto& s : prevDrawnLines)
      s.assign(screenCols, ' ');  // Invalidate editor content cache
    prevStatusMessage = "";       // Invalidate status message cache
    prevMessageBarMessage = "";   // Invalidate message bar cache
}

void Editor::drawTerminalScreen() {
    for (int y = 0; y < terminalHeight; ++y) {
        setCursorPosition(0, y); // Start drawing at screen (0,y)
        for (int x = 0; x < terminalWidth; ++x) {
            TerminalChar tc = terminalBuffer[y][x];
            WORD attributes = tc.fgColor | tc.bgColor;
            if (tc.bold) attributes |= FOREGROUND_INTENSITY; // Apply intensity for bold

            // To avoid flashing/flicker from character-by-character writes,
            // we should group characters with the same attribute.
            // This is a simple per-char write, which can flicker.
            setTextColor(attributes);
            writeStringAt(x, y, std::string(1, tc.c));
        }
        resetTextColor();
    }
}

void Editor::clearTerminalBuffer() {
    terminalBuffer.assign(terminalHeight, std::vector<TerminalChar>(terminalWidth, { ' ', defaultFgColor, defaultBgColor, false }));
}

void Editor::applyAnsiSGR(const std::vector<int>& params) {
  if (params.empty()) {  // Default: reset all attributes
    currentFgColor = defaultFgColor;
    currentBgColor = defaultBgColor;
    currentBold = false;
  }
  for (int param : params) {
    switch (param) {
      case 0:  // Reset all attributes
        currentFgColor = defaultFgColor;
        currentBgColor = defaultBgColor;
        currentBold = false;
        break;
      case 1:  // Bold/Increased Intensity
        currentBold = true;
        break;
      case 22:  // Not Bold/Normal Intensity
        currentBold = false;
        break;
      case 30:
        currentFgColor = BLACK;
        break;
    case 31:
        currentFgColor = RED;
        break;
    case 32:
        currentFgColor = GREEN;
        break;
    case 33:
        currentFgColor = YELLOW;
        break;
    case 34:
        currentFgColor = BLUE;
        break;
    case 35:
        currentFgColor = MAGENTA;
        break;
      case 36:
        currentFgColor = CYAN;
        break;
      case 37:
        currentFgColor = WHITE;
        break;
      case 39:
        currentFgColor = defaultFgColor;
        break;  // Default foreground color
      case 40:
        currentBgColor = BLACK;
        break;
      case 41:
        currentBgColor = BG_RED;
        break;
      case 42:
        currentBgColor = BG_GREEN;
        break;
      case 43:
        currentBgColor = BG_YELLOW;
        break;
      case 44:
        currentBgColor = BG_BLUE;
        break;
      case 45:
        currentBgColor = BG_MAGENTA;
        break;
      case 46:
        currentBgColor = BG_CYAN;
        break;
      case 47:
        currentBgColor = BG_WHITE;
        break;
      case 49:
        currentBgColor = defaultBgColor;
        break;  // Default background color
      default:
        break;
    }
  }
}

void Editor::applyAnsiCUP(int row, int col) {
  // ANSI rows/cols are 1-based. Convert to 0-based.
  terminalCursorY = std::max(0, row - 1);
  terminalCursorX = std::max(0, col - 1);

  // Clamp to screen bounds
  if (terminalCursorY >= terminalHeight)
    terminalCursorY = terminalHeight - 1;
  if (terminalCursorX >= terminalWidth)
    terminalCursorX = terminalWidth - 1;
}

void Editor::applyAnsiED(int param) {
  if (param == 0) {  // Erase from cursor to end of screen
    // Clear current line from cursor
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    // Clear subsequent lines
    for (int y = terminalCursorY + 1; y < terminalHeight; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 1) {  // Erase from cursor to beginning of screen
    // Clear current line from beginning to cursor
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
    // Clear preceding lines
    for (int y = 0; y < terminalCursorY; ++y) {
      terminalBuffer[y].assign(
          terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
    }
  } else if (param == 2) {  // Erase entire screen
    clearTerminalBuffer();
  }
}

void Editor::applyAnsiEL(int param) {
  if (param == 0) {  // Erase from cursor to end of line
    for (int x = terminalCursorX; x < terminalWidth; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 1) {  // Erase from cursor to beginning of line
    for (int x = 0; x <= terminalCursorX; ++x) {
      terminalBuffer[terminalCursorY][x] = {' ', currentFgColor, currentBgColor,
                                            currentBold};
    }
  } else if (param == 2) {  // Erase entire line
    terminalBuffer[terminalCursorY].assign(
        terminalWidth, {' ', currentFgColor, currentBgColor, currentBold});
  }
}

void Editor::refreshScreen() {
    updateScreenSize();

    if (mode == EDIT_MODE || mode == PROMPT_MODE) { // PROMPT_MODE also uses the editor display
        scroll();
        drawScreenContent();
    } else if (mode == FILE_EXPLORER_MODE) {
        drawFileExplorer();
    } else if (mode == TERMINAL_MODE) {
        readTerminalOutput();
        drawTerminalScreen();
    }

    drawStatusBar();
    drawMessageBar();

    if (mode == EDIT_MODE) {
        int finalRenderedCursorX = lineNumberWidth + (cxToRx(cursorY, cursorX) - colOffset);
        int finalRenderedCursorY = cursorY - rowOffset;
        setCursorPosition(finalRenderedCursorX, finalRenderedCursorY);
    } else if (mode == PROMPT_MODE) {
        // Place cursor at the end of the prompt in the message bar
        setCursorPosition(promptMessage.length() + searchQuery.length(), screenRows - 1);
    } else if (mode == TERMINAL_MODE) {
        setCursorPosition(terminalCursorX, terminalCursorY);
    }
    else { 
        setCursorPosition(0, (selectedFileIndex - fileExplorerScrollOffset) + (screenRows - 2));
    }
}